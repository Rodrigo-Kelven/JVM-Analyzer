#ifndef JVMA_PROC_H
#define JVMA_PROC_H

#include <stddef.h>

#include "jvma_types.h"

/* Caminhos /proc: a raiz padrão é "/proc" e pode ser injetada nos testes.
 * proc_set_path() substitui um arquivo específico ("stat", "status", ...);
 * path NULL/"" restaura a montagem a partir da raiz. */
void proc_set_root(const char *root);
void proc_set_path(const char *leaf, const char *path);

/* pid >= 0: <root>/<pid>[/leaf]; pid < 0: <root>[/leaf]. */
void proc_path(int pid, const char *leaf, char *out, size_t cap);

/* Leitores e parsers (comportamento inalterado). */
int           proc_parse_cpu_fields(const char *p, unsigned long long *ut,
                                    unsigned long long *st);
int           proc_read_cpu(int pid, cpusnap_t *out);
double        proc_calc_cpu(const cpusnap_t *prev, const cpusnap_t *cur);
int           proc_read_mem_status(int pid, mem_t *m);
void          proc_read_smaps_rollup(int pid, mem_t *m);
unsigned long proc_estimate_heap(int pid);
void          proc_read_memtotal(mem_t *m);
int           proc_parse_fd_limit(const char *line, int *lim);
void          proc_read_fd(int pid, int *cnt, int *lim);
void          proc_read_cmdline(int pid, char *out, int maxlen);
void          proc_detect_gc(int pid, char *gc, int maxlen);
unsigned long proc_uptime_secs(int pid);
void          proc_count_threads(int pid, thr_t *t);

#endif /* JVMA_PROC_H */
