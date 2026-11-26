#ifndef JVMA_TYPES_H
#define JVMA_TYPES_H

#include <time.h>   /* struct timespec */

/* =========================================================================
 * VERSION + CONSTANTS
 * ========================================================================= */
#define JVMA_VERSION    "6.0"
#define HISTORY_LEN     60
#define MAX_ALERTS      24
#define MAX_DISCOVERED  48
#define DEFAULT_ITVL_MS 2000
#define NOTIFY_TTL_SEC  3
#define FBUF_SIZE       (128 * 1024)

/* Alert thresholds */
#define TH_CPU_W     70.0
#define TH_CPU_C     90.0
#define TH_GC_W_MS   50.0
#define TH_GC_C_MS  200.0
#define TH_THR_W     150
#define TH_THR_C     300
#define TH_ZMB_W       3
#define TH_ZMB_C       8
#define TH_FD_W      800
#define TH_FD_C     3000

/* 256-color palette */
#define CL_BORDER    51   /* bright cyan        — borders, panel titles */
#define CL_HEALTHY   82   /* bright green       — healthy values        */
#define CL_WARN     220   /* yellow             — warning values        */
#define CL_CRIT     196   /* bright red         — critical values       */
#define CL_LABEL    252   /* light gray         — metric labels         */
#define CL_VALUE     87   /* light cyan         — numeric values        */
#define CL_DIM      242   /* medium gray        — secondary text        */
#define CL_ACCENT   213   /* light magenta      — panel names/accents   */
#define CL_HDR_BG    23   /* dark teal          — header bar background */
#define CL_HDR_FG   255   /* white              — header bar text       */
#define CL_OK        46   /* green              — "all nominal"         */

/* =========================================================================
 * DATA STRUCTURES
 * ========================================================================= */

typedef struct {
    double v[HISTORY_LEN];
    int    head, count;
} ring_t;

typedef enum { SEV_WARN=0, SEV_CRIT=1 } sev_t;
typedef enum {
    AL_CPU=0, AL_GC, AL_THREAD, AL_ZOMBIE,
    AL_DEADLOCK, AL_FD, AL_SWAP
} al_kind_t;

typedef struct {
    al_kind_t kind;
    sev_t     sev;
    double    val;
    char      msg[128];
} alert_t;

typedef struct {
    int total, virt, platform, daemon;
    int zombie, blocked, runnable, waiting;
} thr_t;

typedef struct {
    unsigned long long utime, stime;
    struct timespec    ts;
} cpusnap_t;

typedef struct {
    unsigned long rss_kb, vsz_kb, swap_kb, peak_kb;
    unsigned long anon_kb, pss_kb;
    unsigned long java_heap_kb;
    unsigned long mem_total_kb;
} mem_t;

typedef struct {
    int           pid;
    char          cmdline[512];
    char          gc_type[48];
    unsigned long uptime_s;

    cpusnap_t cpu_prev, cpu_cur;
    double    cpu_pct;
    int       first_sample;

    mem_t mem;
    unsigned long xmx_kb;   /* -Xmx do cmdline; 0 = desconhecido */
    int   heap_ctr;

    thr_t thr;
    int   fd_count, fd_limit;

    double gc_pause_ms;
    long   gc_events;

    alert_t alerts[MAX_ALERTS];
    int     nalerts;

    ring_t h_cpu, h_rss, h_threads;

    int paused;
} jvm_t;

typedef struct {
    int  pid;
    char cmdline[256];
    char gc_type[32];
    int  nthr;
} jvm_entry_t;

#endif /* JVMA_TYPES_H */
