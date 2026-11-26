#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "export.h"
#include "util.h"

char g_export[256] = "./exports";

static export_notify_fn g_notify_fn = NULL;

void export_set_notify(export_notify_fn fn) { g_notify_fn = fn; }

static void notify_via_cb(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    if (!g_notify_fn) return;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    g_notify_fn(msg);
}

void ensure_export_dir(void) {
    struct stat st;
    if (stat(g_export, &st) != 0 && mkdir(g_export, 0755) != 0 && errno != EEXIST)
        fprintf(stderr, "JVMA: falha ao criar %s: %s\n", g_export, strerror(errno));
}

/* Abre sem seguir symlink no último componente (evita symlink plantado). */
static FILE *open_nofollow(const char *path, int append)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC |
                        (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, append ? "a" : "w");
    if (!f) close(fd);
    return f;
}

static void export_report_io(const char *action, const char *path)
{
    int e = errno;
    fprintf(stderr, "JVMA: falha ao %s %s: %s\n", action, path,
            e ? strerror(e) : "erro de E/S");
}

/* fprintf com verificação explícita do retorno; falha é reportada uma vez. */
static int export_print(FILE *f, const char *path, const char *fmt, ...)
{
    va_list ap;
    int rc;

    errno = 0;
    va_start(ap, fmt);
    rc = vfprintf(f, fmt, ap);
    va_end(ap);
    if (rc < 0 || ferror(f)) {
        export_report_io("escrever", path);
        return 0;
    }
    return 1;
}

/* Decimal de exportação sempre com ponto; demais categorias intactas. */
void export_numeric_push(char *saved, size_t cap)
{
    const char *cur = setlocale(LC_NUMERIC, NULL);
    snprintf(saved, cap, "%s", cur ? cur : "C");
    setlocale(LC_NUMERIC, "C");
}

void export_numeric_pop(const char *saved)
{
    setlocale(LC_NUMERIC, saved);
}

void export_json(const jvm_t *j) {
    ensure_export_dir();
    char path[512];
    time_t now = time(NULL);
    snprintf(path, sizeof(path), "%s/jvma_%d_%ld.json", g_export, j->pid, now);
    errno = 0;
    FILE *f = open_nofollow(path, 0);
    if (!f) {
        export_report_io("abrir", path);
        return;                 /* TUI permanece intacta */
    }

    /* Mantém o limite de 300 bytes do baseline antes de escapar. */
    char cmd_raw[301];
    char cmd_escaped[300 * 6 + 1];
    char gc_escaped[sizeof(j->gc_type) * 6 + 1];
    snprintf(cmd_raw, sizeof(cmd_raw), "%.300s", j->cmdline);
    json_escape(cmd_raw, cmd_escaped, sizeof(cmd_escaped));
    json_escape(j->gc_type, gc_escaped, sizeof(gc_escaped));

    char saved_numeric[64];
    export_numeric_push(saved_numeric, sizeof(saved_numeric));

    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    if (!export_print(f, path, "{\n  \"jvma_version\": \"%s\",\n  \"timestamp\": \"%s\",\n",
                      JVMA_VERSION, ts)) goto write_failed;
    if (!export_print(f, path, "  \"unix_ts\": %ld,\n  \"pid\": %d,\n  \"gc_type\": \"%s\",\n",
                      now, j->pid, gc_escaped)) goto write_failed;
    if (!export_print(f, path, "  \"uptime_s\": %lu,\n  \"cmdline\": \"%s\",\n",
                      j->uptime_s, cmd_escaped)) goto write_failed;
    if (!export_print(f, path, "  \"cpu_pct\": %.2f,\n", j->cpu_pct)) goto write_failed;
    if (!export_print(f, path, "  \"memory\": { \"rss_mb\": %.1f, \"vsz_mb\": %.1f, \"swap_mb\": %.1f,\n"
                               "    \"pss_mb\": %.1f, \"heap_est_mb\": %.1f, \"anon_mb\": %.1f },\n",
                      j->mem.rss_kb/1024.0, j->mem.vsz_kb/1024.0, j->mem.swap_kb/1024.0,
                      j->mem.pss_kb/1024.0, j->mem.java_heap_kb/1024.0,
                      j->mem.anon_kb/1024.0)) goto write_failed;
    if (!export_print(f, path, "  \"threads\": { \"total\": %d, \"virtual\": %d, \"platform\": %d,\n"
                               "    \"daemon\": %d, \"runnable\": %d, \"waiting\": %d, \"blocked\": %d, \"zombie\": %d },\n",
                      j->thr.total, j->thr.virt, j->thr.platform,
                      j->thr.daemon, j->thr.runnable, j->thr.waiting,
                      j->thr.blocked, j->thr.zombie)) goto write_failed;
    if (!export_print(f, path, "  \"fds\": { \"open\": %d, \"limit\": %d },\n",
                      j->fd_count, j->fd_limit)) goto write_failed;
    if (!export_print(f, path, "  \"gc_pause_ms\": %.1f,\n  \"alerts\": %d\n}\n",
                      j->gc_pause_ms, j->nalerts)) goto write_failed;

    errno = 0;
    if (fclose(f) != 0) {
        export_report_io("fechar", path);
        export_numeric_pop(saved_numeric);
        return;
    }
    export_numeric_pop(saved_numeric);

    notify_via_cb("JSON saved: %s", path);
    return;

write_failed:
    fclose(f);                  /* erro já reportado por export_print */
    export_numeric_pop(saved_numeric);
    return;
}

void export_csv(const jvm_t *j) {
    ensure_export_dir();
    char path[512];
    snprintf(path, sizeof(path), "%s/jvma_%d.csv", g_export, j->pid);
    errno = 0;
    FILE *f = open_nofollow(path, 1);
    if (!f) {
        export_report_io("abrir", path);
        return;
    }
    struct stat st;
    int write_hdr = (fstat(fileno(f), &st) == 0 && st.st_size == 0);

    char saved_numeric[64];
    export_numeric_push(saved_numeric, sizeof(saved_numeric));

    if (write_hdr) {
        if (!export_print(f, path, "timestamp,pid,cpu_pct,rss_mb,vsz_mb,swap_mb,pss_mb,"
                                   "heap_est_mb,anon_mb,threads,runnable,waiting,blocked,"
                                   "zombie,virtual,daemon,fd_count,fd_limit,gc_type,gc_pause_ms,uptime_s\n"))
            goto write_failed;
    }
    if (!export_print(f, path, "%ld,%d,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,"
                              "%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%.1f,%lu\n",
                      time(NULL), j->pid, j->cpu_pct,
                      j->mem.rss_kb/1024.0, j->mem.vsz_kb/1024.0,
                      j->mem.swap_kb/1024.0, j->mem.pss_kb/1024.0,
                      j->mem.java_heap_kb/1024.0, j->mem.anon_kb/1024.0,
                      j->thr.total, j->thr.runnable, j->thr.waiting,
                      j->thr.blocked, j->thr.zombie, j->thr.virt, j->thr.daemon,
                      j->fd_count, j->fd_limit, j->gc_type, j->gc_pause_ms,
                      j->uptime_s)) goto write_failed;

    errno = 0;
    if (fclose(f) != 0) {
        export_report_io("fechar", path);
        export_numeric_pop(saved_numeric);
        return;
    }
    export_numeric_pop(saved_numeric);

    notify_via_cb("CSV appended: %s", path);
    return;

write_failed:
    fclose(f);
    export_numeric_pop(saved_numeric);
    return;
}
