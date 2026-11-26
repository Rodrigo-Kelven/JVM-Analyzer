#ifndef JVMA_RENDER_H
#define JVMA_RENDER_H

#include "jvma_types.h"

/* Drawing helpers (escrevem apenas via fb_* de src/term). */
void draw_bar(int row, int col, int bw, double pct, double warn, double crit);
void draw_hline(int row, int col, int len);
void draw_panel_hdr(int row, int col, int w, const char *title);
void draw_panel_ftr(int row, int col, int w);
void draw_sides(int row, int col, int w);
void draw_trunc_notice(int row, int col, int w);
void draw_footer(const jvm_t *j, const int *itvl_ms);

/* Painéis: cada um devolve a próxima linha livre. */
int panel_header_bar(const jvm_t *j, int row);
int panel_cpu(const jvm_t *j, int row, int col, int w);
int panel_memory(const jvm_t *j, int row, int col, int w);
int panel_threads(const jvm_t *j, int row, int col, int w);
int panel_gc_fd(const jvm_t *j, int row, int col, int w);
int panel_alerts(const jvm_t *j, int row, int col, int w);
int panel_h_cpu(void);
int panel_h_mem(void);
int panel_h_thr(void);
int panel_h_gcfd(void);
int panel_h_alerts(const jvm_t *j);

/* Master render e overlay de ajuda. */
void render_dashboard(const jvm_t *j, const int *itvl_ms);
void render_help(void);

#endif /* JVMA_RENDER_H */
