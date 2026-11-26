#ifndef JVMA_EXPORT_H
#define JVMA_EXPORT_H

#include "jvma_types.h"
#include <stddef.h>

/* Diretório de exportação (compartilhado com main/tests). */
extern char g_export[256];

typedef void (*export_notify_fn)(const char *msg);

/* Configura callback de notificação; NULL desativa. */
void export_set_notify(export_notify_fn fn);

void export_numeric_push(char *saved, size_t cap);
void export_numeric_pop(const char *saved);

void ensure_export_dir(void);
void export_json(const jvm_t *j);
void export_csv(const jvm_t *j);

#endif /* JVMA_EXPORT_H */
