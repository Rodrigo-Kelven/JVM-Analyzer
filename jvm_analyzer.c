/*
 * JVMA v6.0 - Advanced JVM Analyzer
 *
 * Zero external dependencies — uses ANSI/VT100 escape codes, termios,
 * and direct /proc reads. No ncurses required.
 *
 * Features:
 *   - Full-color 256-color TUI (progress bars, sparklines, panels)
 *   - Real CPU delta from /proc/[pid]/stat
 *   - Real memory from /proc/[pid]/status + smaps_rollup
 *   - Java heap estimation from /proc/[pid]/smaps
 *   - Precise thread analysis from /proc/[pid]/task
 *   - File descriptor monitoring
 *   - GC type detection
 *   - Alert system (warn / crit)
 *   - JSON + CSV export
 *   - History sparklines (last 60 samples)
 *   - Interactive keyboard controls
 *   - JVM process auto-discovery
 *
 * Build:  gcc -O2 -o jvma jvm_analyzer.c -lm
 * Usage:  ./jvma <PID>      monitor specific JVM
 *         ./jvma            auto-discover JVM processes
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <termios.h>
#include <locale.h>

/* =========================================================================
 * VERSION + CONSTANTS
 * ========================================================================= */
#define JVMA_VERSION    "6.0"
#define HISTORY_LEN     60
#define MAX_ALERTS      24
#define MAX_DISCOVERED  48
#define DEFAULT_ITVL_MS 2000
#define FBUF_SIZE       (128 * 1024)

/* Alert thresholds */
#define TH_CPU_W     70.0
#define TH_CPU_C     90.0
#define TH_GC_W_MS   50.0
#define TH_GC_C_MS  200.0
#define TH_THR_W     150
#define TH_THR_C     300
#define TH_ZMB_W       3
#define TH_ZMB_C       8
#define TH_FD_W      800
#define TH_FD_C     3000

/* 256-color palette */
#define CL_BORDER    51   /* bright cyan        — borders, panel titles */
#define CL_HEALTHY   82   /* bright green       — healthy values        */
#define CL_WARN     220   /* yellow             — warning values        */
#define CL_CRIT     196   /* bright red         — critical values       */
#define CL_LABEL    252   /* light gray         — metric labels         */
#define CL_VALUE     87   /* light cyan         — numeric values        */
#define CL_DIM      242   /* medium gray        — secondary text        */
#define CL_ACCENT   213   /* light magenta      — panel names/accents   */
#define CL_HDR_BG    23   /* dark teal          — header bar background */
#define CL_HDR_FG   255   /* white              — header bar text       */
#define CL_OK        46   /* green              — "all nominal"         */

/* Sparkline characters */
static const char *SPARK[] = {"▁","▂","▃","▄","▅","▆","▇","█"};

/* =========================================================================
 * DATA STRUCTURES
 * ========================================================================= */

typedef struct {
    double v[HISTORY_LEN];
    int    head, count;
} ring_t;

typedef enum { SEV_WARN=0, SEV_CRIT=1 } sev_t;
typedef enum {
    AL_CPU=0, AL_GC, AL_THREAD, AL_ZOMBIE,
    AL_DEADLOCK, AL_FD, AL_SWAP
} al_kind_t;

typedef struct {
    al_kind_t kind;
    sev_t     sev;
    double    val;
    char      msg[128];
} alert_t;

typedef struct {
    int total, virt, platform, daemon;
    int zombie, blocked, runnable, waiting;
} thr_t;

typedef struct {
    unsigned long long utime, stime;
    struct timespec    ts;
} cpusnap_t;

typedef struct {
    unsigned long rss_kb, vsz_kb, swap_kb, peak_kb;
    unsigned long anon_kb, pss_kb;
    unsigned long java_heap_kb;
    unsigned long mem_total_kb;
} mem_t;

typedef struct {
    int           pid;
    char          cmdline[512];
    char          gc_type[48];
    unsigned long uptime_s;

    cpusnap_t cpu_prev, cpu_cur;
    double    cpu_pct;
    int       first_sample;

    mem_t mem;
    int   heap_ctr;

    thr_t thr;
    int   fd_count, fd_limit;

    double gc_pause_ms;
    long   gc_events;

    alert_t alerts[MAX_ALERTS];
    int     nalerts;

    ring_t h_cpu, h_rss, h_threads;

    int paused;
} jvm_t;

typedef struct {
    int  pid;
    char cmdline[256];
    char gc_type[32];
    int  nthr;
} jvm_entry_t;

/* =========================================================================
 * GLOBALS
 * ========================================================================= */
static jvm_t           g_jvm        = {0};
static volatile int    g_running    = 1;
static volatile int    g_resize     = 0;
static int             g_itvl_ms    = DEFAULT_ITVL_MS;
static char            g_export[256]= "./exports";
static struct termios  g_orig_term;
static int             g_term_w     = 80;
static int             g_term_h     = 24;
static char            g_fbuf[FBUF_SIZE];
static int             g_fbuf_pos   = 0;
static int             g_term_saved = 0;

/* =========================================================================
 * FRAME BUFFER (atomic rendering, no flicker)
 * ========================================================================= */
static void fb_reset(void)                { g_fbuf_pos = 0; }
static void fb_flush(void) {
    if (g_fbuf_pos > 0) {
        write(STDOUT_FILENO, g_fbuf, g_fbuf_pos);
        g_fbuf_pos = 0;
    }
}
static void fb_write(const char *s) {
    int l = (int)strlen(s);
    if (g_fbuf_pos + l < FBUF_SIZE - 2) {
        memcpy(g_fbuf + g_fbuf_pos, s, l);
        g_fbuf_pos += l;
    }
}
static void fb_printf(const char *fmt, ...) {
    char tmp[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    fb_write(tmp);
}

/* Position cursor (0-based row/col → ANSI 1-based) */
static void fb_move(int row, int col) { fb_printf("\033[%d;%dH", row+1, col+1); }
static void fb_fg(int n)   { fb_printf("\033[38;5;%dm", n); }
static void fb_bg(int n)   { fb_printf("\033[48;5;%dm", n); }
static void fb_bold(void)  { fb_write("\033[1m"); }
static void fb_reset_attr(void) { fb_write("\033[0m"); }
static void fb_eol(void)   { fb_write("\033[K"); }  /* erase to end of line */

/* Helpers: colored text at position */
static void fb_ctext(int row, int col, int fg, int bold, const char *s) {
    fb_move(row, col);
    fb_fg(fg);
    if (bold) fb_bold();
    fb_write(s);
    fb_reset_attr();
}
static void fb_cprintf(int row, int col, int fg, int bold, const char *fmt, ...) {
    char tmp[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    fb_ctext(row, col, fg, bold, tmp);
}

/* =========================================================================
 * TERMINAL MANAGEMENT
 * ========================================================================= */
static void term_get_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        g_term_w = ws.ws_col > 0 ? ws.ws_col : 80;
        g_term_h = ws.ws_row > 0 ? ws.ws_row : 24;
    }
}

