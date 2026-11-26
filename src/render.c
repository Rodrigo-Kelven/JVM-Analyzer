#include <stdio.h>
#include <string.h>
#include <time.h>

#include "render.h"
#include "ring.h"
#include "term.h"
#include "util.h"

/* =========================================================================
 * DRAWING HELPERS
 * ========================================================================= */

/* Draw progress bar at (row, col).
   Bar visual width = bw chars (fill region).
   Full widget width = bw + 10  ([ + fill + ] + space + 6-char pct). */
void draw_bar(int row, int col, int bw,
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
void draw_hline(int row, int col, int len) {
    fb_move(row, col);
    fb_fg(CL_BORDER);
    for (int i = 0; i < len; i++) fb_write("─");
    fb_reset_attr();
}

/* Draw panel header: ╭── TITLE ────────────╮ */
void draw_panel_hdr(int row, int col, int w, const char *title) {
    if (w < 8) {
        /* largura mínima segura: bordos fixos "╭──  ╮" ocupam 6 colunas */
        if (w >= 2) {
            fb_move(row, col);
            fb_fg(CL_BORDER); fb_bold();
            fb_write("╭");
            int inner = w - 2;
            if (inner < 0) inner = 0;
            for (int i = 0; i < inner; i++) fb_write("─");
            fb_write("╮");
            fb_reset_attr();
        }
        return;
    }
    int tlen = (int)strlen(title);
    if (tlen < 0) tlen = 0;
    if (tlen > w - 8) tlen = w - 8;
    if (tlen < 0) tlen = 0;
    fb_move(row, col);
    fb_fg(CL_BORDER); fb_bold();
    fb_write("╭──");
    fb_reset_attr();
    fb_fg(CL_BORDER); fb_write(" ");
    fb_fg(CL_ACCENT); fb_bold(); fb_printf("%.*s", tlen, title);
    fb_reset_attr();
    fb_fg(CL_BORDER); fb_write(" ");
    int remaining = w - 6 - tlen;
    if (remaining < 0) remaining = 0;
    for (int i = 0; i < remaining; i++) fb_write("─");
    fb_write("╮");
    fb_reset_attr();
}

/* Draw panel footer: ╰───────────────────╯ */
void draw_panel_ftr(int row, int col, int w) {
    if (w < 2) return;
    fb_move(row, col);
    fb_fg(CL_BORDER);
    fb_write("╰");
    int inner = w - 2;
    if (inner < 0) inner = 0;
    for (int i = 0; i < inner; i++) fb_write("─");
    fb_write("╯");
    fb_reset_attr();
}

/* Draw vertical border char on left and right side of panel */
void draw_sides(int row, int col, int w) {
    if (w < 2) return;
    fb_move(row, col);      fb_fg(CL_BORDER); fb_write("│"); fb_reset_attr();
    fb_move(row, col+w-1);  fb_fg(CL_BORDER); fb_write("│"); fb_reset_attr();
}

/* =========================================================================
 * DASHBOARD PANELS — each returns the next free row
 * ========================================================================= */

int panel_header_bar(const jvm_t *j, int row) {
    time_t now = time(NULL);
    char ts[24], up[20];
    strftime(ts, sizeof(ts), "%H:%M:%S", localtime(&now));
    fmt_uptime(j->uptime_s, up, sizeof(up));

    /* Full-width title bar */
    fb_move(row, 0);
    fb_fg(CL_HDR_FG); fb_bg(CL_HDR_BG); fb_bold();
    /* Pad entire line */
    for (int c = 0; c < term_width(); c++) fb_write(" ");
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
                        "  JVMA v%s   PID:%-6d   %-15s  UP:%-12s  %s  ",
                        JVMA_VERSION, j->pid, j->gc_type, up, ts);
    if (hlen > term_width()) {
        /* Mesma informação sem o padding fixo de 71 colunas. */
        char pidbuf[16];
        int pid_len = snprintf(pidbuf, sizeof(pidbuf), "%d", j->pid);
        if (pid_len < 0) pid_len = 0;
        int overhead = 36 + (pid_len > 6 ? pid_len - 6 : 0);
        int room = term_width() - overhead;
        if (room < 2) room = 2;
        int gc_cap = room / 2;
        int up_cap = room - gc_cap;
        hlen = snprintf(hdr, sizeof(hdr),
                        " JVMA v%s PID:%-6d %.*s UP:%.*s %.*s ",
                        JVMA_VERSION, j->pid,
                        gc_cap, j->gc_type, up_cap, up, 8, ts);
    }
    int hx = (term_width() - hlen) / 2;
    if (hx < 0) hx = 0;
    fb_move(row, hx);
    fb_write(hdr);
    fb_reset_attr();
    row++;

    /* CMD line (dimmed) */
    fb_move(row, 0);
    fb_fg(CL_DIM);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), " CMD: %.*s", term_width() - 8, j->cmdline);
    fb_write(cmd);
    fb_eol();
    fb_reset_attr();
    return row + 1;
}

