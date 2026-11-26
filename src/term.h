#ifndef JVMA_TERM_H
#define JVMA_TERM_H

#include <stddef.h>
#include <time.h>

#include "jvma_types.h"

/* =========================================================================
 * FRAME BUFFER — único dono das sequências ANSI e do flush no fd 1
 * ========================================================================= */
void fb_reset(void);
void fb_write(const char *s);
void fb_printf(const char *fmt, ...);
void fb_move(int row, int col);
void fb_fg(int n);
void fb_bg(int n);
void fb_bold(void);
void fb_reset_attr(void);
void fb_eol(void);
void fb_ctext(int row, int col, int fg, int bold, const char *s);
void fb_cprintf(int row, int col, int fg, int bold, const char *fmt, ...);
void fb_flush(void);

/* Texto simples (menu de descoberta, mensagens de saída): mantém printf()
 * fora dos módulos que não são donos de stdout. */
void term_printf(const char *fmt, ...);
void term_flush_stdout(void);

/* =========================================================================
 * TERMINAL / SINAIS / INTERVALO
 * ========================================================================= */
void term_get_size(void);
void term_raw(void);
void term_restore(void);
int  term_read_key(int timeout_ms);
void term_read_key_blocking(void);
void term_signals_init(void);

int  term_should_run(void);
void term_request_quit(void);
int  term_consume_resize(void);
int  term_width(void);
int  term_height(void);

void interval_deadline(struct timespec *deadline, int interval_ms);
int  deadline_remaining_ms(const struct timespec *deadline);

/* =========================================================================
 * NOTIFICAÇÃO DE EXPORT (sem IO no terminal; desenhada pelo footer)
 * ========================================================================= */
void notify_clear(void);
void notify_set(const char *fmt, ...);
int  notify_active(time_t now);
const char *notify_message(void);

/* =========================================================================
 * Hooks de estado para tests/test_unit.c — as variáveis continuam privadas.
 * ========================================================================= */
int  term_test_running(void);
void term_test_set_running(int value);
int  term_test_resize(void);
void term_test_set_resize(int value);
int  term_test_keys_enabled(void);
void term_test_set_keys_enabled(int value);
void term_test_set_size(int width, int height);
void term_test_signal(int sig);
void term_test_notify_state(char *msg, size_t cap, time_t *at, time_t *until);
void term_test_set_notify_state(const char *msg, time_t at, time_t until);

#endif /* JVMA_TERM_H */