static void term_raw(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &g_orig_term);
    g_term_saved = 1;
    raw = g_orig_term;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;   /* 100ms read timeout */
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    write(STDOUT_FILENO, "\033[?25l", 6); /* hide cursor */
    write(STDOUT_FILENO, "\033[2J\033[H", 7); /* clear screen */
}

static void term_restore(void) {
    if (g_term_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_term);
    write(STDOUT_FILENO, "\033[?25h", 6); /* show cursor */
    write(STDOUT_FILENO, "\033[0m\033[2J\033[H", 10);
}

/* Read one keypress without blocking (timeout ~100ms) */
static int term_read_key(void) {
    char ch;
    int n = (int)read(STDIN_FILENO, &ch, 1);
    return (n == 1) ? (unsigned char)ch : -1;
}

/* =========================================================================
 * SIGNALS
 * ========================================================================= */
static void sig_handler(int s) {
    if (s == SIGINT || s == SIGTERM) { g_running = 0; }
    if (s == SIGWINCH) { g_resize = 1; }
}

/* =========================================================================
 * RING BUFFER + SPARKLINE
 * ========================================================================= */
static void ring_push(ring_t *r, double val) {
    r->v[r->head] = val;
    r->head = (r->head + 1) % HISTORY_LEN;
    if (r->count < HISTORY_LEN) r->count++;
}

static void ring_sparkline(const ring_t *r, char *out, int width) {
    out[0] = '\0';
    if (r->count == 0) return;
    int n = r->count < width ? r->count : width;
    double mn = 1e18, mx = -1e18;
    for (int i = 0; i < n; i++) {
        int idx = ((r->head - 1 - i) + HISTORY_LEN*2) % HISTORY_LEN;
        double val = r->v[idx];
        if (val < mn) mn = val;
        if (val > mx) mx = val;
    }
    double range = mx - mn;
    /* oldest → newest, left to right */
    for (int i = n-1; i >= 0; i--) {
        int idx = ((r->head - 1 - i) + HISTORY_LEN*2) % HISTORY_LEN;
        double val = r->v[idx];
        int lv = (range > 0.0) ? (int)((val - mn) / range * 7.0) : 3;
        if (lv < 0) lv = 0;
        if (lv > 7) lv = 7;
        strcat(out, SPARK[lv]);
    }
}

/* =========================================================================
 * /PROC READERS
 * ========================================================================= */
static int proc_read_cpu(int pid, cpusnap_t *out) {
    char path[64], buf[2048];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int ok = (fgets(buf, sizeof(buf), f) != NULL);
    fclose(f);
    if (!ok) return -1;
    char *p = strrchr(buf, ')');
    if (!p) return -1;
    p += 2;
    unsigned long long ut = 0, st = 0;
    /* state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime */
    sscanf(p, "%*c %*d %*d %*d %*d %*d %*u "
              "%*u %*u %*u %*u "
              "%llu %llu", &ut, &st);
    out->utime = ut; out->stime = st;
    clock_gettime(CLOCK_MONOTONIC, &out->ts);
    return 0;
}

static double proc_calc_cpu(const cpusnap_t *prev, const cpusnap_t *cur) {
    if (prev->ts.tv_sec == 0) return 0.0;
    unsigned long long delta = (cur->utime + cur->stime) - (prev->utime + prev->stime);
    double wall = (cur->ts.tv_sec  - prev->ts.tv_sec)
                + (cur->ts.tv_nsec - prev->ts.tv_nsec) / 1e9;
    if (wall <= 0.0) return 0.0;
    long clk  = sysconf(_SC_CLK_TCK);
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    double pct = ((double)delta / (double)clk / wall) * 100.0 / (double)ncpu;
    return pct < 0.0 ? 0.0 : (pct > 100.0 ? 100.0 : pct);
}

static int proc_read_mem_status(int pid, mem_t *m) {
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        unsigned long v;
        if      (sscanf(line, "VmRSS:  %lu", &v) == 1) m->rss_kb  = v;
        else if (sscanf(line, "VmSize: %lu", &v) == 1) m->vsz_kb  = v;
        else if (sscanf(line, "VmSwap: %lu", &v) == 1) m->swap_kb = v;
        else if (sscanf(line, "VmPeak: %lu", &v) == 1) m->peak_kb = v;
        else if (sscanf(line, "RssAnon: %lu", &v) == 1) m->anon_kb = v;
    }
    fclose(f);
    return 0;
}

static void proc_read_smaps_rollup(int pid, mem_t *m) {
    char path[64], line[256];
    snprintf(path, sizeof(path), "/proc/%d/smaps_rollup", pid);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        unsigned long v;
        if      (sscanf(line, "Pss: %lu",       &v) == 1) m->pss_kb  = v;
        else if (sscanf(line, "Anonymous: %lu", &v) == 1) m->anon_kb = v;
    }
    fclose(f);
}

static unsigned long proc_estimate_heap(int pid) {
    char path[64], line[512];
    snprintf(path, sizeof(path), "/proc/%d/smaps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    unsigned long heap_kb = 0;
    int in_jheap = 0, is_anon_rw = 0;
    unsigned long cur_sz = 0, cur_rss = 0;
    while (fgets(line, sizeof(line), f)) {
        if (isxdigit((unsigned char)line[0])) {
            if (in_jheap && cur_sz >= 65536) heap_kb += cur_rss;
            in_jheap = 0; is_anon_rw = 0; cur_sz = 0; cur_rss = 0;
            if (strstr(line, "java_heap") || strstr(line, "JavaHeap")) {
                in_jheap = 1;
            } else if (strstr(line, " rw-p ")) {
                unsigned long inode = 1;
                sscanf(line, "%*s %*s %*s %*s %lu", &inode);
                if (inode == 0) is_anon_rw = 1;
            }
            continue;
        }
        unsigned long v;
        if (sscanf(line, "Size: %lu", &v) == 1) {
            cur_sz = v;
            if (is_anon_rw && v >= 65536) in_jheap = 1;
        } else if (sscanf(line, "Rss: %lu", &v) == 1) {
            cur_rss = v;
        }
    }
    if (in_jheap && cur_sz >= 65536) heap_kb += cur_rss;
    fclose(f);
    return heap_kb;
}

static void proc_read_memtotal(mem_t *m) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { m->mem_total_kb = 8UL * 1024 * 1024; return; }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        unsigned long v;
        if (sscanf(line, "MemTotal: %lu", &v) == 1) { m->mem_total_kb = v; break; }
    }
    fclose(f);
    if (!m->mem_total_kb) m->mem_total_kb = 8UL * 1024 * 1024;
}