int panel_cpu(const jvm_t *j, int row, int col, int w) {
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

int panel_memory(const jvm_t *j, int row, int col, int w) {
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

int panel_threads(const jvm_t *j, int row, int col, int w) {
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

int panel_gc_fd(const jvm_t *j, int row, int col, int w) {
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
        char fdlab[32];
        snprintf(fdlab, sizeof(fdlab), "%5d/%-5d", j->fd_count, j->fd_limit);
        int avail = w - (11 + bw + 10) - 1; /* até a borda direita */
        if (avail < 0) avail = 0;
        if (avail > (int)sizeof(fdlab) - 1) avail = (int)sizeof(fdlab) - 1;
        if ((int)strlen(fdlab) > avail)
            snprintf(fdlab, sizeof(fdlab), "%d/%d", j->fd_count, j->fd_limit);
        if ((int)strlen(fdlab) > avail) fdlab[avail] = '\0';
        fb_cprintf(row, col+11+bw+10, CL_VALUE, 0, "%s", fdlab);
    } else {
        fb_ctext(row, col+11, CL_DIM, 0, "(access denied — run as same user)");
    }
    row++;

    draw_panel_ftr(row, col, w);
    return row + 1;
}

int panel_alerts(const jvm_t *j, int row, int col, int w) {
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

void draw_footer(const jvm_t *j, const int *itvl_ms) {
    /* Mesmo fb_move para as duas variantes: a linha de status nunca ganha
     * uma segunda posição enquanto a notificação está ativa. */
    fb_move(term_height()-1, 0);
    if (notify_active(time(NULL))) {
        int avail = term_width() - 1;
        fb_fg(CL_OK); fb_bold();
        if (avail > 0) fb_printf("%.*s", avail, notify_message());
    } else {
        fb_fg(CL_DIM);
        fb_printf(" [Q]Quit  [P]%s  [E]JSON  [C]CSV  [H]Help  [+/-]Speed  Int:%ds",
                  j->paused ? "Resume" : "Pause", *itvl_ms/1000);
    }
    fb_eol();
    fb_reset_attr();

    if (j->paused) {
        fb_move(term_height()-1, term_width()-12);
        fb_fg(CL_WARN); fb_bold();
        fb_write(" [PAUSED] ");
        fb_reset_attr();
    }
}

/* =========================================================================
 * HELP SCREEN
 * ========================================================================= */
void render_help(void) {
    const int h = 25, w = 64;
    int y0 = (term_height() - h) / 2, x0 = (term_width() - w) / 2;
    if (y0 < 0) y0 = 0;
    if (x0 < 0) x0 = 0;

    /* Background fill */
    for (int r = y0; r < y0+h && r < term_height(); r++) {
        fb_move(r, x0); fb_fg(CL_DIM);
        for (int c = 0; c < w; c++) fb_write(" ");
    }
    fb_reset_attr();

    draw_panel_hdr(y0, x0, w, "JVMA v" JVMA_VERSION " \xe2\x80\x94 Keyboard Reference");
    draw_panel_ftr(y0+h-1, x0, w);
    for (int r = y0+1; r < y0+h-1 && r < term_height(); r++) draw_sides(r, x0, w);
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
#define TH(n,wfmt,wval,cfmt,cval) do { \
    char wb[16], cb[16]; \
    snprintf(wb, sizeof(wb), wfmt, (wval)); \
    snprintf(cb, sizeof(cb), cfmt, (cval)); \
    fb_cprintf(r,x0+5,CL_LABEL,0,"%-14s",n); \
    fb_cprintf(r,x0+20,CL_WARN,1,"warn %-10s",wb); \
    fb_cprintf(r,x0+34,CL_CRIT,1,"crit %s",cb); r++; \
} while(0)
    /* Mesmas constantes usadas por src/alerts.c — nenhum valor duplicado. */
    TH("CPU",       "%.0f%%", TH_CPU_W,    "%.0f%%",  TH_CPU_C);
    TH("GC Pause",  "%.0fms", TH_GC_W_MS,  "%.0fms",  TH_GC_C_MS);
    TH("Threads",   "%d",     TH_THR_W,    "%d",      TH_THR_C);
    TH("Zombies",   "%d",     TH_ZMB_W,    "%d",      TH_ZMB_C);
    TH("Open FDs",  "%d",     TH_FD_W,     "%d",      TH_FD_C);
    r++;
    fb_cprintf(r, x0+3, CL_DIM, 0,
               "Heap~ = heuristic via /proc/smaps (no JVM agent)"); r++;
    fb_cprintf(r, x0+3, CL_ACCENT, 1,
               "           Press any key to close");

    fb_flush();
    term_read_key_blocking();
#undef TH
#undef HL
}

/* =========================================================================
 * MASTER RENDER
 * ========================================================================= */
int panel_h_cpu(void)    { return 4; }
int panel_h_mem(void)    { return 8; }
int panel_h_thr(void)    { return 7; }
int panel_h_gcfd(void)   { return 4; }
int panel_h_alerts(const jvm_t *j) { return (j->nalerts == 0) ? 3 : 2 + (j->nalerts < 5 ? j->nalerts : 5); }

void draw_trunc_notice(int row, int col, int w) {
    if (row >= term_height() - 1) return;
    draw_sides(row, col, w);
    fb_move(row, col + 2); fb_fg(CL_WARN); fb_bold();
    fb_printf("%-*.*s", w - 4, w - 4, "Layout truncado: aumente a altura do terminal");
    fb_reset_attr();
    row++;
    if (row < term_height() - 1) {
        draw_panel_ftr(row, col, w);
    }
}

void render_dashboard(const jvm_t *j, const int *itvl_ms) {
    if (term_consume_resize()) term_get_size();

    int w = term_width(), h = term_height();
    if (w < 70 || h < 18) {
        fb_reset();
        fb_move(0, 0); fb_fg(CL_CRIT); fb_bold();
        fb_printf("Terminal too small! Need 70x18 (got %dx%d)", w, h);
        fb_reset_attr(); fb_eol();
        fb_flush();
        return;
    }

    fb_reset();
    /* Move to top-left without full clear (avoids flicker) */
    fb_write("\033[H");

    int row = panel_header_bar(j, 0);
    int footer_row = h - 1;
    int alerts_h = panel_h_alerts(j);
    /* orçamento: footer sempre na última linha; painéis não invadem footer.
     * row_limit atende orçamento estrito do layout (0 alertas <=14, 5 alertas <=10). */
    int row_limit = (j->nalerts == 0) ? 14 : 10;
    int content_max = footer_row - 1;
    if (content_max > row_limit) content_max = row_limit;
    /* ALERTS tem prioridade: reserva suas linhas antes de aceitar painéis. */
    int limit = content_max - alerts_h + 1;

    if (w >= 110) {
        /* Two-column layout */
        int lw = w / 2;
        int rw = w - lw - 1;
        int rc = lw + 1;
        int lr = row, rr = row;

        if (lr + panel_h_cpu() <= limit) lr = panel_cpu(j, lr, 0, lw);
        else { draw_trunc_notice(lr, 0, lw); lr = limit; }
        if (lr + panel_h_thr() <= limit) lr = panel_threads(j, lr, 0, lw);
        if (lr + panel_h_gcfd() <= limit) lr = panel_gc_fd(j, lr, 0, lw);

        if (rr + panel_h_mem() <= limit) rr = panel_memory(j, rr, rc, rw);

        row = lr > rr ? lr : rr;
    } else {
        /* ordem: cpu, mem, threads, gc_fd - desenha só os que cabem */
        if (row + panel_h_cpu() <= limit) row = panel_cpu(j, row, 0, w);
        else { draw_trunc_notice(row, 0, w); row = limit; }
        if (row + panel_h_mem() <= limit) row = panel_memory(j, row, 0, w);
        if (row + panel_h_thr() <= limit) row = panel_threads(j, row, 0, w);
        if (row + panel_h_gcfd() <= limit) row = panel_gc_fd(j, row, 0, w);
    }

    if (row + alerts_h - 1 <= content_max)
        panel_alerts(j, row, 0, w);

    draw_footer(j, itvl_ms);
    fb_flush();
}
