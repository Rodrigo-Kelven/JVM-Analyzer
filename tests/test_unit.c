/*
 * JVMA v6.0 — testes unitários (subtarefa 6 + correção P0 da subtarefa 7).
 *
 * A inclusão do fonte com `main` renomeado permite testar funções static
 * sem alterar jvm_analyzer.c. As verificações usam apenas contratos estáveis
 * (ring, helpers de formatação, proc_calc_cpu, export e posição do footer),
 * válidos antes e depois das correções 8-12 e das extrações 13-18.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/time.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define main jvma_main
#include "../jvm_analyzer.c"
#undef main
#pragma GCC diagnostic pop

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, ...) do {                                                \
    g_checks++;                                                              \
    if (!(cond)) {                                                           \
        g_failed++;                                                          \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                 \
        fprintf(stderr, __VA_ARGS__);                                        \
        fputc('\n', stderr);                                                 \
    }                                                                        \
} while (0)

/* ------------------------------------------------------------------ *
 * Utilitários de baixo nível (sem dependências externas)
 * ------------------------------------------------------------------ */

/* Cada glyph do SPARK é UTF-8 de 3 bytes: E2 96 81..88. */
static int glyph_count(const char *s)
{
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ) {
        if (p[0] != 0xE2 || p[1] != 0x96 || p[2] < 0x81 || p[2] > 0x88)
            return -1;
        p += 3;
        n++;
    }
    return n;
}

/* Nível 1..8 do glyph na posição idx (0-based). */
static int glyph_level(const char *s, int idx)
{
    return (int)((unsigned char)s[idx * 3 + 2]) - 0x80;
}

static void spark(const ring_t *r, int width, char *out, size_t cap)
{
    if ((size_t)width * 4 + 8 > cap) width = (int)(cap / 4) - 2;
    ring_sparkline(r, out, width);
}

/* ------------------------------------------------------------------ *
 * ring_push / ring_sparkline
 * ------------------------------------------------------------------ */

static void test_ring(void)
{
    ring_t r = {0};
    char out[HISTORY_LEN * 4 + 8];

    ring_sparkline(&r, out, 8);
    CHECK(out[0] == '\0', "ring vazio deve gerar string vazia");
    CHECK(r.count == 0 && r.head == 0, "ring vazio: count=%d head=%d", r.count, r.head);

    for (int i = 0; i < 8; i++) ring_push(&r, (double)i);
    CHECK(r.count == 8 && r.head == 8, "8 pushes: count=%d head=%d", r.count, r.head);

    spark(&r, 8, out, sizeof(out));
    CHECK(glyph_count(out) == 8, "largura 8 gerou %d glyphs", glyph_count(out));
    for (int i = 0; i < 8; i++)
        CHECK(glyph_level(out, i) == i + 1,
              "sequência crescente: glyph %d tem nível %d (esperado %d)",
              i, glyph_level(out, i), i + 1);

    /* Com largura 3, apenas os 3 valores mais recentes entram (5,6,7). */
    spark(&r, 3, out, sizeof(out));
    CHECK(glyph_count(out) == 3, "largura 3 gerou %d glyphs", glyph_count(out));
    CHECK(glyph_level(out, 0) == 1 && glyph_level(out, 1) == 4 &&
          glyph_level(out, 2) == 8,
          "últimos 3 valores devem ser ▁▄█ (níveis 1,4,8; obtidos %d,%d,%d)",
          glyph_level(out, 0), glyph_level(out, 1), glyph_level(out, 2));

    /* Wrap-around: HISTORY_LEN + 5 amostras. */
    for (int i = 8; i < HISTORY_LEN + 5; i++) ring_push(&r, (double)i);
    CHECK(r.count == HISTORY_LEN && r.head == 5,
          "wrap: count=%d head=%d", r.count, r.head);
    spark(&r, 8, out, sizeof(out));
    CHECK(glyph_count(out) == 8, "após wrap: %d glyphs", glyph_count(out));
    CHECK(glyph_level(out, 7) == 8, "amostra mais recente deve ser a maior");

    /* Valores constantes usam o nível médio (SPARK[3] = ▄ → byte 0x84). */
    ring_t c = {0};
    for (int i = 0; i < 8; i++) ring_push(&c, 42.0);
    spark(&c, 8, out, sizeof(out));
    CHECK(glyph_count(out) == 8 && glyph_level(out, 0) == 4,
          "amostras constantes devem gerar 8×▄ (nível 4)");
}

/* ------------------------------------------------------------------ *
 * pct_color, fmt_uptime, fmt_mb
 * ------------------------------------------------------------------ */

static void test_pct_color(void)
{
    /* CL_HEALTHY=82, CL_WARN=220, CL_CRIT=196 — cores estáveis. */
    CHECK(pct_color(0.0, 70.0, 90.0) == 82, "0%% deve ser saudável");
    CHECK(pct_color(69.999, 70.0, 90.0) == 82, "69.999%% deve ser saudável");
    CHECK(pct_color(70.0, 70.0, 90.0) == 220, "70%% deve ser aviso");
    CHECK(pct_color(89.999, 70.0, 90.0) == 220, "89.999%% deve ser aviso");
    CHECK(pct_color(90.0, 70.0, 90.0) == 196, "90%% deve ser crítico");
    CHECK(pct_color(150.0, 70.0, 90.0) == 196, "acima de 100%% deve ser crítico");
}

static void expect_uptime(unsigned long s, const char *want)
{
    char buf[32];
    fmt_uptime(s, buf, sizeof(buf));
    CHECK(strcmp(buf, want) == 0,
          "uptime %lu = '%s' (esperado '%s')", s, buf, want);
}

static void expect_mb(double mb, const char *want)
{
    char buf[32];
    fmt_mb(mb, buf, sizeof(buf));
    CHECK(strcmp(buf, want) == 0,
          "fmt_mb %.2f = '%s' (esperado '%s')", mb, buf, want);
}