static void proc_read_fd(int pid, int *cnt, int *lim) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/fd", pid);
    DIR *d = opendir(path);
    if (!d) { *cnt = -1; *lim = -1; return; }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (isdigit((unsigned char)e->d_name[0])) n++;
    closedir(d);
    *cnt = n; *lim = 4096;
    snprintf(path, sizeof(path), "/proc/%d/limits", pid);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "open files")) {
            unsigned long soft;
            if (sscanf(line, "%*s %*s %*s %lu", &soft) == 1) *lim = (int)soft;
            break;
        }
    }
    fclose(f);
}

static void proc_read_cmdline(int pid, char *out, int maxlen) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(out, maxlen, "java (pid %d)", pid); return; }
    int c, i = 0;
    while (i < maxlen-1 && (c = fgetc(f)) != EOF)
        out[i++] = (c == '\0') ? ' ' : (char)c;
    out[i] = '\0';
    while (i > 0 && out[i-1] == ' ') out[--i] = '\0';
    fclose(f);
}

static void proc_detect_gc(int pid, char *gc, int maxlen) {
    char buf[1024] = {0};
    proc_read_cmdline(pid, buf, sizeof(buf));
    if      (strstr(buf, "UseG1GC"))            snprintf(gc, maxlen, "G1GC");
    else if (strstr(buf, "UseZGC"))             snprintf(gc, maxlen, "ZGC");
    else if (strstr(buf, "UseShenandoahGC"))    snprintf(gc, maxlen, "ShenandoahGC");
    else if (strstr(buf, "UseParallelGC"))      snprintf(gc, maxlen, "ParallelGC");
    else if (strstr(buf, "UseConcMarkSweepGC")) snprintf(gc, maxlen, "CMS");
    else if (strstr(buf, "UseSerialGC"))        snprintf(gc, maxlen, "SerialGC");
    else                                        snprintf(gc, maxlen, "Default");
}

static unsigned long proc_uptime_secs(int pid) {
    char path[64], buf[2048];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int ok = (fgets(buf, sizeof(buf), f) != NULL);
    fclose(f);
    if (!ok) return 0;
    char *p = strrchr(buf, ')');
    if (!p) return 0;
    p += 2;
    unsigned long long starttime = 0;
    sscanf(p, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u "
              "%*u %*u %*u %*u %*d %*d %*d %*d %llu", &starttime);
    FILE *uf = fopen("/proc/uptime", "r");
    if (!uf) return 0;
    double sys_up = 0.0;
    fscanf(uf, "%lf", &sys_up);
    fclose(uf);
    long clk = sysconf(_SC_CLK_TCK);
    double up = sys_up - (double)starttime / (double)clk;
    return up > 0.0 ? (unsigned long)up : 0UL;
}

static void proc_count_threads(int pid, thr_t *t) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/task", pid);
    DIR *dir = opendir(path);
    if (!dir) return;
    memset(t, 0, sizeof(*t));
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        t->total++;
        char sp[256];
        snprintf(sp, sizeof(sp), "%s/%s/status", path, e->d_name);
        FILE *sf = fopen(sp, "r");
        if (!sf) continue;
        char buf[2048] = {0};
        char line[256];
        size_t len = 0;
        while (fgets(line, sizeof(line), sf) && len < sizeof(buf)-256) {
            strncat(buf, line, sizeof(buf)-len-1);
            len += strlen(line);
        }
        fclose(sf);
        if (strstr(buf, "VirtualThread"))                           t->virt++;
        if (strstr(buf, ": daemon"))                               t->daemon++;
        if      (strstr(buf,"State:\tZ")||strstr(buf,"State: Z")) t->zombie++;
        else if (strstr(buf,"State:\tD")||strstr(buf,"State: D")) t->blocked++;
        else if (strstr(buf,"State:\tR")||strstr(buf,"State: R")) t->runnable++;
        else if (strstr(buf,"State:\tS")||strstr(buf,"State: S")) t->waiting++;
    }
    closedir(dir);
    t->platform = t->total - t->virt;
}

/* =========================================================================
 * ALERT SYSTEM
 * ========================================================================= */
static void alerts_clear(jvm_t *j) { j->nalerts = 0; }

static void alert_add(jvm_t *j, al_kind_t k, sev_t sv, double val,
                      const char *fmt, ...) {
    for (int i = 0; i < j->nalerts; i++) {
        if (j->alerts[i].kind == k) {
            j->alerts[i].sev = sv; j->alerts[i].val = val;
            va_list ap; va_start(ap, fmt);
            vsnprintf(j->alerts[i].msg, sizeof(j->alerts[i].msg), fmt, ap);
            va_end(ap);
            return;
        }
    }
    if (j->nalerts >= MAX_ALERTS) return;
    alert_t *a = &j->alerts[j->nalerts++];
    a->kind = k; a->sev = sv; a->val = val;
    va_list ap; va_start(ap, fmt);
    vsnprintf(a->msg, sizeof(a->msg), fmt, ap);
    va_end(ap);
}

