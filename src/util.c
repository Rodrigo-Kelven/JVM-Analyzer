#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jvma_types.h"
#include "util.h"

/* Select fg color based on pct vs warn/crit thresholds */
int pct_color(double pct, double w, double c) {
    if (pct >= c) return CL_CRIT;
    if (pct >= w) return CL_WARN;
    return CL_HEALTHY;
}

/* Escapa uma string C para uso entre aspas em JSON. Cada byte pode ocupar
 * até 6 bytes na saída (\u00XX), então o chamador deve reservar
 * 6 * strlen(s) + 1 bytes. A truncagem por falta de espaço nunca corta uma
 * sequência de escape ao meio. */
void json_escape(const char *s, char *out, size_t cap)
{
    size_t o = 0;

    if (cap == 0) return;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char ubuf[7];          /* \u00XX + NUL */
        const char *add = NULL;
        size_t need;

        switch (*p) {
        case '"':  add = "\\\""; break;
        case '\\': add = "\\\\"; break;
        case '\n': add = "\\n"; break;
        case '\t': add = "\\t"; break;
        case '\r': add = "\\r"; break;
        default:
            if (*p < 0x20) {
                snprintf(ubuf, sizeof(ubuf), "\\u%04x", (unsigned)*p);
                add = ubuf;
            }
            break;
        }
        need = add ? strlen(add) : 1;
        if (o + need + 1 > cap) break;
        if (add) memcpy(out + o, add, need);
        else     out[o] = (char)*p;
        o += need;
    }
    out[o] = '\0';
}

void fmt_uptime(unsigned long s, char *out, int max) {
    unsigned long h=s/3600, m=(s%3600)/60, sec=s%60;
    if (h > 0)      snprintf(out, (size_t)max, "%luh%02lum%02lus", h, m, sec);
    else if (m > 0) snprintf(out, (size_t)max, "%lum%02lus", m, sec);
    else            snprintf(out, (size_t)max, "%lus", sec);
}

void fmt_mb(double mb, char *out, int max) {
    if (mb >= 1024.0) snprintf(out, max, "%.2fGB", mb/1024.0);
    else              snprintf(out, max, "%.0fMB", mb);
}

int parse_pid_arg(const char *s, int *out)
{
    char *end;
    long v;

    if (s == NULL || *s == '\0' || *s == '+' ||
        isspace((unsigned char)*s)) return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0') return -1;
    if (v < 1 || v > INT_MAX) return -1;
    *out = (int)v;
    return 0;
}

unsigned long parse_xmx_kb(const char *cmdline)
{
    const char *p = strstr(cmdline, "-Xmx");
    if (!p) return 0;
    char *end;
    unsigned long v = strtoul(p + 4, &end, 10);
    if (end == p + 4) return 0;
    switch (*end) {
    case 'k': case 'K': return v;
    case 'g': case 'G': return v * 1024UL * 1024UL;
    case 't': case 'T': return v * 1024UL * 1024UL * 1024UL;
    case 'm': case 'M': return v * 1024UL;
    default:            return v / 1024UL;   /* bytes */
    }
}