static void test_fmt(void)
{
    expect_uptime(0UL, "0s");
    expect_uptime(59UL, "59s");
    expect_uptime(60UL, "1m00s");
    expect_uptime(3599UL, "59m59s");
    expect_uptime(3600UL, "1h00m00s");
    expect_uptime(3661UL, "1h01m01s");
    expect_uptime(90061UL, "25h01m01s");

    char small[4];
    fmt_uptime(3661UL, small, sizeof(small));
    CHECK(strlen(small) <= 3 && small[3] == '\0',
          "truncamento deve manter NUL dentro do buffer");

    expect_mb(0.0, "0MB");
    expect_mb(512.0, "512MB");
    expect_mb(1536.0, "1.50GB");
    expect_mb(2048.0, "2.00GB");
}

/* ------------------------------------------------------------------ *
 * proc_calc_cpu
 * ------------------------------------------------------------------ */

static void test_proc_cpu(void)
{
    cpusnap_t prev = {0};
    cpusnap_t cur = {0};

    /* Sem baseline não há delta: deve ser 0. */
    CHECK(proc_calc_cpu(&prev, &cur) == 0.0, "sem baseline deve retornar 0");

    prev.utime = 1000; prev.stime = 500;
    prev.ts.tv_sec = 100; prev.ts.tv_nsec = 0;
    cur.utime = 1100; cur.stime = 600;
    cur.ts.tv_sec = 101; cur.ts.tv_nsec = 0;

    double got = proc_calc_cpu(&prev, &cur);
    long clk = sysconf(_SC_CLK_TCK);
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    double want = 200.0 / (double)clk / 1.0 * 100.0 / (double)ncpu;
    if (want > 100.0) want = 100.0;
    CHECK(fabs(got - want) < 1e-9,
          "delta 200 ticks/1s: %.6f (esperado %.6f)", got, want);
    CHECK(got >= 0.0 && got <= 100.0, "pct fora de [0,100]: %.6f", got);

    /* Mais ticks no mesmo intervalo → maior percentual. */
    cpusnap_t fast = cur;
    fast.utime += 200;
    fast.stime += 200;
    CHECK(proc_calc_cpu(&prev, &fast) > got, "delta maior deve aumentar o pct");

    /* Tempo de parede não positivo → zero. */
    cpusnap_t same = cur;
    same.ts = prev.ts;
    CHECK(proc_calc_cpu(&prev, &same) == 0.0, "wall <= 0 deve retornar 0");

    /* Contadores retrógrados (PID reaproveitado): delta não pode dar wrap. */
    cpusnap_t bad = prev;
    bad.utime = 10; bad.stime = 10;
    double under = proc_calc_cpu(&prev, &bad);
    CHECK(under == 0.0, "delta retrógrado deve retornar 0: %.6f", under);

    /* Um contador sobe, mas o total (utime+stime) regrediu: também é 0. */
    cpusnap_t mixed = prev;
    mixed.utime = prev.utime + 100;
    mixed.stime = 1;
    CHECK(proc_calc_cpu(&prev, &mixed) == 0.0,
          "total retrógrado deve retornar 0 mesmo com utime maior");

    /* Saturação no teto. */
    cpusnap_t huge = cur;
    huge.utime += 100000000ULL;
    CHECK(proc_calc_cpu(&prev, &huge) == 100.0, "delta enorme deve saturar em 100");
}

/* ------------------------------------------------------------------ *
 * Parsers de /proc — correção P2 (subtarefa 11)
 * ------------------------------------------------------------------ */

static void test_proc_parsers(void)
{
    unsigned long long ut = 0, st = 0;

    CHECK(proc_parse_cpu_fields("S 1 2 3 4 5 6 7 8 9 10 123 456", &ut, &st) == 0,
          "stat completo deve parsear utime e stime");
    CHECK(ut == 123ULL && st == 456ULL,
          "utime/stime esperados 123/456, obtidos %llu/%llu", ut, st);

    ut = 777; st = 888;
    CHECK(proc_parse_cpu_fields("S 1 2 3 4 5 6 7 8 9 10 123", &ut, &st) != 0,
          "stat com apenas utime deve falhar");
    CHECK(ut == 777ULL && st == 888ULL,
          "parse parcial não pode alterar a saída (%llu/%llu)", ut, st);
    CHECK(proc_parse_cpu_fields("S 1 2 3 4 5 6 7 8 9 10", &ut, &st) != 0,
          "stat sem utime/stime deve falhar");
    CHECK(proc_parse_cpu_fields("(junk)", &ut, &st) != 0,
          "stat sem campos numéricos deve falhar");

    cpusnap_t snap = {0};
    CHECK(proc_read_cpu(getpid(), &snap) == 0,
          "leitura real de /proc/<pid>/stat deve ter sucesso");
    CHECK(snap.ts.tv_sec > 0, "timestamp monotonic deve ser preenchido");
    CHECK(proc_read_cpu(-1, &snap) != 0, "PID inexistente deve falhar");

    int lim = 4096;
    CHECK(proc_parse_fd_limit("Max open files 1048576 1048576 files", &lim) == 0,
          "limite de FDs comum deve parsear");
    CHECK(lim == 1048576, "limite esperado 1048576, obtido %d", lim);

    lim = 4096;
    CHECK(proc_parse_fd_limit("Max open files 5000000000 5000000000 files", &lim) == 0,
          "limite acima de INT_MAX deve parsear sem overflow");
    CHECK(lim == INT_MAX, "limite acima de INT_MAX deve saturar (obtido %d)", lim);

    lim = 4096;
    CHECK(proc_parse_fd_limit("Max open files -1 -1 files", &lim) == 0,
          "limite negativo deve parsear");
    CHECK(lim == 0, "limite negativo deve saturar em 0 (obtido %d)", lim);

    lim = 4096;
    CHECK(proc_parse_fd_limit("Max open files unlimited unlimited files", &lim) != 0,
          "limite unlimited deve falhar");
    CHECK(lim == 4096, "falha de parse não pode alterar o fallback (%d)", lim);

    int cnt = -1;
    lim = -1;
    proc_read_fd(getpid(), &cnt, &lim);
    CHECK(cnt > 0, "contagem real de FDs deve ser positiva (%d)", cnt);
    CHECK(lim > 0 && lim <= INT_MAX, "limite real de FDs fora de faixa: %d", lim);
}

