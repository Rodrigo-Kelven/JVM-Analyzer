#include <stdarg.h>
#include <stdio.h>

#include "alerts.h"
#include "gc_sim.h"

/* =========================================================================
 * ALERT SYSTEM — mesmas regras e mensagens do baseline
 * ========================================================================= */
void alerts_clear(jvm_t *j) { j->nalerts = 0; }

void alert_add(jvm_t *j, al_kind_t k, sev_t sv, double val,
               const char *fmt, ...) {
    for (int i = 0; i < j->nalerts; i++) {
        if (j->alerts[i].kind == k) {
            j->alerts[i].sev = sv; j->alerts[i].val = val;
            va_list ap; va_start(ap, fmt);
            vsnprintf(j->alerts[i].msg, sizeof(j->alerts[i].msg), fmt, ap);
            va_end(ap);
            return;
        }
    }
    if (j->nalerts >= MAX_ALERTS) return;
    alert_t *a = &j->alerts[j->nalerts++];
    a->kind = k; a->sev = sv; a->val = val;
    va_list ap; va_start(ap, fmt);
    vsnprintf(a->msg, sizeof(a->msg), fmt, ap);
    va_end(ap);
}

void eval_alerts(jvm_t *j) {
    alerts_clear(j);
    if (j->cpu_pct >= TH_CPU_C)
        alert_add(j, AL_CPU, SEV_CRIT, j->cpu_pct,
                  "CPU critical: %.1f%% (threshold %.0f%%)", j->cpu_pct, TH_CPU_C);
    else if (j->cpu_pct >= TH_CPU_W)
        alert_add(j, AL_CPU, SEV_WARN, j->cpu_pct,
                  "CPU high: %.1f%% (threshold %.0f%%)", j->cpu_pct, TH_CPU_W);

    /* Alerta de GC é estimativa (est); suprimido quando a simulação está desligada. */
    if (gc_sim_enabled()) {
        if (j->gc_pause_ms >= TH_GC_C_MS)
            alert_add(j, AL_GC, SEV_CRIT, j->gc_pause_ms,
                      "GC pause critical: %.1fms (threshold %.0fms) (est)",
                      j->gc_pause_ms, TH_GC_C_MS);
        else if (j->gc_pause_ms >= TH_GC_W_MS)
            alert_add(j, AL_GC, SEV_WARN, j->gc_pause_ms,
                      "GC pause high: %.1fms (threshold %.0fms) (est)",
                      j->gc_pause_ms, TH_GC_W_MS);
    }

    if (j->thr.total >= TH_THR_C)
        alert_add(j, AL_THREAD, SEV_CRIT, j->thr.total,
                  "Thread explosion: %d threads (threshold %d)", j->thr.total, TH_THR_C);
    else if (j->thr.total >= TH_THR_W)
        alert_add(j, AL_THREAD, SEV_WARN, j->thr.total,
                  "Thread count high: %d (threshold %d)", j->thr.total, TH_THR_W);

    if (j->thr.zombie >= TH_ZMB_C)
        alert_add(j, AL_ZOMBIE, SEV_CRIT, j->thr.zombie,
                  "Zombie threads critical: %d (threshold %d)", j->thr.zombie, TH_ZMB_C);
    else if (j->thr.zombie >= TH_ZMB_W)
        alert_add(j, AL_ZOMBIE, SEV_WARN, j->thr.zombie,
                  "Zombie threads: %d (threshold %d)", j->thr.zombie, TH_ZMB_W);

    if (j->thr.blocked > 5)
        alert_add(j, AL_DEADLOCK, SEV_WARN, j->thr.blocked,
                  "Lock contention: %d threads blocked", j->thr.blocked);

    if (j->fd_count >= TH_FD_C)
        alert_add(j, AL_FD, SEV_CRIT, j->fd_count,
                  "FD critical: %d open (threshold %d)", j->fd_count, TH_FD_C);
    else if (j->fd_count >= TH_FD_W)
        alert_add(j, AL_FD, SEV_WARN, j->fd_count,
                  "FD high: %d open (threshold %d)", j->fd_count, TH_FD_W);

    if (j->mem.swap_kb > 0)
        alert_add(j, AL_SWAP, SEV_WARN, j->mem.swap_kb / 1024.0,
                  "Swap in use: %.0f MB", j->mem.swap_kb / 1024.0);
}
