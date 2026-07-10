#ifndef _YOS_XO_H_
#define _YOS_XO_H_
/* Minimal libxo(3) surface bin/ps links. The real libxo emits structured
 * output (text/json/xml/html); ps only ever runs in TEXT style here, so the
 * shim (yos_ps_compat.c) renders xo_emit's format language straight to stdio. */
#include <stdarg.h>
#include <stdio.h>
#define XO_STYLE_TEXT 0
#define XOF_COLUMNS   0
#define XO_UNIT_NONE  0
typedef struct xo_handle_s xo_handle_t;
int  xo_parse_args(int, char **);
int  xo_emit(const char *, ...);
int  xo_emit_h(xo_handle_t *, const char *, ...);
void xo_open_list(const char *);
void xo_close_list(const char *);
void xo_open_instance(const char *);
void xo_close_instance(const char *);
void xo_open_container(const char *);
void xo_close_container(const char *);
int  xo_finish(void);
void xo_set_flags(xo_handle_t *, unsigned);
unsigned xo_get_style(xo_handle_t *);
void xo_warn(const char *, ...);
void xo_warnx(const char *, ...);
void xo_err(int, const char *, ...);
void xo_errx(int, const char *, ...);
void xo_error(const char *, ...);
#endif