/* ------------------------------------------------------------------ *
 * Leitura real de /proc/self/status (subtarefa 6)
 * ------------------------------------------------------------------ */

/* O caminho ainda é fixo em proc_read_mem_status() (/proc/%d/status); a
 * injeção de caminho e o uso de tests/fixtures/proc_status.txt ficam para a
 * subtarefa 15. Aqui a função REAL é chamada com o PID deste processo. */

/* /proc muda entre duas leituras (stdio e sscanf alocam páginas): 8 MB de
 * folga absorvem essa variação sem afetar a conferência dos valores. */
static int near_kb(unsigned long got, unsigned long want)
{
    unsigned long delta = got > want ? got - want : want - got;
    return delta <= 8UL * 1024UL;
}

static void test_status_fixture(void)
{
    char line[256];
    unsigned long rss = 0, vsz = 0, anon = 0, swap = 0, peak = 0;

    /* Oráculo independente: mesmo arquivo, sscanf separado da função sob
     * teste. Feito antes da chamada real para minimizar a janela de variação
     * dos contadores de /proc. */
    FILE *f = fopen("/proc/self/status", "r");
    CHECK(f != NULL, "/proc/self/status não pôde ser aberto");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            unsigned long v;
            if      (sscanf(line, "VmRSS:  %lu", &v) == 1) rss = v;
            else if (sscanf(line, "VmSize: %lu", &v) == 1) vsz = v;
            else if (sscanf(line, "RssAnon: %lu", &v) == 1) anon = v;
            else if (sscanf(line, "VmSwap: %lu", &v) == 1) swap = v;
            else if (sscanf(line, "VmPeak: %lu", &v) == 1) peak = v;
        }
        fclose(f);
    }

    mem_t mem = {0};
    int rc = proc_read_mem_status(getpid(), &mem);
    CHECK(rc == 0, "proc_read_mem_status(getpid) deve retornar 0, retornou %d", rc);
    CHECK(mem.rss_kb > 0, "rss_kb deve ser > 0 (obtido %lu)", mem.rss_kb);
    CHECK(mem.vsz_kb > 0, "vsz_kb deve ser > 0 (obtido %lu)", mem.vsz_kb);

    CHECK(near_kb(mem.rss_kb, rss), "rss_kb=%lu divergiu do sscanf=%lu", mem.rss_kb, rss);
    CHECK(near_kb(mem.vsz_kb, vsz), "vsz_kb=%lu divergiu do sscanf=%lu", mem.vsz_kb, vsz);
    CHECK(near_kb(mem.anon_kb, anon), "anon_kb=%lu divergiu do sscanf=%lu", mem.anon_kb, anon);
    CHECK(near_kb(mem.swap_kb, swap), "swap_kb=%lu divergiu do sscanf=%lu", mem.swap_kb, swap);
    CHECK(near_kb(mem.peak_kb, peak), "peak_kb=%lu divergiu do sscanf=%lu", mem.peak_kb, peak);
}

/* ------------------------------------------------------------------ *
 * Orçamento de linhas do layout (footer na última linha)
 * ------------------------------------------------------------------ */

typedef struct {
    int row;   /* 0-based */
    int col;
    int pos;   /* deslocamento no buffer capturado */
} move_t;

/* Captura o flush do render sem depender de globais do frame buffer. */
static int capture_dashboard(const jvm_t *j, char *buf, size_t cap)
{
    FILE *tmp = tmpfile();
    if (!tmp) return -1;
    int fd = fileno(tmp);
    int saved = dup(STDOUT_FILENO);
    if (fd < 0 || saved < 0) { fclose(tmp); if (saved >= 0) close(saved); return -1; }

    fflush(stdout);
    dup2(fd, STDOUT_FILENO);
    fb_reset();
    render_dashboard(j);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);

    fflush(tmp);
    rewind(tmp);
    size_t n = fread(buf, 1, cap - 1, tmp);
    buf[n] = '\0';
    fclose(tmp);
    return (int)n;
}

/* Reconhece apenas sequências ESC[<row>;<col>H. */
static int parse_moves(const char *s, move_t *moves, int cap)
{
    int n = 0;
    size_t len = strlen(s);
    for (size_t i = 0; i + 2 < len; i++) {
        if (s[i] != '\033' || s[i + 1] != '[') continue;
        size_t p = i + 2;
        int row = 0, col = 0, digits = 0;
        while (p < len && s[p] >= '0' && s[p] <= '9') {
            row = row * 10 + (s[p] - '0'); p++; digits++;
        }
        if (!digits || p >= len || s[p] != ';') continue;
        p++; digits = 0;
        while (p < len && s[p] >= '0' && s[p] <= '9') {
            col = col * 10 + (s[p] - '0'); p++; digits++;
        }
        if (!digits || p >= len || s[p] != 'H') continue;
        if (n < cap) {
            moves[n].row = row - 1;
            moves[n].col = col - 1;
            moves[n].pos = (int)i;
            n++;
        }
        i = p;
    }
    return n;
}

