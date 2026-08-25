/*
 * ytermsink — the terminal-host "sink": the abstract interface a terminal-
 * content figure (yvterm's grid + vterm figure) calls UP to reach services
 * only its host terminal can provide:
 *
 *   pty_write        relay child-directed bytes (keystrokes, query
 *                    responses) to the real PTY
 *   request_render   ask the host to schedule a frame
 *   mouse_sub        report a DEC 1500/1501 mouse/card subscription change
 *   clipboard_write  hand an OSC 52 payload up so the host sets the OS
 *                    clipboard
 *   sixel_write      hand a decoded sixel image up so the host presents it
 *   set_title        report the terminal title (OSC 0/2) so the host can
 *                    surface it (tab label, window title)
 *
 * Direction is always content -> host. Each is a `virtual@` slot with a no-op
 * default; the host (the terminal) derives from this base (parent@ytermsink:
 * sink) and overrides them. The content figure is handed the terminal AS a
 * sink object and dispatches these methods on it — so the injected object IS
 * the state the old `(fn, userdata)` callbacks captured.
 *
 * This inverts the dependency: the interface lives BELOW both yvterm and
 * yterminal, so neither depends on the other (no cycle). It is GPU-free
 * (yclass + ycore only), so a content figure's generated api header names
 * nothing but an opaque `struct yetty_yclass_object *`.
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <stddef.h>

/* pty_write: relay child-directed bytes to the real PTY. Default no-op — a
 * host with no PTY (headless/test) drops them. */
YETTY_ANNOTATE("virtual@ytermsink:sink:pty_write")
YETTY_ANNOTATE("local@ytermsink:pty_write")
static struct yetty_ycore_void_result yetty_ytermsink_sink_default_pty_write(
    struct yetty_yclass_object *obj, const char *data, size_t len)
{
    (void)obj;
    (void)data;
    (void)len;
    return YETTY_OK_VOID();
}

/* request_render: ask the host to schedule a frame. Default no-op. */
YETTY_ANNOTATE("virtual@ytermsink:sink:request_render")
YETTY_ANNOTATE("local@ytermsink:request_render")
static struct yetty_ycore_void_result yetty_ytermsink_sink_default_request_render(
    struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_OK_VOID();
}

/* mouse_sub: report the current DEC 1500/1501 (card click/move) plus key
 * subscription state so the host starts/stops forwarding input. Default
 * no-op. */
YETTY_ANNOTATE("virtual@ytermsink:sink:mouse_sub")
YETTY_ANNOTATE("local@ytermsink:mouse_sub")
static struct yetty_ycore_void_result yetty_ytermsink_sink_default_mouse_sub(
    struct yetty_yclass_object *obj, int click_enabled, int move_enabled, int key_enabled)
{
    (void)obj;
    (void)click_enabled;
    (void)move_enabled;
    (void)key_enabled;
    return YETTY_OK_VOID();
}

/* clipboard_write: hand an OSC 52 payload up so the host sets the OS
 * clipboard. `clipboard` is non-zero for the system clipboard ('c'), zero
 * for the primary selection. Default no-op. */
YETTY_ANNOTATE("virtual@ytermsink:sink:clipboard_write")
YETTY_ANNOTATE("local@ytermsink:clipboard_write")
static struct yetty_ycore_void_result yetty_ytermsink_sink_default_clipboard_write(
    struct yetty_yclass_object *obj, const char *text, size_t len, int clipboard)
{
    (void)obj;
    (void)text;
    (void)len;
    (void)clipboard;
    return YETTY_OK_VOID();
}

/* sixel_write: hand a decoded sixel image up so the host presents it as an
 * anchored image figure. Default no-op. */
YETTY_ANNOTATE("virtual@ytermsink:sink:sixel_write")
YETTY_ANNOTATE("local@ytermsink:sixel_write")
static struct yetty_ycore_void_result yetty_ytermsink_sink_default_sixel_write(
    struct yetty_yclass_object *obj, const char *data, size_t len)
{
    (void)obj;
    (void)data;
    (void)len;
    return YETTY_OK_VOID();
}

/* set_title: report the terminal title set by the running program (OSC 0/2)
 * so the host can surface it — tab label, window title. `title` is the
 * complete NUL-terminated title (fragments already assembled by the content
 * side); `len` excludes the NUL. Default no-op. */
YETTY_ANNOTATE("virtual@ytermsink:sink:set_title")
YETTY_ANNOTATE("local@ytermsink:set_title")
static struct yetty_ycore_void_result yetty_ytermsink_sink_default_set_title(
    struct yetty_yclass_object *obj, const char *title, size_t len)
{
    (void)obj;
    (void)title;
    (void)len;
    return YETTY_OK_VOID();
}

/* Pure abstract interface — no per-instance data. The implementing host (the
 * terminal) carries the real state; these slots dispatch onto its object. The
 * single reserved member keeps the data slice a well-formed (non-empty) C
 * struct without implying any state. */
struct YETTY_ANNOTATE("class@ytermsink:sink") yetty_ytermsink_sink {
    int reserved_unused;
};

/* Result wrapper for the sink-base handle. Declared here (not pulled from the
 * generated sink.h, which this TU does not include) so the appended impl glue
 * has the type in scope. The public header publishes the identical
 * declaration for other modules. */
YETTY_YRESULT_DECLARE(yetty_ytermsink_sink_ptr, struct yetty_ytermsink_sink *);

#include "yetty/gen/impl/ytermsink/sink.c"