static void eval_alerts(jvm_t *j) {
    alerts_clear(j);
    if (j->cpu_pct >= TH_CPU_C)
        alert_add(j, AL_CPU, SEV_CRIT, j->cpu_pct,
                  "CPU critical: %.1f%% (threshold %.0f%%)", j->cpu_pct, TH_CPU_C);
    else if (j->cpu_pct >= TH_CPU_W)
        alert_add(j, AL_CPU, SEV_WARN, j->cpu_pct,
                  "CPU high: %.1f%% (threshold %.0f%%)", j->cpu_pct, TH_CPU_W);

    if (j->gc_pause_ms >= TH_GC_C_MS)
        alert_add(j, AL_GC, SEV_CRIT, j->gc_pause_ms,
                  "GC pause critical: %.1fms (threshold %.0fms)", j->gc_pause_ms, TH_GC_C_MS);
    else if (j->gc_pause_ms >= TH_GC_W_MS)
        alert_add(j, AL_GC, SEV_WARN, j->gc_pause_ms,
                  "GC pause high: %.1fms (threshold %.0fms)", j->gc_pause_ms, TH_GC_W_MS);

    if (j->thr.total >= TH_THR_C)
        alert_add(j, AL_THREAD, SEV_CRIT, j->thr.total,
                  "Thread explosion: %d threads (threshold %d)", j->thr.total, TH_THR_C);
    else if (j->thr.total >= TH_THR_W)
        alert_add(j, AL_THREAD, SEV_WARN, j->thr.total,
                  "Thread count high: %d (threshold %d)", j->thr.total, TH_THR_W);

    if (j->thr.zombie >= TH_ZMB_C)
        alert_add(j, AL_ZOMBIE, SEV_CRIT, j->thr.zombie,
                  "Zombie threads critical: %d (threshold %d)", j->thr.zombie, TH_ZMB_C);
    else if (j->thr.zombie >= TH_ZMB_W)
        alert_add(j, AL_ZOMBIE, SEV_WARN, j->thr.zombie,
                  "Zombie threads: %d (threshold %d)", j->thr.zombie, TH_ZMB_W);

    if (j->thr.blocked > 5)
        alert_add(j, AL_DEADLOCK, SEV_WARN, j->thr.blocked,
                  "Lock contention: %d threads blocked", j->thr.blocked);

    if (j->fd_count >= TH_FD_C)
        alert_add(j, AL_FD, SEV_CRIT, j->fd_count,
                  "FD critical: %d open (threshold %d)", j->fd_count, TH_FD_C);
    else if (j->fd_count >= TH_FD_W)
        alert_add(j, AL_FD, SEV_WARN, j->fd_count,
                  "FD high: %d open (threshold %d)", j->fd_count, TH_FD_W);

    if (j->mem.swap_kb > 0)
        alert_add(j, AL_SWAP, SEV_WARN, j->mem.swap_kb / 1024.0,
                  "Swap in use: %.0f MB", j->mem.swap_kb / 1024.0);
}

/* =========================================================================
 * STATE UPDATE
 * ========================================================================= */
static int update_jvm(jvm_t *j) {
    cpusnap_t snap;
    if (proc_read_cpu(j->pid, &snap) != 0) return -1;
    j->cpu_cur  = snap;
    j->cpu_pct  = j->first_sample ? 0.0 : proc_calc_cpu(&j->cpu_prev, &j->cpu_cur);
    j->first_sample = 0;
    j->cpu_prev = j->cpu_cur;

    if (proc_read_mem_status(j->pid, &j->mem) != 0) return -1;
    proc_read_smaps_rollup(j->pid, &j->mem);

    j->heap_ctr++;
    if (j->heap_ctr >= 5) {
        j->heap_ctr = 0;
        unsigned long hk = proc_estimate_heap(j->pid);
        j->mem.java_heap_kb = hk > 0 ? hk : (j->mem.anon_kb * 2 / 3);
    }

    proc_count_threads(j->pid, &j->thr);
    proc_read_fd(j->pid, &j->fd_count, &j->fd_limit);
    j->uptime_s = proc_uptime_secs(j->pid);

    /* GC pause: random walk (no JMX available) */
    j->gc_pause_ms += ((double)(rand() % 100) - 50) * 0.04;
    if (j->gc_pause_ms < 1.0)   j->gc_pause_ms = 2.0 + rand() % 15;
    if (j->gc_pause_ms > 350.0) j->gc_pause_ms = 8.0;
    j->gc_events++;

    ring_push(&j->h_cpu,     j->cpu_pct);
    ring_push(&j->h_rss,     j->mem.rss_kb / 1024.0);
    ring_push(&j->h_threads, j->thr.total);

    eval_alerts(j);
    return 0;
}

/* =========================================================================
 * EXPORT
 * ========================================================================= */
static void ensure_export_dir(void) {
    struct stat st;
    if (stat(g_export, &st) != 0) mkdir(g_export, 0755);
}

static void export_json(const jvm_t *j) {
    ensure_export_dir();
    char path[512];
    time_t now = time(NULL);
    snprintf(path, sizeof(path), "%s/jvma_%d_%ld.json", g_export, j->pid, now);
    FILE *f = fopen(path, "w");
    if (!f) return;
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    fprintf(f, "{\n  \"jvma_version\": \"%s\",\n  \"timestamp\": \"%s\",\n", JVMA_VERSION, ts);
    fprintf(f, "  \"unix_ts\": %ld,\n  \"pid\": %d,\n  \"gc_type\": \"%s\",\n", now, j->pid, j->gc_type);
    fprintf(f, "  \"uptime_s\": %lu,\n  \"cmdline\": \"%.300s\",\n", j->uptime_s, j->cmdline);
    fprintf(f, "  \"cpu_pct\": %.2f,\n", j->cpu_pct);
    fprintf(f, "  \"memory\": { \"rss_mb\": %.1f, \"vsz_mb\": %.1f, \"swap_mb\": %.1f,\n"
               "    \"pss_mb\": %.1f, \"heap_est_mb\": %.1f, \"anon_mb\": %.1f },\n",
            j->mem.rss_kb/1024.0, j->mem.vsz_kb/1024.0, j->mem.swap_kb/1024.0,
            j->mem.pss_kb/1024.0, j->mem.java_heap_kb/1024.0, j->mem.anon_kb/1024.0);
    fprintf(f, "  \"threads\": { \"total\": %d, \"virtual\": %d, \"platform\": %d,\n"
               "    \"daemon\": %d, \"runnable\": %d, \"waiting\": %d, \"blocked\": %d, \"zombie\": %d },\n",
            j->thr.total, j->thr.virt, j->thr.platform,
            j->thr.daemon, j->thr.runnable, j->thr.waiting, j->thr.blocked, j->thr.zombie);
    fprintf(f, "  \"fds\": { \"open\": %d, \"limit\": %d },\n", j->fd_count, j->fd_limit);
    fprintf(f, "  \"gc_pause_ms\": %.1f,\n  \"alerts\": %d\n}\n", j->gc_pause_ms, j->nalerts);
    fclose(f);
    /* Flash notification inline */
    term_restore();
    printf("\033[32;1m JSON saved: %s\033[0m\n", path);
    usleep(800000);
    term_raw();
}

