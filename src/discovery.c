#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "discovery.h"
#include "proc.h"
#include "term.h"

int discover_jvms(jvm_entry_t *out, int max) {
    char root[256];
    proc_path(-1, NULL, root, sizeof(root));
    DIR *proc = opendir(root);
    if (!proc) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(proc)) != NULL && n < max) {
        if (!isdigit((unsigned char)e->d_name[0])) continue;
        int pid = atoi(e->d_name);
        char exe_path[256], target[512] = {0};
        proc_path(pid, "exe", exe_path, sizeof(exe_path));
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

int show_discovery_menu(const jvm_entry_t *list, int n) {
    term_printf("\n  JVMA v%s — JVM Process Discovery\n", JVMA_VERSION);
    term_printf("  %.*s\n", 74, "──────────────────────────────────────────────────────────────────────────");
    term_printf("  %-3s  %-7s  %-7s  %-14s  %s\n", "#", "PID", "THREADS", "GC", "COMMAND");
    term_printf("  %.*s\n", 74, "──────────────────────────────────────────────────────────────────────────");
    for (int i = 0; i < n; i++) {
        char cmd[48]; strncpy(cmd, list[i].cmdline, 47); cmd[47] = '\0';
        term_printf("  %-3d  %-7d  %-7d  %-14s  %s\n",
                    i+1, list[i].pid, list[i].nthr, list[i].gc_type, cmd);
    }
    term_printf("  %.*s\n", 74, "──────────────────────────────────────────────────────────────────────────");
    if (n == 1) { term_printf("  Auto-selecting PID %d\n", list[0].pid); return list[0].pid; }
    term_printf("  Select [1-%d] or press Enter to cancel: ", n);
    term_flush_stdout();
    char buf[16] = {0};
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    int sel = atoi(buf);
    if (sel < 1 || sel > n) return -1;
    return list[sel-1].pid;
}
