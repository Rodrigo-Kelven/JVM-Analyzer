#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "jvma_types.h"
#include "alerts.h"
#include "discovery.h"
#include "export.h"
#include "gc_sim.h"
#include "proc.h"
#include "render.h"
#include "sample.h"
#include "term.h"
#include "util.h"

/* =========================================================================
 * GLOBALS
 * ========================================================================= */
static jvm_t g_jvm     = {0};
static int   g_itvl_ms = DEFAULT_ITVL_MS;

/* Notificação de export via callback injetada (desacopla export do term). */
static void export_notify_cb(const char *msg)
{
    notify_set("%s", msg);
}

/* =========================================================================
 * INPUT HANDLER
 * ========================================================================= */
static int handle_key(jvm_t *j, int *itvl_ms, int key) {
    switch (key) {
    case 'q': case 'Q': term_request_quit(); return 0;
    case 'p': case 'P': j->paused = !j->paused; break;
    case 'e': case 'E': export_json(j); render_dashboard(j, itvl_ms); break;
    case 'c': case 'C': export_csv(j); render_dashboard(j, itvl_ms); break;
    case 'h': case 'H': render_help(); break;
    case '+': case '=':
        *itvl_ms -= 500; if (*itvl_ms < 500) *itvl_ms = 500; break;
    case '-': case '_':
        *itvl_ms += 500; if (*itvl_ms > 10000) *itvl_ms = 10000; break;
    }
    return 1;
}


/* =========================================================================
 * MAIN
 * ========================================================================= */
int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    /* decimal com ponto em JSON/CSV; hora e demais categorias seguem o ambiente */
    term_signals_init();
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    gc_sim_set_disabled(gc_sim_disabled(argc, argv));
    export_set_notify(export_notify_cb);

    int pid = 0;
    int ipid = 0, idir = 0; /* argumentos posicionais, ignorando a flag */
    for (int i = 1; i < argc; i++) {
        if (argv[i] != NULL && strcmp(argv[i], "--no-sim-gc") == 0) continue;
        if (ipid == 0) ipid = i;
        else if (idir == 0) idir = i;
    }
    if (ipid != 0 && parse_pid_arg(argv[ipid], &pid) != 0) {
        fprintf(stderr, "JVMA: invalid PID '%s'\n", argv[ipid]);
        return 1;
    }
    if (idir != 0) strncpy(g_export, argv[idir], sizeof(g_export)-1);

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
        if (pid <= 0) { term_printf("  Cancelled.\n"); return 0; }
        term_printf("  Attaching to PID %d...\n", pid);
        usleep(400000);
    }

    /* Validate process */
    char check[256];
    proc_path(pid, NULL, check, sizeof(check));
    struct stat st;
    if (stat(check, &st) != 0) {
        fprintf(stderr, "JVMA: Process %d not found.\n", pid);
        return 1;
    }

    /* Initialize state */
    memset(&g_jvm, 0, sizeof(g_jvm));
    notify_clear();
    g_jvm.pid = pid;
    g_jvm.first_sample = 1;
    g_jvm.gc_pause_ms = gc_sim_enabled() ? 8.5 : 0.0;
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

    while (term_should_run()) {
        if (!g_jvm.paused) {
            if (sample_update(&g_jvm) != 0) {
                term_restore();
                fprintf(stderr, "\nJVMA: Process %d exited.\n", pid);
                return 0;
            }
        }

        render_dashboard(&g_jvm, &g_itvl_ms);

        struct timespec deadline;
        interval_deadline(&deadline, g_itvl_ms);
        while (term_should_run()) {
            int remaining = deadline_remaining_ms(&deadline);
            if (remaining <= 0) break;

            int itvl_before = g_itvl_ms;
            int key = term_read_key(remaining);
            if (key >= 0) {
                if (!handle_key(&g_jvm, &g_itvl_ms, key)) goto done;
                if (g_itvl_ms != itvl_before)
                    interval_deadline(&deadline, g_itvl_ms);
            }
            if (term_consume_resize()) {
                term_get_size();
                render_dashboard(&g_jvm, &g_itvl_ms);
            }
        }
    }

done:
    term_restore();
    term_printf("JVMA v%s exited cleanly.\n", JVMA_VERSION);
    return 0;
}