static void test_layout_budget(void)
{
    /* 240x80 incluído como pior caso amplo; 70x18/80x24 exercitam o P1 do doc 05. */
    const int sizes[][2] = {{70,18}, {80,24}, {110,30}, {200,50}, {240,80}};
    const int nalerts_cases[] = {0, 5};
    jvm_t base = {0};
    base.pid = 1;
    base.gc_pause_ms = 8.5;
    base.fd_count = 10;
    base.fd_limit = 1024;
    base.thr.total = 20;
    strcpy(base.gc_type, "G1");
    strcpy(base.cmdline, "java -jar app.jar");

    char buf[32768];
    move_t moves[1024];

    for (unsigned ac = 0; ac < sizeof(nalerts_cases) / sizeof(nalerts_cases[0]); ac++) {
        int nalerts = nalerts_cases[ac];
        jvm_t j = base;
        j.nalerts = nalerts;
        for (int i = 0; i < nalerts && i < MAX_ALERTS; i++) {
            j.alerts[i].sev = (i % 2 == 0) ? SEV_CRIT : SEV_WARN;
            j.alerts[i].kind = (al_kind_t)(AL_CPU + i);
            snprintf(j.alerts[i].msg, sizeof(j.alerts[i].msg),
                     "Alerta %d: valor acima do limiar", i + 1);
        }

        for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
            g_term_w = sizes[s][0];
            g_term_h = sizes[s][1];
            g_resize = 0;

            int len = capture_dashboard(&j, buf, sizeof(buf));
            int nm = parse_moves(buf, moves, 1024);
            CHECK(len > 0 && nm > 0,
                  "render %dx%d (nalerts=%d) não produziu frame", g_term_w, g_term_h, nalerts);

            int footer = g_term_h - 1;
            int footer_idx = -1;
            for (int i = 0; i < nm; i++)
                if (moves[i].row == footer) footer_idx = i;

            CHECK(footer_idx >= 0,
                  "footer ausente na linha %d (%dx%d, nalerts=%d)",
                  footer, g_term_w, g_term_h, nalerts);
            CHECK(nm > 0 && moves[nm - 1].row == footer,
                  "footer deve ser a última posição desenhada (%dx%d, nalerts=%d): última=%d",
                  g_term_w, g_term_h, nalerts, nm > 0 ? moves[nm - 1].row : -1);

            /* Painéis = todas as posições anteriores ao footer. */
            int max_panel = -1;
            for (int i = 0; i < footer_idx && i < nm; i++)
                if (moves[i].row > max_panel) max_panel = moves[i].row;

            /* P1 doc 05: painéis não podem invadir a linha do footer. */
            CHECK(max_panel < g_term_h - 1,
                  "painéis até a linha %d invadem footer em %dx%d (nalerts=%d)",
                  max_panel, g_term_w, g_term_h, nalerts);

            /* Cenários exigidos pelo revisor: 0 alertas => row <= 14;
             * 5 alertas => row <= 10. Detecta o estouro de 70x18/80x24
             * antes da correção 10. */
            int row_limit = (nalerts == 0) ? 14 : 10;
            CHECK(max_panel <= row_limit,
                  "painéis até a linha %d excedem limite %d em %dx%d (nalerts=%d)",
                  max_panel, row_limit, g_term_w, g_term_h, nalerts);

            CHECK(strstr(buf, "JVMA v6.0") != NULL,
                  "cabeçalho ausente no render %dx%d (nalerts=%d)",
                  g_term_w, g_term_h, nalerts);
        }
    }
}

/* ------------------------------------------------------------------ *
 * Export JSON: escape, round-trip, validade e decimal (subtarefa 7)
 * ------------------------------------------------------------------ */

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodifica uma string JSON simples. \u só é aceito para controladores,
 * que é o domínio produzido por json_escape(). */
static int json_get_string(const char *json, const char *key,
                           char *out, size_t cap)
{
    char needle[128];
    const char *p;
    size_t o = 0;

    if (cap == 0) return 0;
    snprintf(needle, sizeof(needle), "\"%s\": \"", key);
    p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);

    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p;

        if (c < 0x20) return 0;            /* byte de controle cru */
        if (c == '\\') {
            p++;
            if (!*p) return 0;
            switch (*p) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'n':  c = '\n'; break;
            case 't':  c = '\t'; break;
            case 'r':  c = '\r'; break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 'u': {
                int hi, lo;
                if (!p[1] || !p[2] || !p[3] || !p[4]) return 0;
                if (p[1] != '0' || p[2] != '0') return 0;
                hi = hex_digit((unsigned char)p[3]);
                lo = hex_digit((unsigned char)p[4]);
                if (hi < 0 || lo < 0) return 0;
                c = (unsigned char)((hi << 4) | lo);
                if (c >= 0x20) return 0;
                p += 4;
                break;
            }
            default: return 0;
            }
        }
        if (o + 1 >= cap) return 0;
        out[o++] = (char)c;
        p++;
    }
    if (*p != '"') return 0;
    out[o] = '\0';
    return 1;
}

static int json_basic_ok(const char *s, size_t len)
{
    size_t i = 0;

    while (i < len && isspace((unsigned char)s[i])) i++;
    if (i == len || s[i] != '{') return 0;
    while (len > i && isspace((unsigned char)s[len - 1])) len--;
    if (len == i || s[len - 1] != '}') return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 && c != '\n') return 0;
    }
    return 1;
}

static char *find_and_read_json(const char *dir, char *path, size_t cap)
{
    DIR *d = opendir(dir);
    struct dirent *de;
    int found = 0;

    path[0] = '\0';
    if (!d) return NULL;
    while ((de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        if (n >= 5 && strcmp(de->d_name + n - 5, ".json") == 0) {
            snprintf(path, cap, "%s/%s", dir, de->d_name);
            found = 1;
            break;
        }
    }
    closedir(d);
    if (!found) return NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || sz > 65536) { fclose(f); return NULL; }
    rewind(f);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    if (got != (size_t)sz && ferror(f)) { free(buf); fclose(f); return NULL; }
    buf[got] = '\0';
    fclose(f);
    return buf;
}

/* export_json não deve mais tocar no terminal: isolastdio e stdin para o
 * teste não poluir a saída nem mudar o modo do terminal. */
static void export_json_quiet(const jvm_t *j)
{
    int out_saved = dup(STDOUT_FILENO);
    int in_saved  = dup(STDIN_FILENO);
    int nullfd    = open("/dev/null", O_RDWR);

    if (out_saved >= 0 && in_saved >= 0 && nullfd >= 0) {
        fflush(stdout);
        dup2(nullfd, STDOUT_FILENO);
        dup2(nullfd, STDIN_FILENO);
        export_json(j);
        fflush(stdout);
        dup2(out_saved, STDOUT_FILENO);
        dup2(in_saved, STDIN_FILENO);
    } else {
        export_json(j);
    }
    if (nullfd >= 0) close(nullfd);
    if (out_saved >= 0) close(out_saved);
    if (in_saved >= 0) close(in_saved);
}

