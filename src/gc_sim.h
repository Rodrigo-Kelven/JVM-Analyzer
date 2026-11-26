#ifndef JVMA_GC_SIM_H
#define JVMA_GC_SIM_H

#include "jvma_types.h"

/* GC simulado é opt-in: --sim-gc / JVMA_SIM_GC=1 ligam o random walk;
 * --no-sim-gc / JVMA_NO_SIM_GC=1 desligam e prevalecem. */
int  gc_sim_opt_in(int argc, char **argv);
void gc_sim_set_disabled(int disabled);
int  gc_sim_enabled(void);

void gc_sim_step(jvm_t *j);
int  gc_sim_disabled(int argc, char **argv);

#endif /* JVMA_GC_SIM_H */
