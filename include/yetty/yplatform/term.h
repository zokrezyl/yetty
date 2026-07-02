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

/* Like yetty_yplatform_term_get_size, but also reports the terminal's pixel
 * area (ws_xpixel / ws_ypixel from TIOCGWINSZ) when the terminal provides it.
 * *pixel_width / *pixel_height receive the pane size in pixels, or 0 when the
 * terminal does not report a pixel size (e.g. a Win32 console). Any of the
 * output pointers may be NULL. Returns 0 on success, -1 if the size cannot be
 * determined (outputs left unchanged). */
int yetty_yplatform_term_get_size_pixels(int *cols, int *rows, int *pixel_width,
                                         int *pixel_height);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_TERM_H */
