#ifndef JVMA_RING_H
#define JVMA_RING_H

#include "jvma_types.h"

/* Buffer circular de HISTORY_LEN amostras e renderização em sparkline. */
void ring_push(ring_t *r, double val);
/* Min/max e primeira/última amostra das últimas `width` amostras (as mesmas
 * que ring_sparkline desenha). Retorna quantas amostras; 0 se vazio. */
int  ring_stats(const ring_t *r, int width, double *mn, double *mx,
                double *first, double *last);
void ring_sparkline(const ring_t *r, char *out, int width);

#endif /* JVMA_RING_H */
