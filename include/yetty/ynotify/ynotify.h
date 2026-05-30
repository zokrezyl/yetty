#ifndef YETTY_YNOTIFY_H
#define YETTY_YNOTIFY_H

/*
 * ynotify - low-level user-facing notification primitive.
 *
 * Any subsystem can include this header and call `ynotify(sev, "...")`
 * without depending on yui, ygui, or the event loop. The call logs the
 * message through ytrace (yerror/ywarn/yinfo by severity) AND forwards
 * the formatted text to a single registered handler if one is installed.
 *
 * Wiring at startup:
 *   - yui installs a handler in yetty_yui_create() that posts the
 *     notification onto the event-loop thread, where it ultimately calls
 *     the ygui framework's notify on yui's engine. That hop is what
 *     makes ynotify safe to call from any thread.
 *   - When no handler is registered (e.g. headless tools, unit tests),
 *     ynotify still logs — the notification just doesn't appear on
 *     screen. That's intentional: producers shouldn't have to know
 *     whether anyone is listening.
 *
 * Severity drives both the log level and the on-screen accent stripe
 * (INFO mint, WARN amber, ERROR crimson). The integer values are a
 * caller-defined level (0 = info … higher = more severe).
 */

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_ynotify_severity {
    YETTY_YNOTIFY_INFO = 0,
    YETTY_YNOTIFY_WARN = 1,
    YETTY_YNOTIFY_ERROR = 2,
};

/* Handler signature. The msg pointer is borrowed (owned by ynotify's
 * stack frame for the duration of the call); copy if you need to defer.
 * `severity` is one of yetty_ynotify_severity. `userdata` is whatever
 * was passed to ynotify_set_handler. */
typedef void (*yetty_ynotify_handler_fn)(int severity, const char *msg, void *userdata);

/* printf-style. Thread-safe. */
void ynotify(int severity, const char *fmt, ...)
#ifndef _MSC_VER
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* va_list variant — for wrappers that pass through their own ... args. */
void vynotify(int severity, const char *fmt, va_list ap);

/* Install (or clear, with NULL) the single global handler. Thread-safe;
 * lock-serialised against in-flight ynotify calls. The handler stays
 * registered until replaced or cleared. */
void ynotify_set_handler(yetty_ynotify_handler_fn handler, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YNOTIFY_H */
