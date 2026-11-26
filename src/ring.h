#ifndef JVMA_RING_H
#define JVMA_RING_H

#include "jvma_types.h"

/* Buffer circular de HISTORY_LEN amostras e renderização em sparkline. */
void ring_push(ring_t *r, double val);
void ring_sparkline(const ring_t *r, char *out, int width);

#endif /* JVMA_RING_H */