static void test_json_escape_direct(void)
{
    char full[64];
    char tight[8];

    json_escape("a\"b\\c\nd\te\rf\001g\037h", full, sizeof(full));
    CHECK(strcmp(full, "a\\\"b\\\\c\\nd\\te\\rf\\u0001g\\u001fh") == 0,
          "escape direto divergiu: '%s'", full);

    /* Sem espaço para o segundo escape: corta antes dele, nunca no meio. */
    json_escape("\001\"", tight, sizeof(tight));
    CHECK(strcmp(tight, "\\u0001") == 0,
          "truncamento partiu uma sequência de escape: '%s'", tight);

    json_escape("abc", NULL, 0); /* capacidade zero não deve escrever */
}

static void test_export_p0(void)
{
    char dir[] = "/tmp/jvma-export.XXXXXX";
    char saved_export[sizeof(g_export)];
    char saved_num[64];
    char path[512];
    char *json = NULL;
    const char *cur;
    static const char *candidates[] = {
        "pt_BR.UTF-8", "pt_BR.utf8", "pt_BR",
        "de_DE.UTF-8", "de_DE.utf8", "de_DE", NULL
    };
    const char *comma_locale = NULL;
    char want_num[32] = "";
    char comma_num[32] = "";

    snprintf(saved_export, sizeof(saved_export), "%s", g_export);
    cur = setlocale(LC_NUMERIC, NULL);
    snprintf(saved_num, sizeof(saved_num), "%s", cur ? cur : "C");

    CHECK(mkdtemp(dir) != NULL, "mkdtemp falhou: %s", strerror(errno));
    if (strstr(dir, "XXXXXX") != NULL) {
        snprintf(g_export, sizeof(g_export), "%s", saved_export);
        setlocale(LC_NUMERIC, saved_num);
        return;
    }
    snprintf(g_export, sizeof(g_export), "%s", dir);

    setlocale(LC_NUMERIC, "C");
    snprintf(want_num, sizeof(want_num), "%.2f", 12.34);
    for (int i = 0; candidates[i]; i++) {
        if (setlocale(LC_NUMERIC, candidates[i]) != NULL &&
            localeconv()->decimal_point[0] == ',') {
            comma_locale = candidates[i];
            break;
        }
    }
    if (comma_locale) snprintf(comma_num, sizeof(comma_num), "%.2f", 12.34);
    else              setlocale(LC_NUMERIC, "C");

    jvm_t j = {0};
    const char *cmd = "java -Xmx1g \"app\" \\ cfg\nnext\tcol\001ctl";
    const char *gc  = "G1\"\\GC";
    j.pid = 424242;
    j.cpu_pct = 12.34;
    j.gc_pause_ms = 8.5;
    j.uptime_s = 123;
    j.fd_count = 10;
    j.fd_limit = 1024;
    j.mem.rss_kb = 1024;
    j.thr.total = 20;
    snprintf(j.cmdline, sizeof(j.cmdline), "%s", cmd);
    snprintf(j.gc_type, sizeof(j.gc_type), "%s", gc);

    export_json_quiet(&j);
    json = find_and_read_json(dir, path, sizeof(path));
    CHECK(json != NULL, "export_json não gerou JSON legível em %s", dir);
    if (json) {
        char got_cmd[768];
        char got_gc[128];

        CHECK(json_basic_ok(json, strlen(json)), "JSON exportado é inválido");
        CHECK(json_get_string(json, "cmdline", got_cmd, sizeof(got_cmd)),
              "campo cmdline ausente ou mal escapado");
        CHECK(strcmp(got_cmd, cmd) == 0,
              "cmdline round-trip divergiu: '%s'", got_cmd);
        CHECK(json_get_string(json, "gc_type", got_gc, sizeof(got_gc)),
              "campo gc_type ausente ou mal escapado");
        CHECK(strcmp(got_gc, gc) == 0,
              "gc_type round-trip divergiu: '%s'", got_gc);
        CHECK(strstr(json,
              "  \"cmdline\": \"java -Xmx1g \\\"app\\\" \\\\ cfg\\nnext\\tcol\\u0001ctl\",\n") != NULL,
              "escape exato de cmdline ausente no JSON");
        CHECK(strstr(json, "  \"gc_type\": \"G1\\\"\\\\GC\",\n") != NULL,
              "escape exato de gc_type ausente no JSON");
        CHECK(strstr(json, "\"cpu_pct\": 12.34,") != NULL,
              "cpu_pct não usa ponto decimal sob locale %s", comma_locale ? comma_locale : "C");
        CHECK(strstr(json, "12,34") == NULL, "vírgula decimal vazou para o JSON");
        if (comma_locale) {
            CHECK(strchr(comma_num, ',') != NULL,
                  "locale %s não produziu vírgula decimal", comma_locale);
            CHECK(strcmp(want_num, "12.34") == 0,
                  "formatação de referência LC_NUMERIC=C divergiu: '%s'", want_num);
        }
        free(json);
        json = NULL;
    }

    /* Cmdline comum permanece byte-idêntica ao baseline sob LC_ALL=C. */
    setlocale(LC_NUMERIC, saved_num);
    if (path[0]) unlink(path);
    path[0] = '\0';
    memset(&j, 0, sizeof(j));
    j.pid = 424242;
    j.cpu_pct = 12.34;
    j.gc_pause_ms = 8.5;
    snprintf(j.cmdline, sizeof(j.cmdline), "java -jar app.jar");
    snprintf(j.gc_type, sizeof(j.gc_type), "G1");

    export_json_quiet(&j);
    json = find_and_read_json(dir, path, sizeof(path));
    CHECK(json != NULL, "export_json (cmdline comum) não gerou JSON");
    if (json) {
        CHECK(strstr(json, "  \"gc_type\": \"G1\",\n") != NULL,
              "gc_type comum divergiu do baseline");
        CHECK(strstr(json, "  \"cmdline\": \"java -jar app.jar\",\n") != NULL,
              "cmdline comum divergiu do baseline");
        CHECK(strstr(json, "  \"cpu_pct\": 12.34,\n") != NULL,
              "cpu_pct comum divergiu do baseline");
        free(json);
    }

    if (path[0]) unlink(path);
    rmdir(dir);
    snprintf(g_export, sizeof(g_export), "%s", saved_export);
    setlocale(LC_NUMERIC, saved_num);
}