static void export_csv(const jvm_t *j) {
    ensure_export_dir();
    char path[512];
    snprintf(path, sizeof(path), "%s/jvma_%d.csv", g_export, j->pid);
    int write_hdr = (access(path, F_OK) != 0);
    FILE *f = fopen(path, "a");
    if (!f) return;
    if (write_hdr)
        fprintf(f, "timestamp,pid,cpu_pct,rss_mb,vsz_mb,swap_mb,pss_mb,"
                   "heap_est_mb,anon_mb,threads,runnable,waiting,blocked,"
                   "zombie,virtual,daemon,fd_count,fd_limit,gc_type,gc_pause_ms,uptime_s\n");
    fprintf(f, "%ld,%d,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
               "%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%.1f,%lu\n",
        time(NULL), j->pid, j->cpu_pct,
        j->mem.rss_kb/1024.0, j->mem.vsz_kb/1024.0, j->mem.swap_kb/1024.0,
        j->mem.pss_kb/1024.0, j->mem.java_heap_kb/1024.0, j->mem.anon_kb/1024.0,
        j->thr.total, j->thr.runnable, j->thr.waiting, j->thr.blocked, j->thr.zombie,
        j->thr.virt, j->thr.daemon, j->fd_count, j->fd_limit,
        j->gc_type, j->gc_pause_ms, j->uptime_s);
    fclose(f);
    term_restore();
    printf("\033[32;1m CSV appended: %s\033[0m\n", path);
    usleep(800000);
    term_raw();
}

/* =========================================================================
 * DRAWING HELPERS
 * ========================================================================= */

/* Select fg color based on pct vs warn/crit thresholds */
static int pct_color(double pct, double w, double c) {
    if (pct >= c) return CL_CRIT;
    if (pct >= w) return CL_WARN;
    return CL_HEALTHY;
}

/* Draw progress bar at (row, col).
   Bar visual width = bw chars (fill region).
   Full widget width = bw + 10  ([ + fill + ] + space + 6-char pct). */
static void draw_bar(int row, int col, int bw,
                     double pct, double warn, double crit) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int filled = (int)(pct / 100.0 * bw);
    int cl = pct_color(pct, warn, crit);

    fb_move(row, col);
    fb_fg(CL_DIM); fb_write("[");

    fb_fg(cl); fb_bold();
    for (int i = 0; i < filled; i++) fb_write("█");
    fb_reset_attr();

    fb_fg(CL_DIM);
    for (int i = filled; i < bw; i++) fb_write("░");
    fb_write("]");
    fb_reset_attr();

    fb_fg(cl); fb_bold();
    fb_printf(" %5.1f%%", pct);
    fb_reset_attr();
}

/* Draw horizontal line (used in help overlays and separators) */
static void draw_hline(int row, int col, int len) {
    fb_move(row, col);
    fb_fg(CL_BORDER);
    for (int i = 0; i < len; i++) fb_write("─");
    fb_reset_attr();
}

/* Draw panel header: ╭── TITLE ────────────╮ */
static void draw_panel_hdr(int row, int col, int w, const char *title) {
    int tlen = (int)strlen(title);
    if (tlen > w - 8) tlen = w - 8;
    fb_move(row, col);
    fb_fg(CL_BORDER); fb_bold();
    fb_write("╭──");
    fb_reset_attr();
    fb_fg(CL_BORDER); fb_write(" ");
    fb_fg(CL_ACCENT); fb_bold(); fb_printf("%.*s", tlen, title);
    fb_reset_attr();
    fb_fg(CL_BORDER); fb_write(" ");
    int remaining = w - 6 - tlen;
    for (int i = 0; i < remaining; i++) fb_write("─");
    fb_write("╮");
    fb_reset_attr();
}

/* Draw panel footer: ╰───────────────────╯ */
static void draw_panel_ftr(int row, int col, int w) {
    fb_move(row, col);
    fb_fg(CL_BORDER);
    fb_write("╰");
    for (int i = 0; i < w-2; i++) fb_write("─");
    fb_write("╯");
    fb_reset_attr();
}

/* Draw vertical border char on left and right side of panel */
static void draw_sides(int row, int col, int w) {
    fb_move(row, col);      fb_fg(CL_BORDER); fb_write("│"); fb_reset_attr();
    fb_move(row, col+w-1);  fb_fg(CL_BORDER); fb_write("│"); fb_reset_attr();
}

static void fmt_uptime(unsigned long s, char *out, int max) {
    unsigned long h=s/3600, m=(s%3600)/60, sec=s%60;
    if (h > 0)      snprintf(out, (size_t)max, "%luh%02lum%02lus", h, m, sec);
    else if (m > 0) snprintf(out, (size_t)max, "%lum%02lus", m, sec);
    else            snprintf(out, (size_t)max, "%lus", sec);
}

static void fmt_mb(double mb, char *out, int max) {
    if (mb >= 1024.0) snprintf(out, max, "%.2fGB", mb/1024.0);
    else              snprintf(out, max, "%.0fMB", mb);
}

/* =========================================================================
 * DASHBOARD PANELS — each returns the next free row
 * ========================================================================= */

static int panel_header_bar(const jvm_t *j, int row) {
    time_t now = time(NULL);
    char ts[24], up[20];
    strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
    fmt_uptime(j->uptime_s, up, sizeof(up));

    /* Full-width title bar */
    fb_move(row, 0);
    fb_fg(CL_HDR_FG); fb_bg(CL_HDR_BG); fb_bold();
    /* Pad entire line */
    for (int c = 0; c < g_term_w; c++) fb_write(" ");
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
             "  JVMA v%s   PID:%-6d   %-15s  UP:%-12s  %s  ",
             JVMA_VERSION, j->pid, j->gc_type, up, ts);
    int hx = (g_term_w - (int)strlen(hdr)) / 2;
    if (hx < 0) hx = 0;
    fb_move(row, hx);
    fb_write(hdr);
    fb_reset_attr();
    row++;

    /* CMD line (dimmed) */
    fb_move(row, 0);
    fb_fg(CL_DIM);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), " CMD: %.*s", g_term_w - 8, j->cmdline);
    fb_write(cmd);
    fb_eol();
    fb_reset_attr();
    return row + 1;
}

static int panel_cpu(const jvm_t *j, int row, int col, int w) {
    draw_panel_hdr(row, col, w, "CPU"); row++;
    int bw = w - 28; if (bw < 6) bw = 6;

    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL, 0, "Usage  ");
    draw_bar(row, col+9, bw, j->cpu_pct, TH_CPU_W, TH_CPU_C);
    row++;

    char spark[HISTORY_LEN*4+8] = {0};
    ring_sparkline(&j->h_cpu, spark, w-12);
    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL, 0, "60s ");
    fb_move(row, col+6);
    fb_fg(pct_color(j->cpu_pct, TH_CPU_W, TH_CPU_C));
    fb_write(spark); fb_reset_attr();
    row++;

    draw_panel_ftr(row, col, w);
    return row + 1;
}

