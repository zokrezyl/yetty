#ifndef YOPENSTREET_INTERACTIVE_H
#define YOPENSTREET_INTERACTIVE_H

/* Interactive map session (see interactive.c). Takes the initial view
 * from the CLI config; returns a process exit code. */

#include <stdbool.h>

struct yetty_yopenstreet_config;

int yopenstreet_interactive_run(const struct yetty_yopenstreet_config *initial_config,
                                bool vector_mode);

#endif /* YOPENSTREET_INTERACTIVE_H */
