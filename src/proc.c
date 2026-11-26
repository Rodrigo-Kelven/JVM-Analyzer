#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "jvma_types.h"
#include "proc.h"

#define PROC_PATH_MAX 512

/* =========================================================================
 * CAMINHOS (raiz configurável; padrão "/proc")
 * ========================================================================= */
static char g_root[PROC_PATH_MAX] = "/proc";

typedef struct {
    const char *leaf;
    char        path[PROC_PATH_MAX];
} proc_override_t;

/* Arquivos que podem ser injetados diretamente (ex.: fixture de status). */
static proc_override_t g_overrides[] = {
    {"stat",          ""},
    {"status",        ""},
    {"smaps_rollup",  ""},
    {"smaps",         ""},
    {"limits",        ""},
    {"cmdline",       ""},
    {"meminfo",       ""},
    {"uptime",        ""}
};

void proc_set_root(const char *root)
{
    const char *src = (root == NULL || root[0] == '\0') ? "/proc" : root;
    snprintf(g_root, sizeof(g_root), "%s", src);
}

void proc_set_path(const char *leaf, const char *path)
{
    if (leaf == NULL) return;
    for (size_t i = 0; i < sizeof(g_overrides) / sizeof(g_overrides[0]); i++) {
        if (strcmp(g_overrides[i].leaf, leaf) != 0) continue;
        snprintf(g_overrides[i].path, sizeof(g_overrides[i].path),
                 "%s", path ? path : "");
        return;
    }
}

void proc_path(int pid, const char *leaf, char *out, size_t cap)
{
    if (cap == 0) return;
    if (leaf != NULL) {
        for (size_t i = 0; i < sizeof(g_overrides) / sizeof(g_overrides[0]); i++) {
            if (strcmp(g_overrides[i].leaf, leaf) != 0) continue;
            if (g_overrides[i].path[0] != '\0') {
                snprintf(out, cap, "%s", g_overrides[i].path);
                return;
            }
            break;
        }
    }
    if (pid < 0) {
        if (leaf != NULL) snprintf(out, cap, "%s/%s", g_root, leaf);
        else              snprintf(out, cap, "%s", g_root);
    } else {
        if (leaf != NULL) snprintf(out, cap, "%s/%d/%s", g_root, pid, leaf);
        else              snprintf(out, cap, "%s/%d", g_root, pid);
    }
}

/* =========================================================================
 * /PROC READERS
 * ========================================================================= */
int proc_parse_cpu_fields(const char *p, unsigned long long *ut,
                          unsigned long long *st)
{
    unsigned long long utmp = 0, stmp = 0;
    /* state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime */
    int n = sscanf(p, "%*c %*d %*d %*d %*d %*d %*u "
                       "%*u %*u %*u %*u "
                       "%llu %llu", &utmp, &stmp);
    if (n != 2) return -1; /* nunca aceitar valores parciais */
    *ut = utmp;
    *st = stmp;
    return 0;
}