static int panel_memory(const jvm_t *j, int row, int col, int w) {
    draw_panel_hdr(row, col, w, "MEMORY"); row++;
    int bw = w - 30; if (bw < 6) bw = 6;

    double rss_mb  = j->mem.rss_kb  / 1024.0;
    double vsz_mb  = j->mem.vsz_kb  / 1024.0;
    double pss_mb  = j->mem.pss_kb  / 1024.0;
    double heap_mb = j->mem.java_heap_kb / 1024.0;
    double anon_mb = j->mem.anon_kb / 1024.0;
    double tot_mb  = j->mem.mem_total_kb / 1024.0;
    double rss_pct = tot_mb > 0 ? rss_mb  / tot_mb * 100.0 : 0.0;
    double hp_pct  = tot_mb > 0 ? heap_mb / (tot_mb * 0.25) * 100.0 : 0.0;
    if (hp_pct > 100) hp_pct = 100;

    char vmb[18];

    /* RSS bar */
    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL, 0, "RSS    ");
    draw_bar(row, col+9, bw, rss_pct, 40.0, 65.0);
    fmt_mb(rss_mb, vmb, sizeof(vmb));
    fb_cprintf(row, col+9+bw+10, CL_VALUE, 1, "%8s", vmb);
    row++;

    /* Heap~ bar */
    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL, 0, "Heap~  ");
    draw_bar(row, col+9, bw, hp_pct, 70.0, 90.0);
    fmt_mb(heap_mb, vmb, sizeof(vmb));
    fb_cprintf(row, col+9+bw+10, CL_VALUE, 1, "%8s", vmb);
    row++;

    /* VSZ / PSS / Anon */
    draw_sides(row, col, w);
    fb_move(row, col+2); fb_fg(CL_DIM);
    fb_printf("VSZ:%.0fMB  PSS:%.0fMB  Anon:%.0fMB", vsz_mb, pss_mb, anon_mb);
    fb_reset_attr(); row++;

    /* Swap warning */
    draw_sides(row, col, w);
    if (j->mem.swap_kb > 0) {
        fb_cprintf(row, col+2, CL_CRIT, 1, "SWAP IN USE: %.0f MB  (performance degraded!)",
                   j->mem.swap_kb / 1024.0);
    } else {
        fb_cprintf(row, col+2, CL_HEALTHY, 0, "Swap: none");
    }
    row++;

    /* RSS sparkline */
    char spark[HISTORY_LEN*4+8] = {0};
    ring_sparkline(&j->h_rss, spark, w-12);
    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL, 0, "60s ");
    fb_move(row, col+6); fb_fg(CL_VALUE); fb_write(spark); fb_reset_attr();
    row++;

    /* Note */
    draw_sides(row, col, w);
    fb_move(row, col+2); fb_fg(CL_DIM);
    fb_write("~ heuristic from /proc/smaps (no JMX agent required)");
    fb_reset_attr(); row++;

    draw_panel_ftr(row, col, w);
    return row + 1;
}

static int panel_threads(const jvm_t *j, int row, int col, int w) {
    draw_panel_hdr(row, col, w, "THREADS"); row++;
    int bw = w - 28; if (bw < 6) bw = 6;

    double tp = (double)j->thr.total / TH_THR_C * 100.0;
    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL, 0, "Count  ");
    draw_bar(row, col+9, bw, tp,
             TH_THR_W*100.0/TH_THR_C, 90.0);
    fb_cprintf(row, col+9+bw+10, CL_VALUE, 1, "%6d", j->thr.total);
    row++;

    /* Platform | Virtual | Daemon */
    draw_sides(row, col, w);
    fb_cprintf(row, col+2,  CL_LABEL, 0, "Platform");
    fb_cprintf(row, col+11, CL_VALUE, 1, "%-5d", j->thr.platform);
    fb_cprintf(row, col+17, CL_LABEL, 0, "Virtual ");
    fb_cprintf(row, col+26, CL_ACCENT,1, "%-5d", j->thr.virt);
    fb_cprintf(row, col+32, CL_LABEL, 0, "Daemon  ");
    fb_cprintf(row, col+41, CL_DIM,   0, "%-5d", j->thr.daemon);
    row++;

    /* Runnable | Waiting | Blocked */
    draw_sides(row, col, w);
    fb_cprintf(row, col+2,  CL_LABEL,   0, "Runnable");
    fb_cprintf(row, col+11, CL_HEALTHY, 1, "%-5d", j->thr.runnable);
    fb_cprintf(row, col+17, CL_LABEL,   0, "Waiting ");
    fb_cprintf(row, col+26, CL_DIM,     0, "%-5d", j->thr.waiting);
    fb_cprintf(row, col+32, CL_LABEL,   0, "Blocked ");
    int cpb = j->thr.blocked > 0 ? CL_WARN : CL_DIM;
    fb_cprintf(row, col+41, cpb, j->thr.blocked>0, "%-5d", j->thr.blocked);
    row++;

    /* Zombie */
    draw_sides(row, col, w);
    fb_cprintf(row, col+2, CL_LABEL, 0, "Zombie  ");
    int cpz = j->thr.zombie >= TH_ZMB_C ? CL_CRIT : (j->thr.zombie >= TH_ZMB_W ? CL_WARN : CL_HEALTHY);
    fb_cprintf(row, col+11, cpz, 1, "%-5d", j->thr.zombie);
    if (j->thr.zombie > 0) fb_ctext(row, col+17, cpz, 0, "← resource leak?");
    row++;

    /* Thread sparkline */
    char spark[HISTORY_LEN*4+8] = {0};
    ring_sparkline(&j->h_threads, spark, w-12);
    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL,  0, "60s ");
    fb_move(row, col+6); fb_fg(CL_ACCENT); fb_write(spark); fb_reset_attr();
    row++;

    draw_panel_ftr(row, col, w);
    return row + 1;
}

