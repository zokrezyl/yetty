/*
 * hud.h — ccc's non-scrolling status window.
 *
 * A real ygui `window` widget (titlebar, drag, resize) floating over the
 * scroll buffer. The conversation scrolls in the terminal; the window
 * carries what must NOT scroll away: state (idle / thinking / running
 * tool), the last turn's token/cost line and the session totals.
 *
 * ALL focus and hit-testing is client-side: main.c forwards every mouse
 * event here; ccc_hud_mouse_button reports whether the press landed on
 * the window (→ the GUI owns input) or fell through (→ the terminal
 * owns input). Titlebar drag is native to the window widget; the
 * resize grip (bottom-right corner) is implemented here in ccc.
 *
 * Drives the ygui engine directly: framework + widget tree emitting
 * compositor envelopes through a blocking-write pty shim over stdout.
 *
 * Every function that can fail returns a Result; `hud` must be non-NULL
 * (a NULL hud is an error, like everywhere else in the tree). The
 * intentional "no HUD" cases (CCC_NO_HUD=1, stdout not a tty) are an OK
 * result carrying a NULL value from ccc_hud_create — the caller runs
 * without a HUD and never calls the other entry points.
 */
#ifndef CCC_HUD_H
#define CCC_HUD_H

#include <stddef.h>

#include <yetty/ycore/result.h>

struct ccc_hud;
YETTY_YRESULT_DECLARE(ccc_hud_ptr, struct ccc_hud *);

/* Create the window. OK with a NULL value when the HUD is intentionally
 * unavailable (CCC_NO_HUD=1, stdout not a tty); an error Result on a
 * real init failure. */
struct ccc_hud_ptr_result ccc_hud_create(void);

/* Update one line. Cheap; nothing is written until ccc_hud_flush. */
struct yetty_ycore_void_result ccc_hud_set_state(struct ccc_hud *hud, const char *text);
struct yetty_ycore_void_result ccc_hud_set_turn(struct ccc_hud *hud, const char *text);
struct yetty_ycore_void_result ccc_hud_set_session(struct ccc_hud *hud, const char *text);

/* Emit a frame if anything changed. Flushes stdout first so the
 * envelope serializes after pending text (single-writer discipline). */
struct yetty_ycore_void_result ccc_hud_flush(struct ccc_hud *hud);

/* Authoritative pane pixel size from the host's resize envelope (the
 * tty winsize often carries no pixel fields). Re-places the window
 * top-right until the user has dragged/resized it, then only clamps. */
struct yetty_ycore_void_result ccc_hud_set_viewport(struct ccc_hud *hud, float width, float height);

/* SIGWINCH fallback: re-read TIOCGWINSZ. A no-op once the host has
 * supplied the viewport via ccc_hud_set_viewport (the host re-sends its
 * resize envelope on every pane resize, which is the better signal). */
struct yetty_ycore_void_result ccc_hud_viewport_changed(struct ccc_hud *hud);

/*---------------------------------------------------------------------------
 * Client-side input. Coordinates are pane-local pixels (the same space
 * as the framework viewport).
 *---------------------------------------------------------------------------*/

/* Button transition. The int value is 1 when the press belongs to the
 * GUI (it landed inside the window rect — widget, titlebar, or the
 * resize grip), 0 when it fell through to the terminal area. The
 * caller uses the value of a PRESS to move its focus. */
struct yetty_ycore_int_result ccc_hud_mouse_button(struct ccc_hud *hud, float x, float y,
                                                   int button, int pressed);

/* Pointer motion (drives titlebar drag and the ccc-side resize). */
struct yetty_ycore_void_result ccc_hud_mouse_motion(struct ccc_hud *hud, float x, float y);

/* Wheel scroll at (x, y). */
struct yetty_ycore_void_result ccc_hud_mouse_wheel(struct ccc_hud *hud, float x, float y,
                                                   float delta_y);

/* Hit-test: int value is 1 when (x, y) lies inside the window rect.
 * The caller uses this to decide GUI-consume vs reinject-to-host. */
struct yetty_ycore_int_result ccc_hud_contains_point(struct ccc_hud *hud, float x, float y);

/* Keyboard bytes routed to the GUI while it owns the focus. */
struct yetty_ycore_void_result ccc_hud_feed_keys(struct ccc_hud *hud, const char *bytes,
                                                 size_t len);

/* Clear the remote figure and tear the framework down. Best-effort:
 * every step runs; the first failure is surfaced at the end. */
struct yetty_ycore_void_result ccc_hud_destroy(struct ccc_hud *hud);

#endif /* CCC_HUD_H */
