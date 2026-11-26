#ifndef JVMA_GC_SIM_H
#define JVMA_GC_SIM_H

#include "jvma_types.h"

/* GC simulado é o padrão; --no-sim-gc / JVMA_NO_SIM_GC=1 desligam o random walk. */
void gc_sim_set_disabled(int disabled);
int  gc_sim_enabled(void);

void gc_sim_step(jvm_t *j);
int  gc_sim_disabled(int argc, char **argv);

#endif /* JVMA_GC_SIM_H */