/* ------------------------------------------------------------------ *
 * Notificação não bloqueante de export (subtarefa 8)
 * ------------------------------------------------------------------ */

static void test_notification(void)
{
    char saved_msg[sizeof(g_notify_msg)];
    time_t saved_at = g_notify_at;
    time_t saved_until = g_notify_until;
    int saved_w = g_term_w, saved_h = g_term_h, saved_resize = g_resize;
    jvm_t j = {0};
    char buf[32768];
    time_t now;

    snprintf(saved_msg, sizeof(saved_msg), "%s", g_notify_msg);
    j.pid = 1;
    j.gc_pause_ms = 8.5;
    strcpy(j.gc_type, "G1");
    strcpy(j.cmdline, "java -jar app.jar");

    g_term_w = 80;
    g_term_h = 24;
    g_resize = 0;

    notify_clear();
    now = time(NULL);
    CHECK(!notify_active(now), "notificação limpa não deve estar ativa");
    CHECK(g_notify_msg[0] == '\0' && g_notify_at == 0 && g_notify_until == 0,
          "notify_clear não zerou o estado da notificação");

    notify_set("JSON saved: %s", "/tmp/jvma-notify.json");
    now = time(NULL);
    CHECK(strcmp(g_notify_msg, "JSON saved: /tmp/jvma-notify.json") == 0,
          "mensagem de notificação divergiu: '%s'", g_notify_msg);
    CHECK(g_notify_at <= now && g_notify_until == g_notify_at + NOTIFY_TTL_SEC,
          "timestamp/TTL divergiram: at=%ld until=%ld",
          (long)g_notify_at, (long)g_notify_until);
    CHECK(notify_active(now), "notificação deve estar ativa após notify_set");
    CHECK(notify_active(g_notify_until - 1), "notificação deve estar ativa antes do TTL");
    CHECK(!notify_active(g_notify_until), "notificação deve expirar no instante do TTL");
    CHECK(!notify_active(g_notify_until + 1), "notificação deve expirar após o TTL");

    CHECK(capture_dashboard(&j, buf, sizeof(buf)) > 0,
          "render com notificação ativa falhou");
    CHECK(strstr(buf, "JSON saved: /tmp/jvma-notify.json") != NULL,
          "notificação ausente na linha de status");
    CHECK(strstr(buf, "\033[?25h") == NULL && strstr(buf, "\033[2J") == NULL,
          "notificação não deve restaurar/reiniciar o terminal");

    int saved_running = g_running;
    g_running = 1;
    CHECK(handle_key(&j, 'q') == 0 && g_running == 0,
          "q deve continuar saindo durante a notificação");
    g_running = saved_running;

    g_resize = 0;
    sig_handler(SIGWINCH);
    CHECK(g_resize == 1, "SIGWINCH deve continuar marcando resize durante a notificação");
    g_resize = saved_resize;

    notify_set("CSV appended: %s", "/tmp/jvma-notify.csv");
    CHECK(strcmp(g_notify_msg, "CSV appended: /tmp/jvma-notify.csv") == 0 &&
          notify_active(time(NULL)),
          "notify_set deve substituir a mensagem anterior");

    g_notify_until = time(NULL) - 1;
    CHECK(capture_dashboard(&j, buf, sizeof(buf)) > 0,
          "render com notificação expirada falhou");
    CHECK(strstr(buf, "CSV appended:") == NULL,
          "notificação expirada ainda é desenhada no footer");

    snprintf(g_notify_msg, sizeof(g_notify_msg), "%s", saved_msg);
    g_notify_at = saved_at;
    g_notify_until = saved_until;
    g_term_w = saved_w;
    g_term_h = saved_h;
    g_resize = saved_resize;
}

/* ------------------------------------------------------------------ *
 * Timing do loop de input (subtarefa 9)
 * ------------------------------------------------------------------ */

