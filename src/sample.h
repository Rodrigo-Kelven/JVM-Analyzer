#ifndef JVMA_SAMPLE_H
#define JVMA_SAMPLE_H

#include "jvma_types.h"

/* Coleta/amostragem de um ciclo: /proc, random walk do GC, rings e alertas.
 * Retorna -1 se o processo desapareceu ou uma leitura obrigatória falhou. */
int sample_update(jvm_t *j);

#endif /* JVMA_SAMPLE_H */