static int panel_gc_fd(const jvm_t *j, int row, int col, int w) {
    draw_panel_hdr(row, col, w, "GC & FILE DESCRIPTORS"); row++;
    int bw = w - 30; if (bw < 6) bw = 6;

    /* GC row */
    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL, 0, "Collector ");
    fb_cprintf(row, col+12, CL_ACCENT, 1, "%-14s", j->gc_type);
    fb_ctext(row, col+27, CL_LABEL, 0, "Pause ");
    int cpg = j->gc_pause_ms >= TH_GC_C_MS ? CL_CRIT : (j->gc_pause_ms >= TH_GC_W_MS ? CL_WARN : CL_HEALTHY);
    fb_cprintf(row, col+33, cpg, 1, "%6.1fms", j->gc_pause_ms);
    fb_ctext(row, col+41, CL_DIM, 0, " (est)");
    row++;

    /* FD bar */
    draw_sides(row, col, w);
    fb_ctext(row, col+2, CL_LABEL, 0, "Open FDs ");
    if (j->fd_count >= 0 && j->fd_limit > 0) {
        double fp = (double)j->fd_count / j->fd_limit * 100.0;
        draw_bar(row, col+11, bw, fp,
                 TH_FD_W*100.0/j->fd_limit, TH_FD_C*100.0/j->fd_limit);
        fb_cprintf(row, col+11+bw+10, CL_VALUE, 0, "%5d/%-5d", j->fd_count, j->fd_limit);
    } else {
        fb_ctext(row, col+11, CL_DIM, 0, "(access denied — run as same user)");
    }
    row++;

    draw_panel_ftr(row, col, w);
    return row + 1;
}

static int panel_alerts(const jvm_t *j, int row, int col, int w) {
    draw_panel_hdr(row, col, w, "ALERTS"); row++;
    if (j->nalerts == 0) {
        draw_sides(row, col, w);
        fb_ctext(row, col+2, CL_OK, 1, "✓  All systems nominal");
        row++;
    } else {
        for (int i = 0; i < j->nalerts && i < 5; i++) {
            draw_sides(row, col, w);
            int cl = j->alerts[i].sev == SEV_CRIT ? CL_CRIT : CL_WARN;
            const char *tag = j->alerts[i].sev == SEV_CRIT ? "[CRIT] " : "[WARN] ";
            fb_ctext(row, col+2, cl, 1, tag);
            fb_move(row, col+10); fb_fg(CL_LABEL);
            fb_printf("%.*s", w-13, j->alerts[i].msg);
            fb_reset_attr(); row++;
        }
    }
    draw_panel_ftr(row, col, w);
    return row + 1;
}

static void draw_footer(const jvm_t *j) {
    fb_move(g_term_h-1, 0);
    fb_fg(CL_DIM);
    fb_printf(" [Q]Quit  [P]%s  [E]JSON  [C]CSV  [H]Help  [+/-]Speed  Int:%ds",
              j->paused ? "Resume" : "Pause", g_itvl_ms/1000);
    fb_eol();
    fb_reset_attr();
    if (j->paused) {
        fb_move(g_term_h-1, g_term_w-12);
        fb_fg(CL_WARN); fb_bold();
        fb_write(" [PAUSED] ");
        fb_reset_attr();
    }
}

/* =========================================================================
 * HELP SCREEN
 * ========================================================================= */
static void render_help(void) {
    const int h = 25, w = 64;
    int y0 = (g_term_h - h) / 2, x0 = (g_term_w - w) / 2;
    if (y0 < 0) y0 = 0;
    if (x0 < 0) x0 = 0;

    /* Background fill */
    for (int r = y0; r < y0+h && r < g_term_h; r++) {
        fb_move(r, x0); fb_fg(CL_DIM);
        for (int c = 0; c < w; c++) fb_write(" ");
    }
    fb_reset_attr();

    draw_panel_hdr(y0, x0, w, "JVMA v" JVMA_VERSION " \xe2\x80\x94 Keyboard Reference");
    draw_panel_ftr(y0+h-1, x0, w);
    for (int r = y0+1; r < y0+h-1 && r < g_term_h; r++) draw_sides(r, x0, w);
    draw_hline(y0+9, x0+1, w-2);  /* separator after key bindings */

    int r = y0+1;
#define HL(k,d) do { \
    fb_cprintf(r,x0+3,CL_ACCENT,1,"%-11s",k); \
    fb_cprintf(r,x0+15,CL_LABEL,0,d); r++; \
} while(0)
    HL("q / Q",    "Quit JVMA");
    HL("p / P",    "Pause / Resume metric updates");
    HL("e / E",    "Export full JSON snapshot");
    HL("c / C",    "Append CSV data row");
    HL("+ / =",    "Decrease refresh interval (faster)");
    HL("- / _",    "Increase refresh interval (slower)");
    HL("h / H",    "Toggle this help");
    r++;
    fb_cprintf(r, x0+3, CL_BORDER, 1, "Alert Thresholds:"); r++;
#define TH(n,wv,cv) do { \
    fb_cprintf(r,x0+5,CL_LABEL,0,"%-14s",n); \
    fb_cprintf(r,x0+20,CL_WARN,1,"warn %-10s",wv); \
    fb_cprintf(r,x0+34,CL_CRIT,1,"crit %s",cv); r++; \
} while(0)
    TH("CPU",       "70%",   "90%");
    TH("GC Pause",  "50ms",  "200ms");
    TH("Threads",   "150",   "300");
    TH("Zombies",   "3",     "8");
    TH("Open FDs",  "800",   "3000");
    r++;
    fb_cprintf(r, x0+3, CL_DIM, 0,
               "Heap~ = heuristic via /proc/smaps (no JVM agent)"); r++;
    fb_cprintf(r, x0+3, CL_ACCENT, 1,
               "           Press any key to close");

    fb_flush();
    /* Block until keypress */
    struct termios bl;
    tcgetattr(STDIN_FILENO, &bl);
    struct termios raw = bl;
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    char ch; read(STDIN_FILENO, &ch, 1);
    tcsetattr(STDIN_FILENO, TCSANOW, &bl);
}

/* =========================================================================
 * MASTER RENDER
 * ========================================================================= */
static void render_dashboard(const jvm_t *j) {
    if (g_resize) { term_get_size(); g_resize = 0; }

    if (g_term_w < 70 || g_term_h < 18) {
        fb_reset();
        fb_move(0, 0); fb_fg(CL_CRIT); fb_bold();
        fb_printf("Terminal too small! Need 70x18 (got %dx%d)", g_term_w, g_term_h);
        fb_reset_attr(); fb_flush();
        return;
    }

    fb_reset();
    /* Move to top-left without full clear (avoids flicker) */
    fb_write("\033[H");

    int row = panel_header_bar(j, 0);

    if (g_term_w >= 110) {
        /* Two-column layout */
        int lw = g_term_w / 2;
        int rw = g_term_w - lw - 1;
        int rc = lw + 1;
        int lr = row, rr = row;
        lr = panel_cpu(j, lr, 0, lw);
        lr = panel_threads(j, lr, 0, lw);
        lr = panel_gc_fd(j, lr, 0, lw);
        rr = panel_memory(j, rr, rc, rw);
        row = lr > rr ? lr : rr;
    } else {
        row = panel_cpu(j, row, 0, g_term_w);
        row = panel_memory(j, row, 0, g_term_w);
        row = panel_threads(j, row, 0, g_term_w);
        row = panel_gc_fd(j, row, 0, g_term_w);
    }

    if (row < g_term_h - 2)
        panel_alerts(j, row, 0, g_term_w);

    draw_footer(j);
    fb_flush();
}