static void test_input_timing(void)
{
    struct timespec deadline, t0, t1;
    int saved_keys = g_keys_enabled;
    int saved_itvl = g_itvl_ms;
    jvm_t j = {0};
    int calls = 0;
    long long elapsed_ms;

    /* Deadline absoluto: o restante acompanha o relógio monotônico. */
    interval_deadline(&deadline, 60);
    int rem = deadline_remaining_ms(&deadline);
    CHECK(rem >= 58 && rem <= 60, "restante de 60ms divergiu: %d", rem);
    deadline.tv_sec -= 1;
    CHECK(deadline_remaining_ms(&deadline) == 0,
          "deadline passado deve retornar restante zero");

    /* Sem TTY/EOF, uma única espera cobre o intervalo inteiro: não pode
     * girar CPU com centenas de chamadas a poll() no mesmo intervalo. */
    g_keys_enabled = 0;
    interval_deadline(&deadline, 60);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (deadline_remaining_ms(&deadline) > 0) {
        (void)term_read_key(deadline_remaining_ms(&deadline));
        if (++calls > 8) break;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    elapsed_ms = ((long long)t1.tv_sec - t0.tv_sec) * 1000
               + ((long long)t1.tv_nsec - t0.tv_nsec) / 1000000;

    CHECK(calls <= 2, "espera sem TTY chamou term_read_key %d vezes (busy-wait)", calls);
    CHECK(elapsed_ms >= 50 && elapsed_ms < 500,
          "espera de 60ms durou %lldms", elapsed_ms);

    /* Controles de intervalo continuam nos limites 500..10000 ms. */
    g_itvl_ms = 2000;
    for (int i = 0; i < 10; i++) handle_key(&j, '+');
    CHECK(g_itvl_ms == 500, "'+' deve limitar a 500ms (obtido %d)", g_itvl_ms);
    for (int i = 0; i < 25; i++) handle_key(&j, '-');
    CHECK(g_itvl_ms == 10000, "'-' deve limitar a 10000ms (obtido %d)", g_itvl_ms);
    CHECK(handle_key(&j, 'p') == 1 && j.paused == 1 &&
          handle_key(&j, 'P') == 1 && j.paused == 0,
          "p/P deve alternar pausa");

    g_keys_enabled = saved_keys;
    g_itvl_ms = saved_itvl;
}

static void test_key_poll_hup(void)
{
    int pipefd[2];
    int saved_in = dup(STDIN_FILENO);
    int saved_keys = g_keys_enabled;

    CHECK(saved_in >= 0, "dup(STDIN_FILENO) falhou");
    if (saved_in < 0) return;

    if (pipe(pipefd) != 0) {
        CHECK(0, "pipe() falhou");
    } else {
        dup2(pipefd[0], STDIN_FILENO);
        g_keys_enabled = 1;

        CHECK(write(pipefd[1], "q", 1) == 1, "write no pipe falhou");
        CHECK(term_read_key(200) == 'q', "POLLIN deve devolver o byte lido");

        close(pipefd[1]);
        CHECK(term_read_key(200) == -1 && !g_keys_enabled,
              "POLLHUP/EOF deve desativar a leitura de teclas");
        CHECK(term_read_key(20) == -1, "após HUP não deve haver nova leitura");

        close(pipefd[0]);
        dup2(saved_in, STDIN_FILENO);
    }

    g_keys_enabled = saved_keys;
    close(saved_in);
}

/* ------------------------------------------------------------------ *
 * PID estrito e sinais explícitos (itens 7 e 8)
 * ------------------------------------------------------------------ */

static void test_pid_and_signals(void)
{
    const char *bad[] = {"", "abc", "1abc", "-1", "0", "1 ", "  1", "+1",
                          "999999999999999999999"};
    struct sigaction sa, old_term, old_int, old_winch;
    int saved_running = g_running, saved_resize = g_resize;
    int pid = -1;

    CHECK(parse_pid_arg("1", &pid) == 0 && pid == 1,
          "PID '1' deve ser aceito (obtido %d)", pid);
    pid = -1;
    CHECK(parse_pid_arg("2147483647", &pid) == 0 && pid == INT_MAX,
          "INT_MAX deve ser aceito (obtido %d)", pid);
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        pid = 42;
        CHECK(parse_pid_arg(bad[i], &pid) != 0 && pid == 42,
              "PID '%s' deve ser rejeitado sem alterar o output", bad[i]);
    }

    sigaction(SIGTERM, NULL, &old_term);
    sigaction(SIGINT, NULL, &old_int);
    sigaction(SIGWINCH, NULL, &old_winch);
    g_running = 1;
    g_resize = 0;
    signals_init();
    sigaction(SIGTERM, NULL, &sa);
    CHECK((sa.sa_flags & SA_RESTART) != 0,
          "SIGTERM deve ser instalado com SA_RESTART explícito");

    CHECK(raise(SIGTERM) == 0 && g_running == 0,
          "SIGTERM deve zerar g_running via sigaction");
    CHECK(raise(SIGWINCH) == 0 && g_resize == 1 && g_running == 0,
          "SIGWINCH deve marcar resize sem alterar g_running");

    g_running = saved_running;
    g_resize = saved_resize;
    sigaction(SIGTERM, &old_term, NULL);
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGWINCH, &old_winch, NULL);
}

/* O timer dispara SIGTERM enquanto render_help() espera uma tecla. */
static void test_alrm_term(int s)
{
    (void)s;
    sig_handler(SIGTERM);
}

static void test_help_signal(void)
{
    struct sigaction sa, old_alrm, old_term;
    struct itimerval timer;
    struct timespec t0, t1;
    int pfd[2], saved_in, saved_out, nullfd, timer_set, pipe_ok;
    int saved_running = g_running;
    long long ms;

    sigaction(SIGTERM, NULL, &old_term);
    signals_init();
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = test_alrm_term;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, &old_alrm);

    saved_in = dup(STDIN_FILENO);
    saved_out = dup(STDOUT_FILENO);
    CHECK(saved_in >= 0 && saved_out >= 0,
          "dup() falhou no teste de interrupção do help");
    if (saved_in < 0 || saved_out < 0) goto help_done;

    pipe_ok = (pipe(pfd) == 0);
    CHECK(pipe_ok, "pipe() falhou no teste de interrupção do help");
    if (!pipe_ok) goto help_restore_fd;

    nullfd = open("/dev/null", O_WRONLY);
    dup2(pfd[0], STDIN_FILENO);
    if (nullfd >= 0) dup2(nullfd, STDOUT_FILENO);

    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_usec = 20000; /* sinal em 20 ms, sem tecla disponível */
    g_running = 1;
    timer_set = (setitimer(ITIMER_REAL, &timer, NULL) == 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (timer_set) render_help();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ms = ((long long)t1.tv_sec - t0.tv_sec) * 1000
       + ((long long)t1.tv_nsec - t0.tv_nsec) / 1000000;

    memset(&timer, 0, sizeof(timer));
    setitimer(ITIMER_REAL, &timer, NULL);
    CHECK(timer_set, "setitimer() falhou no teste de interrupção do help");
    CHECK(!g_running, "SIGTERM durante help deve encerrar g_running");
    CHECK(ms < 300, "render_help levou %lldms para reagir ao SIGTERM", ms);

    if (nullfd >= 0) close(nullfd);
    dup2(saved_out, STDOUT_FILENO);
    dup2(saved_in, STDIN_FILENO);
    close(pfd[0]);
    close(pfd[1]);
help_restore_fd:
    close(saved_out);
    close(saved_in);
help_done:
    g_running = saved_running;
    sigaction(SIGALRM, &old_alrm, NULL);
    sigaction(SIGTERM, &old_term, NULL);
}

/* ------------------------------------------------------------------ *
 * Golden determinístico (LC_ALL=C imposto por tests/run_tests.sh)
 * ------------------------------------------------------------------ */

