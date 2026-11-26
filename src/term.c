#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "term.h"

/* =========================================================================
 * ESTADO PRIVADO DO TERMINAL
 * ========================================================================= */
static volatile sig_atomic_t g_running   = 1;
static volatile sig_atomic_t g_resize    = 0;
static struct termios  g_orig_term;
static int             g_term_w     = 80;
static int             g_term_h     = 24;
static int             g_term_saved = 0;

/* Leitura de teclas: habilitada para TTY, pipe e arquivo; desativada apenas
 * quando read/poll reporta EOF/POLLHUP/erro. Nesse estado o loop apenas dorme
 * o restante do intervalo, sem girar CPU. */
static int             g_keys_enabled = 1;

static char            g_fbuf[FBUF_SIZE];
static int             g_fbuf_pos    = 0;
static int             g_fb_overflow = 0;
static int             g_fb_col      = 0;

/* Notificação de export: texto + timestamp, sem sleeps nem IO no terminal.
 * O footer desenha enquanto now < g_notify_until; o loop de input continua vivo. */
static char            g_notify_msg[512] = {0};
static time_t          g_notify_at       = 0;
static time_t          g_notify_until    = 0;

/* =========================================================================
 * FRAME BUFFER (atomic rendering, no flicker)
 * ========================================================================= */
void fb_reset(void) { g_fbuf_pos = 0; g_fb_overflow = 0; g_fb_col = 0; }

void fb_flush(void) {
    if (g_fbuf_pos > 0) {
        write(STDOUT_FILENO, g_fbuf, (size_t)g_fbuf_pos);
        g_fbuf_pos = 0;
    }
    if (g_fb_overflow) {
        static int reported = 0;
        if (!reported) {
            const char msg[] = "JVMA: frame buffer overflow (fragmento descartado)\n";
            ssize_t n = write(STDERR_FILENO, msg, sizeof(msg) - 1);
            (void)n;
            reported = 1;
        }
    }
}

static void fb_put_raw(const char *data, int len) {
    if (len <= 0) return;
    if (g_fbuf_pos + len < FBUF_SIZE - 2) {
        memcpy(g_fbuf + g_fbuf_pos, data, (size_t)len);
        g_fbuf_pos += len;
    } else {
        int avail = FBUF_SIZE - 2 - g_fbuf_pos;
        if (avail > 0) {
            memcpy(g_fbuf + g_fbuf_pos, data, (size_t)avail);
            g_fbuf_pos += avail;
        }
        g_fb_overflow = 1;
    }
}

void fb_write(const char *s) {
    /* Limita escrita visível por linha a g_term_w; trunca com segurança e
     * sinaliza overflow. Sequências ANSI são preservadas. */
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        if (*p == '\033') {
            const unsigned char *esc = p;
            p++;
            if (*p == '[') {
                p++;
                while (*p && !(*p >= '@' && *p <= '~')) p++;
                if (*p) p++; /* terminador */
            } else if (*p) {
                p++;
            }
            fb_put_raw((const char *)esc, (int)(p - esc));
            continue;
        }
        /* caractere visível: UTF-8 */
        unsigned int cp = 0;
        int seq_len = 1;
        if ((*p & 0x80) == 0) { cp = *p; seq_len = 1; }
        else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((p[0]&0x1F)<<6)|(p[1]&0x3F); seq_len = 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((p[0]&0x0F)<<12)|((p[1]&0x3F)<<6)|(p[2]&0x3F); seq_len = 3; }
        else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = ((p[0]&0x07)<<18)|((p[1]&0x3F)<<12)|((p[2]&0x3F)<<6)|(p[3]&0x3F); seq_len = 4; }
        else { seq_len = 1; cp = *p; }
        /* largura de exibição: 1 para BMP comum usado aqui; 0 para controle */
        int wch = (cp >= 0x20) ? 1 : 0;
        if (g_term_w > 0 && g_fb_col + wch > g_term_w) {
            g_fb_overflow = 1;
            p += seq_len;
            continue;
        }
        fb_put_raw((const char *)p, seq_len);
        g_fb_col += wch;
        p += seq_len;
    }
}

