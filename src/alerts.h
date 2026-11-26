#ifndef JVMA_ALERTS_H
#define JVMA_ALERTS_H

#include "jvma_types.h"

/* Regras de threshold centralizadas em jvma_types.h, avaliadas em alerts.c. */
void alerts_clear(jvm_t *j);
void alert_add(jvm_t *j, al_kind_t k, sev_t sv, double val,
               const char *fmt, ...);
void eval_alerts(jvm_t *j);

#endif /* JVMA_ALERTS_H */
