#ifndef JVMA_UTIL_H
#define JVMA_UTIL_H

#include <stddef.h>

/* Helpers de formatação e escaping sem estado compartilhado. */
int  pct_color(double pct, double w, double c);
void fmt_uptime(unsigned long s, char *out, int max);
void fmt_mb(double mb, char *out, int max);
void json_escape(const char *s, char *out, size_t cap);

/* -Xmx<n>[k|m|g|t] (ou -XX:MaxHeapSize=) no cmdline, em KB; 0 se ausente. */
unsigned long parse_xmx_kb(const char *cmdline);

/* PID estrito: strtol sem whitespace, '+', sinal ou lixo final. */
int parse_pid_arg(const char *s, int *out);

#endif /* JVMA_UTIL_H */