void fb_printf(const char *fmt, ...) {
    char tmp[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    fb_write(tmp);
}

/* Position cursor (0-based row/col → ANSI 1-based) */
void fb_move(int row, int col) { g_fb_col = col < 0 ? 0 : col; fb_printf("\033[%d;%dH", row+1, col+1); }
void fb_fg(int n)   { fb_printf("\033[38;5;%dm", n); }
void fb_bg(int n)   { fb_printf("\033[48;5;%dm", n); }
void fb_bold(void)  { fb_write("\033[1m"); }
void fb_reset_attr(void) { fb_write("\033[0m"); }
void fb_eol(void)   { fb_write("\033[K"); }  /* erase to end of line */

/* Helpers: colored text at position */
void fb_ctext(int row, int col, int fg, int bold, const char *s) {
    fb_move(row, col);
    fb_fg(fg);
    if (bold) fb_bold();
    fb_write(s);
    fb_reset_attr();
}

void fb_cprintf(int row, int col, int fg, int bold, const char *fmt, ...) {
    char tmp[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    fb_ctext(row, col, fg, bold, tmp);
}

void term_printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
}

void term_flush_stdout(void) { fflush(stdout); }

/* =========================================================================
 * TERMINAL MANAGEMENT
 * ========================================================================= */
void term_get_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        g_term_w = ws.ws_col > 0 ? ws.ws_col : 80;
        g_term_h = ws.ws_row > 0 ? ws.ws_row : 24;
    }
}

void term_raw(void) {
    struct termios raw;

    /* Raw mode é exclusivo de TTY; a leitura de teclas vale também para
     * pipe/arquivo (contrato `printf 'ecq' | jvma <PID> <DIR>`). */
    g_keys_enabled = 1;
    if (isatty(STDIN_FILENO)) {
        tcgetattr(STDIN_FILENO, &g_orig_term);
        g_term_saved = 1;
        raw = g_orig_term;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;   /* poll() decide; read() nunca bloqueia */
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    write(STDOUT_FILENO, "\033[?25l", 6); /* hide cursor */
    write(STDOUT_FILENO, "\033[2J\033[H", 7); /* clear screen */
}

void term_restore(void) {
    if (g_term_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term);
    write(STDOUT_FILENO, "\033[?25h", 6); /* show cursor */
    write(STDOUT_FILENO, "\033[0m\033[2J\033[H", 10);
}

int term_width(void)  { return g_term_w; }
int term_height(void) { return g_term_h; }

/* Timing do loop: deadline absoluto por volta (CLOCK_MONOTONIC) e restante
 * recalculado a cada passo. Nada de "elapsed += 100": o próximo frame não
 * herda o erro de arredondamento do anterior. */
void interval_deadline(struct timespec *deadline, int interval_ms)
{
    clock_gettime(CLOCK_MONOTONIC, deadline);
    deadline->tv_sec  += interval_ms / 1000;
    deadline->tv_nsec += (long)(interval_ms % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

/* Milissegundos até o deadline, arredondados para cima para não acordar
 * cedo; 0 quando o prazo já passou. */
int deadline_remaining_ms(const struct timespec *deadline)
{
    struct timespec now;
    long long rem_ns;

    clock_gettime(CLOCK_MONOTONIC, &now);
    rem_ns = ((long long)deadline->tv_sec - now.tv_sec) * 1000000000LL
           + ((long long)deadline->tv_nsec - now.tv_nsec);
    if (rem_ns <= 0) return 0;
    rem_ns = (rem_ns + 999999LL) / 1000000LL;
    return (rem_ns > INT_MAX) ? INT_MAX : (int)rem_ns;
}

/* Espera no máximo timeout_ms e lê 1 byte se ele chegar, inclusive em stdin
 * não-TTY (pipe/arquivo). POLLIN tem precedência sobre POLLHUP: bytes já
 * buffered no pipe são entregues antes de desativar a leitura. Após
 * EOF/POLLHUP/erro o restante do intervalo é dormido com
 * poll(NULL, 0, timeout): nenhuma volta ocupada de CPU. */
int term_read_key(int timeout_ms)
{
    struct pollfd pfd;
    int rc;

    if (timeout_ms <= 0) return -1;

    if (!g_keys_enabled) {
        poll(NULL, 0, timeout_ms);
        return -1;
    }

    pfd.fd      = STDIN_FILENO;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return -1; /* timeout ou EINTR */

    if (pfd.revents & POLLIN) {
        unsigned char ch;
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n == 1) return (int)ch; /* HUP não descarta byte disponível */
        if (n == 0) {
            g_keys_enabled = 0;     /* EOF */
            return -1;
        }
        if (n < 0 && errno != EINTR && errno != EAGAIN) g_keys_enabled = 0;
    }
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
        g_keys_enabled = 0; /* sem dados: desativa após HUP/erro */

    return -1;
}

/* Help overlay: qualquer byte (ou EOF/HUP em pipe/arquivo) fecha;
 * interrompível por sinal. VMIN=0/VTIME=0 mantêm read() não bloqueante e
 * poll() dorme em fatias curtas verificando g_running. O termios local é
 * restaurado ao sair. */
void term_read_key_blocking(void)
{
    struct termios bl;
    int term_saved = (tcgetattr(STDIN_FILENO, &bl) == 0);
    if (term_saved) {
        struct termios raw = bl;
        raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }

    while (g_running) {
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN, .revents = 0 };
        int rc = poll(&pfd, 1, 25);
        if (rc < 0) {
            if (errno == EINTR) continue; /* sinal já atualizou g_running */
            break;
        }
        if (rc == 0) continue;            /* sem tecla: reavalia g_running */
        if (pfd.revents & POLLIN) {
            unsigned char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n == 1 || n == 0) break;  /* qualquer tecla ou EOF fecha */
            if (errno == EINTR) continue;
            break;
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
    }
    if (term_saved) tcsetattr(STDIN_FILENO, TCSANOW, &bl);
}

/* =========================================================================
 * SINAIS
 * ========================================================================= */
static void sig_handler(int s) {
    if (s == SIGINT || s == SIGTERM) { g_running = 0; }
    if (s == SIGWINCH) { g_resize = 1; }
}

/* Instalação explícita das três flags: máscara vazia e SA_RESTART. poll()
 * não é reiniciado por SA_RESTART (EINTR), então o loop continua vendo
 * g_running/g_resize imediatamente após o sinal. */
void term_signals_init(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGINT,   &sa, NULL);
    sigaction(SIGTERM,  &sa, NULL);
    sigaction(SIGWINCH, &sa, NULL);
}

