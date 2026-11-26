#include "sample.h"
#include "alerts.h"
#include "gc_sim.h"
#include "proc.h"
#include "ring.h"

/* =========================================================================
 * STATE UPDATE — coleta de amostra (antes update_jvm no monólito)
 * ========================================================================= */
int sample_update(jvm_t *j) {
    cpusnap_t snap;
    if (proc_read_cpu(j->pid, &snap) != 0) return -1;
    j->cpu_cur  = snap;
    j->cpu_pct  = j->first_sample ? 0.0 : proc_calc_cpu(&j->cpu_prev, &j->cpu_cur);
    j->first_sample = 0;
    j->cpu_prev = j->cpu_cur;

    if (proc_read_mem_status(j->pid, &j->mem) != 0) return -1;
    proc_read_smaps_rollup(j->pid, &j->mem);

    j->heap_ctr++;
    if (j->heap_ctr >= 5) {
        j->heap_ctr = 0;
        unsigned long hk = proc_estimate_heap(j->pid);
        j->mem.java_heap_kb = hk > 0 ? hk : (j->mem.anon_kb * 2 / 3);
    }

    proc_count_threads(j->pid, &j->thr);
    proc_read_fd(j->pid, &j->fd_count, &j->fd_limit);
    j->uptime_s = proc_uptime_secs(j->pid);

    /* GC pause: random walk (no JMX available), desligável via --no-sim-gc */
    gc_sim_step(j);
    j->gc_events++;

    ring_push(&j->h_cpu,     j->cpu_pct);
    ring_push(&j->h_rss,     j->mem.rss_kb / 1024.0);
    ring_push(&j->h_threads, j->thr.total);

    eval_alerts(j);
    return 0;
}
