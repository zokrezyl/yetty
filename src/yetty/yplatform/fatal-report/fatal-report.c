/*
 * fatal-report.c - hand a fatal error message to the hosting environment
 * before the process exits. See <yetty/yplatform/fatal-report.h>.
 *
 * Single-file capability: the platform split lives in the #ifdef below
 * (webasm hands the message to the page; everything else is a no-op
 * because stderr/trace already reach the operator).
 */

#include <yetty/yplatform/fatal-report.h>

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>

/* Hands the message to the hosting page. terminal.html installs
 * Module.onYettyFatal and opens its abort dialog; pages without the hook
 * fall back to a loud console line. Must never throw back into wasm. */
// clang-format off
EM_JS(void, yetty_yplatform_fatal_report_to_page, (const char *message_ptr), {
    var text = '';
    try {
        text = UTF8ToString(message_ptr);
    } catch (ignored) {
        text = 'yetty fatal error (message unavailable)';
    }
    try {
        if (typeof Module !== 'undefined' && typeof Module.onYettyFatal === 'function') {
            Module.onYettyFatal(text);
            return;
        }
    } catch (ignored) {
    }
    try {
        console.error('[yetty-fatal] ' + text);
    } catch (ignored) {
    }
});
// clang-format on

void yetty_yplatform_fatal_report(const char *message)
{
    yetty_yplatform_fatal_report_to_page(message ? message : "yetty fatal error (no message)");
}

#else /* !__EMSCRIPTEN__ */

void yetty_yplatform_fatal_report(const char *message)
{
    /* Native: stderr and the ytrace log already carry the message. */
    (void)message;
}

#endif