int  term_should_run(void)         { return g_running != 0; }
void term_request_quit(void)       { g_running = 0; }
int  term_consume_resize(void)     { int v = g_resize != 0; g_resize = 0; return v; }

/* =========================================================================
 * NOTIFICAÇÃO DE EXPORT
 * ========================================================================= */
void notify_clear(void) {
    g_notify_msg[0] = '\0';
    g_notify_at = 0;
    g_notify_until = 0;
}

void notify_set(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_notify_msg, sizeof(g_notify_msg), fmt, ap);
    va_end(ap);
    g_notify_at = time(NULL);
    g_notify_until = g_notify_at + NOTIFY_TTL_SEC;
}

int notify_active(time_t now) {
    return g_notify_msg[0] != '\0' && now >= g_notify_at && now < g_notify_until;
}

const char *notify_message(void) { return g_notify_msg; }

/* =========================================================================
 * Hooks de estado para testes — nenhuma global é exposta.
 * ========================================================================= */
int  term_test_running(void)             { return g_running != 0; }
void term_test_set_running(int value)    { g_running = value ? 1 : 0; }
int  term_test_resize(void)              { return g_resize != 0; }
void term_test_set_resize(int value)     { g_resize = value ? 1 : 0; }
int  term_test_keys_enabled(void)        { return g_keys_enabled; }
void term_test_set_keys_enabled(int v)   { g_keys_enabled = v ? 1 : 0; }
void term_test_set_size(int width, int height) {
    g_term_w = width;
    g_term_h = height;
}
void term_test_signal(int sig) { sig_handler(sig); }
void term_test_notify_state(char *msg, size_t cap, time_t *at, time_t *until) {
    snprintf(msg, cap, "%s", g_notify_msg);
    if (at) *at = g_notify_at;
    if (until) *until = g_notify_until;
}
void term_test_set_notify_state(const char *msg, time_t at, time_t until) {
    snprintf(g_notify_msg, sizeof(g_notify_msg), "%s", msg ? msg : "");
    g_notify_at = at;
    g_notify_until = until;
}