static void dump_golden(void)
{
    ring_t r = {0};
    char out[HISTORY_LEN * 4 + 8];

    spark(&r, 8, out, sizeof(out));
    printf("ring:empty=%s\n", out);
    for (int i = 0; i < 8; i++) ring_push(&r, (double)i);
    spark(&r, 8, out, sizeof(out));
    printf("ring:asc=%s\n", out);
    ring_t c = {0};
    for (int i = 0; i < 8; i++) ring_push(&c, 42.0);
    spark(&c, 8, out, sizeof(out));
    printf("ring:const=%s\n", out);

    char buf[32];
    const unsigned long ups[] = {0, 59, 60, 3599, 3600, 3661};
    for (unsigned i = 0; i < sizeof(ups) / sizeof(ups[0]); i++) {
        fmt_uptime(ups[i], buf, sizeof(buf));
        printf("uptime:%lu=%s\n", ups[i], buf);
    }
    const double mbs[] = {0.0, 512.0, 1536.0, 2048.0};
    for (unsigned i = 0; i < sizeof(mbs) / sizeof(mbs[0]); i++) {
        fmt_mb(mbs[i], buf, sizeof(buf));
        printf("mb:%.0f=%s\n", mbs[i], buf);
    }
    const double pcts[] = {0.0, 69.999, 70.0, 89.999, 90.0, 150.0};
    for (unsigned i = 0; i < sizeof(pcts) / sizeof(pcts[0]); i++)
        printf("pctcolor:%.3f=%d\n", pcts[i],
               pct_color(pcts[i], 70.0, 90.0));
}

/* ------------------------------------------------------------------ *
 * GC simulado: padrão ligado e opt-out CLI/variável (subtarefa 12)
 * ------------------------------------------------------------------ */

static void test_gc_sim_toggle(void)
{
    int saved_flag = g_no_sim_gc;
    jvm_t j = {0};
    char *no_flag[]   = {(char *)"jvma", (char *)"123", NULL};
    char *cli_first[] = {(char *)"jvma", (char *)"--no-sim-gc", (char *)"123", NULL};
    char *cli_last[]  = {(char *)"jvma", (char *)"123", (char *)"/tmp",
                         (char *)"--no-sim-gc", NULL};
    int found, est;

    /* Padrão: random walk executado; valor muito negativo força o
     * reinício em 2..16 independentemente do delta de rand(). */
    g_no_sim_gc = 0;
    j.gc_pause_ms = -100.0;
    gc_sim_step(&j);
    CHECK(j.gc_pause_ms >= 2.0 && j.gc_pause_ms <= 16.0,
          "padrão deve executar random walk (obtido %.1f)", j.gc_pause_ms);

    j.gc_pause_ms = 60.0;
    eval_alerts(&j);
    found = est = 0;
    for (int i = 0; i < j.nalerts; i++) {
        if (j.alerts[i].kind == AL_GC) {
            found++;
            if (strstr(j.alerts[i].msg, "(est)") != NULL) est++;
        }
    }
    CHECK(found == 1, "padrão deve gerar 1 alerta AL_GC (obtido %d)", found);
    CHECK(est == 1, "mensagem de alerta GC deve trazer o sufixo (est)");

    /* Flag: gc_pause_ms = 0.0 e nenhum AL_GC, mesmo com valor alto no estado. */
    g_no_sim_gc = 1;
    j.gc_pause_ms = 250.0;
    gc_sim_step(&j);
    CHECK(j.gc_pause_ms == 0.0,
          "com flag gc_pause_ms deve ser 0.0 (obtido %.1f)", j.gc_pause_ms);
    j.gc_pause_ms = 250.0; /* força o limiar para provar a supressão no eval */
    eval_alerts(&j);
    found = 0;
    for (int i = 0; i < j.nalerts; i++)
        if (j.alerts[i].kind == AL_GC) found++;
    CHECK(found == 0, "com flag não deve haver alerta AL_GC (obtido %d)", found);

    /* CLI em qualquer posição; variável apenas com o valor exato "1". */
    unsetenv("JVMA_NO_SIM_GC");
    CHECK(gc_sim_disabled(3, no_flag) == 0, "sem flag/env: simulação ligada");
    CHECK(gc_sim_disabled(3, cli_first) == 1, "flag no início do argv deve desligar");
    CHECK(gc_sim_disabled(4, cli_last) == 1, "flag no fim do argv deve desligar");

    setenv("JVMA_NO_SIM_GC", "", 1);
    CHECK(gc_sim_disabled(3, no_flag) == 0, "env vazio não deve desligar");
    setenv("JVMA_NO_SIM_GC", "0", 1);
    CHECK(gc_sim_disabled(3, no_flag) == 0, "env '0' não deve desligar");
    setenv("JVMA_NO_SIM_GC", "true", 1);
    CHECK(gc_sim_disabled(3, no_flag) == 0, "env outro texto não deve desligar");
    setenv("JVMA_NO_SIM_GC", "1", 1);
    CHECK(gc_sim_disabled(3, no_flag) == 1, "env '1' deve desligar");
    CHECK(gc_sim_disabled(3, cli_first) == 1, "CLI prevalece com env '1'");
    setenv("JVMA_NO_SIM_GC", "", 1);
    CHECK(gc_sim_disabled(3, cli_first) == 1, "CLI prevalece com env vazio");
    unsetenv("JVMA_NO_SIM_GC");

    g_no_sim_gc = saved_flag;
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "C");
    if (argc == 2 && strcmp(argv[1], "--dump") == 0) {
        dump_golden();
        return 0;
    }

    test_ring();
    test_pct_color();
    test_fmt();
    test_proc_cpu();
    test_proc_parsers();
    test_status_fixture();
    test_json_escape_direct();
    test_export_p0();
    test_notification();
    test_input_timing();
    test_key_poll_hup();
    test_pid_and_signals();
    test_help_signal();
    test_layout_budget();
    test_gc_sim_toggle();

    printf("unit: %d checks, %d failures\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