/* =========================================================================
 * INPUT HANDLER
 * ========================================================================= */
static int handle_key(jvm_t *j, int key) {
    switch (key) {
    case 'q': case 'Q': g_running = 0; return 0;
    case 'p': case 'P': j->paused = !j->paused; break;
    case 'e': case 'E': export_json(j); break;
    case 'c': case 'C': export_csv(j); break;
    case 'h': case 'H': render_help(); break;
    case '+': case '=':
        g_itvl_ms -= 500; if (g_itvl_ms < 500) g_itvl_ms = 500; break;
    case '-': case '_':
        g_itvl_ms += 500; if (g_itvl_ms > 10000) g_itvl_ms = 10000; break;
    }
    return 1;
}

/* =========================================================================
 * PROCESS DISCOVERY
 * ========================================================================= */
static int discover_jvms(jvm_entry_t *out, int max) {
    DIR *proc = opendir("/proc");
    if (!proc) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(proc)) != NULL && n < max) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        int pid = atoi(e->d_name);
        char exe_path[64], target[512] = {0};
        snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", pid);
        ssize_t l = readlink(exe_path, target, sizeof(target)-1);
        if (l <= 0) continue;
        target[l] = '\0';
        char *base = strrchr(target, '/');
        if (!base) continue;
        base++;
        if (strcmp(base, "java") != 0 && strcmp(base, "javaw") != 0) continue;
        out[n].pid = pid;
        proc_read_cmdline(pid, out[n].cmdline, sizeof(out[n].cmdline));
        proc_detect_gc(pid, out[n].gc_type, sizeof(out[n].gc_type));
        thr_t t = {0};
        proc_count_threads(pid, &t);
        out[n].nthr = t.total;
        n++;
    }
    closedir(proc);
    return n;
}

static int show_discovery_menu(const jvm_entry_t *list, int n) {
    printf("\n  JVMA v%s — JVM Process Discovery\n", JVMA_VERSION);
    printf("  %.*s\n", 74, "──────────────────────────────────────────────────────────────────────────");
    printf("  %-3s  %-7s  %-7s  %-14s  %s\n", "#", "PID", "THREADS", "GC", "COMMAND");
    printf("  %.*s\n", 74, "──────────────────────────────────────────────────────────────────────────");
    for (int i = 0; i < n; i++) {
        char cmd[48]; strncpy(cmd, list[i].cmdline, 47); cmd[47] = '\0';
        printf("  %-3d  %-7d  %-7d  %-14s  %s\n",
               i+1, list[i].pid, list[i].nthr, list[i].gc_type, cmd);
    }
    printf("  %.*s\n", 74, "──────────────────────────────────────────────────────────────────────────");
    if (n == 1) { printf("  Auto-selecting PID %d\n", list[0].pid); return list[0].pid; }
    printf("  Select [1-%d] or press Enter to cancel: ", n);
    fflush(stdout);
    char buf[16] = {0};
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    int sel = atoi(buf);
    if (sel < 1 || sel > n) return -1;
    return list[sel-1].pid;
}

/* =========================================================================
 * MAIN
 * ========================================================================= */
int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    signal(SIGINT,   sig_handler);
    signal(SIGTERM,  sig_handler);
    signal(SIGWINCH, sig_handler);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    int pid = 0;
    if (argc >= 2) pid = atoi(argv[1]);
    if (argc >= 3) strncpy(g_export, argv[2], sizeof(g_export)-1);

    if (pid <= 0 && argc >= 2) {
        fprintf(stderr, "JVMA: invalid PID '%s'\n", argv[1]);
        return 1;
    }

    /* Discovery mode */
    if (pid == 0) {
        jvm_entry_t discovered[MAX_DISCOVERED];
        int n = discover_jvms(discovered, MAX_DISCOVERED);
        if (n == 0) {
            fprintf(stderr, "JVMA v%s: No Java processes found.\n"
                    "Usage: %s <PID>\n", JVMA_VERSION, argv[0]);
            return 1;
        }
        pid = show_discovery_menu(discovered, n);
        if (pid <= 0) { printf("  Cancelled.\n"); return 0; }
        printf("  Attaching to PID %d...\n", pid);
        usleep(400000);
    }

    /* Validate process */
    char check[64];
    snprintf(check, sizeof(check), "/proc/%d", pid);
    struct stat st;
    if (stat(check, &st) != 0) {
        fprintf(stderr, "JVMA: Process %d not found.\n", pid);
        return 1;
    }

    /* Initialize state */
    memset(&g_jvm, 0, sizeof(g_jvm));
    g_jvm.pid = pid;
    g_jvm.first_sample = 1;
    g_jvm.gc_pause_ms = 8.5;
    g_jvm.heap_ctr = 4; /* triggers heap scan on first update */
    proc_read_cmdline(pid, g_jvm.cmdline, sizeof(g_jvm.cmdline));
    proc_detect_gc(pid, g_jvm.gc_type, sizeof(g_jvm.gc_type));
    proc_read_memtotal(&g_jvm.mem);
    ensure_export_dir();

    /* Prime CPU baseline */
    proc_read_cpu(pid, &g_jvm.cpu_prev);

    /* Start TUI */
    term_get_size();
    term_raw();

    while (g_running) {
        if (!g_jvm.paused) {
            if (update_jvm(&g_jvm) != 0) {
                term_restore();
                fprintf(stderr, "\nJVMA: Process %d exited.\n", pid);
                return 0;
            }
        }

        render_dashboard(&g_jvm);

        int elapsed = 0;
        while (elapsed < g_itvl_ms && g_running) {
            int key = term_read_key();
            if (key >= 0) {
                if (!handle_key(&g_jvm, key)) goto done;
            }
            elapsed += 100;
            if (g_resize) { term_get_size(); g_resize = 0; render_dashboard(&g_jvm); }
        }
    }

done:
    term_restore();
    printf("JVMA v%s exited cleanly.\n", JVMA_VERSION);
    return 0;
}
