#include <stdlib.h>
#include <string.h>

#include "gc_sim.h"

/* Estado privado do módulo; acesso externo apenas via gc_sim_set_disabled()
 * e gc_sim_enabled(). */
static int g_no_sim_gc = 0;

void gc_sim_set_disabled(int disabled) { g_no_sim_gc = disabled ? 1 : 0; }
int  gc_sim_enabled(void)             { return !g_no_sim_gc; }

/* Random walk idêntico ao baseline quando g_no_sim_gc == 0 (mesma ordem de
 * chamadas a rand()). Com a simulação desligada, o campo é fixado em 0.0. */
void gc_sim_step(jvm_t *j)
{
    if (g_no_sim_gc) {
        j->gc_pause_ms = 0.0;
        return;
    }
    j->gc_pause_ms += ((double)(rand() % 100) - 50) * 0.04;
    if (j->gc_pause_ms < 1.0)   j->gc_pause_ms = 2.0 + rand() % 15;
    if (j->gc_pause_ms > 350.0) j->gc_pause_ms = 8.0;
}

int gc_sim_opt_in(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (argv[i] != NULL && strcmp(argv[i], "--sim-gc") == 0)
            return 1;
    const char *v = getenv("JVMA_SIM_GC");
    return v != NULL && strcmp(v, "1") == 0;
}

/* Desligar a simulação de GC: CLI em qualquer posição prevalece
 * unidirecionalmente; na falta da flag, só o valor exato "1" da variável
 * ativa o modo (presente/vazio/"0"/outros textos não ativam). */
int gc_sim_disabled(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (argv[i] != NULL && strcmp(argv[i], "--no-sim-gc") == 0)
            return 1;

    const char *v = getenv("JVMA_NO_SIM_GC");
    return v != NULL && strcmp(v, "1") == 0;
}
