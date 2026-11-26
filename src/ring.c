#include <string.h>

#include "ring.h"

/* =========================================================================
 * RING BUFFER + SPARKLINE
 * ========================================================================= */
/* Sparkline characters — usados apenas por ring_sparkline(). */
static const char *SPARK[] = {"▁","▂","▃","▄","▅","▆","▇","█"};

void ring_push(ring_t *r, double val) {
    r->v[r->head] = val;
    r->head = (r->head + 1) % HISTORY_LEN;
    if (r->count < HISTORY_LEN) r->count++;
}

void ring_sparkline(const ring_t *r, char *out, int width) {
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

int ring_stats(const ring_t *r, int width, double *mn, double *mx,
               double *first, double *last)
{
    int n = r->count < width ? r->count : width;
    if (n <= 0) return 0;
    *mn = 1e18; *mx = -1e18;
    for (int i = 0; i < n; i++) {
        double v = r->v[((r->head - 1 - i) + HISTORY_LEN*2) % HISTORY_LEN];
        if (v < *mn) *mn = v;
        if (v > *mx) *mx = v;
        if (i == 0) *last = v;
        if (i == n - 1) *first = v;
    }
    return n;
}
