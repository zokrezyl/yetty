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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_TERM_H */
