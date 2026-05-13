/*
 * yplatform/term.h - Cross-platform terminal helpers
 */

#ifndef YETTY_YPLATFORM_TERM_H
#define YETTY_YPLATFORM_TERM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Check if stderr supports ANSI colors (enables VT processing on Windows) */
int yetty_yplatform_stderr_supports_color(void);

/* Format current local time as "HH:MM:SS.mmm" into buf (must be >= 16 bytes) */
void yetty_yplatform_format_timestamp(char *buf, size_t bufsize);

/* Query the controlling terminal's cell dimensions. Writes the column
 * and row counts to *cols / *rows and returns 0 on success. Returns -1
 * if stdout/stdin isn't a tty (or the size cannot be determined); in
 * that case *cols and *rows are left unchanged so the caller can use
 * its own defaults. */
int yetty_yplatform_term_get_size(int *cols, int *rows);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_TERM_H */
