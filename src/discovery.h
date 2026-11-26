#ifndef JVMA_DISCOVERY_H
#define JVMA_DISCOVERY_H

#include "jvma_types.h"

/* Descobre JVMs em /proc. Retorna número de entradas preenchidas. */
int discover_jvms(jvm_entry_t *out, int max);

/* Menu interativo de seleção; retorna PID selecionado ou -1. */
int show_discovery_menu(const jvm_entry_t *list, int n);

#endif /* JVMA_DISCOVERY_H */