int proc_read_cpu(int pid, cpusnap_t *out) {
    char path[PROC_PATH_MAX], buf[2048];
    proc_path(pid, "stat", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int ok = (fgets(buf, sizeof(buf), f) != NULL);
    fclose(f);
    if (!ok) return -1;
    char *p = strrchr(buf, ')');
    if (!p) return -1;
    p += 2;
    unsigned long long ut = 0, st = 0;
    if (proc_parse_cpu_fields(p, &ut, &st) != 0) return -1;
    out->utime = ut; out->stime = st;
    clock_gettime(CLOCK_MONOTONIC, &out->ts);
    return 0;
}

double proc_calc_cpu(const cpusnap_t *prev, const cpusnap_t *cur) {
    if (prev->ts.tv_sec == 0) return 0.0;
    /* PID reaproveitado ou leitura racy pode reiniciar os contadores. */
    if (prev->utime > ULLONG_MAX - prev->stime ||
        cur->utime  > ULLONG_MAX - cur->stime) return 0.0;
    unsigned long long prev_total = prev->utime + prev->stime;
    unsigned long long cur_total  = cur->utime + cur->stime;
    if (cur_total < prev_total) return 0.0;
    unsigned long long delta = cur_total - prev_total;
    double wall = (cur->ts.tv_sec  - prev->ts.tv_sec)
                + (cur->ts.tv_nsec - prev->ts.tv_nsec) / 1e9;
    if (wall <= 0.0) return 0.0;
    long clk  = sysconf(_SC_CLK_TCK);
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    double pct = ((double)delta / (double)clk / wall) * 100.0 / (double)ncpu;
    return pct < 0.0 ? 0.0 : (pct > 100.0 ? 100.0 : pct);
}

int proc_read_mem_status(int pid, mem_t *m) {
    char path[PROC_PATH_MAX], line[256];
    proc_path(pid, "status", path, sizeof(path));
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

void proc_read_smaps_rollup(int pid, mem_t *m) {
    char path[PROC_PATH_MAX], line[256];
    proc_path(pid, "smaps_rollup", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        unsigned long v;
        if      (sscanf(line, "Pss: %lu",       &v) == 1) m->pss_kb  = v;
        else if (sscanf(line, "Anonymous: %lu", &v) == 1) m->anon_kb = v;
    }
    fclose(f);
}

unsigned long proc_estimate_heap(int pid) {
    char path[PROC_PATH_MAX], line[512];
    proc_path(pid, "smaps", path, sizeof(path));
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

void proc_read_memtotal(mem_t *m) {
    char path[PROC_PATH_MAX];
    proc_path(-1, "meminfo", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) { m->mem_total_kb = 8UL * 1024 * 1024; return; }
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        unsigned long v;
        if (sscanf(line, "MemTotal: %lu", &v) == 1) { m->mem_total_kb = v; break; }
    }
    fclose(f);
    if (!m->mem_total_kb) m->mem_total_kb = 8UL * 1024 * 1024;
}

int proc_parse_fd_limit(const char *line, int *lim) {
    long long soft = 0;
    if (sscanf(line, "%*s %*s %*s %lld", &soft) != 1) return -1;
    if (soft < 0) soft = 0;
    if (soft > (long long)INT_MAX) soft = (long long)INT_MAX;
    *lim = (int)soft;
    return 0;
}

void proc_read_fd(int pid, int *cnt, int *lim) {
    char path[PROC_PATH_MAX];
    proc_path(pid, "fd", path, sizeof(path));
    DIR *d = opendir(path);
    if (!d) { *cnt = -1; *lim = -1; return; }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (isdigit((unsigned char)e->d_name[0])) n++;
    closedir(d);
    *cnt = n; *lim = 4096;
    proc_path(pid, "limits", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "open files")) {
            (void)proc_parse_fd_limit(line, lim); /* falha mantém o fallback */
            break;
        }
    }
    fclose(f);
}

void proc_read_cmdline(int pid, char *out, int maxlen) {
    char path[PROC_PATH_MAX];
    proc_path(pid, "cmdline", path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(out, (size_t)maxlen, "java (pid %d)", pid); return; }
    int c, i = 0;
    while (i < maxlen-1 && (c = fgetc(f)) != EOF)
        out[i++] = (c == '\0') ? ' ' : (c < 0x20 || c == 0x7f) ? '?' : (char)c;
    out[i] = '\0';
    while (i > 0 && out[i-1] == ' ') out[--i] = '\0';
    fclose(f);
}

void proc_detect_gc(int pid, char *gc, int maxlen) {
    char buf[1024] = {0};
    proc_read_cmdline(pid, buf, sizeof(buf));
    if      (strstr(buf, "UseG1GC"))            snprintf(gc, (size_t)maxlen, "G1GC");
    else if (strstr(buf, "UseZGC"))             snprintf(gc, (size_t)maxlen, "ZGC");
    else if (strstr(buf, "UseShenandoahGC"))    snprintf(gc, (size_t)maxlen, "ShenandoahGC");
    else if (strstr(buf, "UseParallelGC"))      snprintf(gc, (size_t)maxlen, "ParallelGC");
    else if (strstr(buf, "UseConcMarkSweepGC")) snprintf(gc, (size_t)maxlen, "CMS");
    else if (strstr(buf, "UseSerialGC"))        snprintf(gc, (size_t)maxlen, "SerialGC");
    else                                        snprintf(gc, (size_t)maxlen, "Default");
}

unsigned long proc_uptime_secs(int pid) {
    char path[PROC_PATH_MAX], buf[2048];
    proc_path(pid, "stat", path, sizeof(path));
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
    char upath[PROC_PATH_MAX];
    proc_path(-1, "uptime", upath, sizeof(upath));
    FILE *uf = fopen(upath, "r");
    if (!uf) return 0;
    double sys_up = 0.0;
    fscanf(uf, "%lf", &sys_up);
    fclose(uf);
    long clk = sysconf(_SC_CLK_TCK);
    double up = sys_up - (double)starttime / (double)clk;
    return up > 0.0 ? (unsigned long)up : 0UL;
}

void proc_count_threads(int pid, thr_t *t) {
    char path[PROC_PATH_MAX];
    proc_path(pid, "task", path, sizeof(path));
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
