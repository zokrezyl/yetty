/*
 * yplatform/term.h - Cross-platform terminal helpers
 */

#ifndef YOS_YPLATFORM_TERM_H
#define YOS_YPLATFORM_TERM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Check if stderr supports ANSI colors (enables VT processing on Windows) */
int yos_yplatform_stderr_supports_color(void);

/* Format current local time as "HH:MM:SS.mmm" into buf (must be >= 16 bytes) */
void yos_yplatform_format_timestamp(char *buf, size_t bufsize);

#ifdef __cplusplus
}
#endif

#endif /* YOS_YPLATFORM_TERM_H */
