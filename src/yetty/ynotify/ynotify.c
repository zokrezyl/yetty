/*
 * ynotify.c — see include/yetty/ynotify/ynotify.h.
 *
 * Single global handler slot, lazy-initialised mutex (matching ytrace.c's
 * shape so we don't need a separate init entrypoint that every binary
 * has to remember to call). The lock window only covers the handler
 * snapshot — the actual handler call happens outside, so a slow handler
 * doesn't stall other ynotify callers.
 */

#include <yetty/ynotify/ynotify.h>

#include <stdarg.h>
#include <stdio.h>

#include <yetty/yplatform/thread.h>
#include <yetty/ytrace/ytrace.h>

static struct yetty_yplatform_ymutex *g_mutex      = NULL;
static yetty_ynotify_handler_fn       g_handler    = NULL;
static void                          *g_userdata   = NULL;

static void ensure_mutex(void)
{
    /* Same lazy pattern as ytrace. The risk of two threads racing here
     * is identical to ytrace's — practically zero, since registration
     * happens once at process startup before any worker thread runs. */
    if (!g_mutex) {
        g_mutex = yetty_yplatform_ymutex_create();
    }
}

void ynotify_set_handler(yetty_ynotify_handler_fn handler, void *userdata)
{
    ensure_mutex();
    yetty_yplatform_ymutex_lock(g_mutex);
    g_handler  = handler;
    g_userdata = userdata;
    yetty_yplatform_ymutex_unlock(g_mutex);
}

void vynotify(int severity, const char *fmt, va_list ap)
{
    if (!fmt) {
        return;
    }
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, ap);

    /* 1. Log unconditionally so the message is captured in tmp logs
     *    even when no on-screen handler is installed. */
    switch (severity) {
    case YETTY_YNOTIFY_INFO:  yinfo("%s", msg);  break;
    case YETTY_YNOTIFY_WARN:  ywarn("%s", msg);  break;
    case YETTY_YNOTIFY_ERROR: yerror("%s", msg); break;
    default:                  yerror("%s", msg); break;
    }

    /* 2. Snapshot the handler under the lock, then call it outside. A
     *    handler that re-enters ynotify (e.g. on its own error path)
     *    would otherwise deadlock the slow-path. */
    yetty_ynotify_handler_fn handler = NULL;
    void                    *userdata = NULL;
    if (g_mutex) {
        yetty_yplatform_ymutex_lock(g_mutex);
        handler  = g_handler;
        userdata = g_userdata;
        yetty_yplatform_ymutex_unlock(g_mutex);
    }
    if (handler) {
        handler(severity, msg, userdata);
    }
}

void ynotify(int severity, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vynotify(severity, fmt, ap);
    va_end(ap);
}
