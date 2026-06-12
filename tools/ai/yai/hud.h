/*
 * hud.h — yai's non-scrolling status window.
 *
 * A real ygui `window` widget (titlebar, drag, resize) floating over the
 * scroll buffer. The conversation scrolls in the terminal; the window
 * carries what must NOT scroll away: state (idle / thinking / running
 * tool), the last turn's token/cost line and the session totals.
 *
 * ALL focus and hit-testing is client-side: main.c forwards every mouse
 * event here; yai_hud_mouse_button reports whether the press landed on
 * the window (→ the GUI owns input) or fell through (→ the terminal
 * owns input). Titlebar drag is native to the window widget; the
 * resize grip (bottom-right corner) is implemented here in yai.
 *
 * Drives the ygui engine directly: framework + widget tree emitting
 * compositor envelopes through a blocking-write pty shim over stdout.
 *
 * Every function that can fail returns a Result; `hud` must be non-NULL
 * (a NULL hud is an error, like everywhere else in the tree). The
 * intentional "no HUD" cases (YAI_NO_HUD=1, stdout not a tty) are an OK
 * result carrying a NULL value from yai_hud_create — the caller runs
 * without a HUD and never calls the other entry points.
 */
#ifndef YAI_HUD_H
#define YAI_HUD_H

#include <stddef.h>

#include <yetty/ycore/result.h>

struct yai_hud;
YETTY_YRESULT_DECLARE(yai_hud_ptr, struct yai_hud *);

/* Create the window. OK with a NULL value when the HUD is intentionally
 * unavailable (YAI_NO_HUD=1, stdout not a tty); an error Result on a
 * real init failure. */
struct yai_hud_ptr_result yai_hud_create(void);

/* Update one line. Cheap; nothing is written until yai_hud_flush.
 *
 * The HUD body is split into two columns. The LEFT column carries the
 * live conversation status (set_state / set_turn / set_session); the
 * RIGHT column carries the lower-priority reference info — the active
 * model (set_model) and the session statistics (set_stats, two rows). */
struct yetty_ycore_void_result yai_hud_set_state(struct yai_hud *hud, const char *text);
struct yetty_ycore_void_result yai_hud_set_turn(struct yai_hud *hud, const char *text);
struct yetty_ycore_void_result yai_hud_set_session(struct yai_hud *hud, const char *text);
struct yetty_ycore_void_result yai_hud_set_model(struct yai_hud *hud, const char *text);
struct yetty_ycore_void_result yai_hud_set_stats(struct yai_hud *hud, const char *primary,
                                                 const char *secondary);

/* Emit a frame if anything changed. Flushes stdout first so the
 * envelope serializes after pending text (single-writer discipline). */
struct yetty_ycore_void_result yai_hud_flush(struct yai_hud *hud);

/* Pixels the docked HUD reserves at the bottom of the pane (0 when the
 * HUD is floating or NULL). yai converts this to reserved terminal
 * rows via the scroll region so text never scrolls under the bar. */
float yai_hud_dock_height(const struct yai_hud *hud);

/*---------------------------------------------------------------------------
 * Config dialog — an EDITABLE, TABBED floating panel toggled by /config.
 *
 * A tab strip across the top selects which page is shown; every page's
 * widgets are pre-built and only the active page's are visible. Pages:
 *   - "yai"      yai settings: show-thinking checkbox, fold-lines slider,
 *                edit-mode knob, plus read-only info rows.
 *   - "<engine>" backend settings: the engine's knob (sandbox / permission
 *                / approval) + its describe rows.
 *   - "model"    model selection knob for the active backend.
 *   - "stats"    read-only session statistics (like Claude Code /stats).
 *
 * Each widget is tagged with the tab index it belongs to. Read-only rows
 * ("## " lines render as accent section headers) and knobs both carry a
 * tab. main polls widget state after every GUI-owned click
 * (yai_hud_config_poll) and applies the diff.
 *---------------------------------------------------------------------------*/

#define YAI_HUD_CONFIG_KNOB_MAX_OPTIONS 8
/* edit-mode (1) + several backend knobs + model (1). */
#define YAI_HUD_CONFIG_MAX_KNOBS 6
#define YAI_HUD_CONFIG_TAB_MAX 4

/* One editable choice rendered as a radio group on tab `tab`: the yai
 * edit-mode knob, the engine knob (sandbox / permission / approval), the
 * model knob, … */
struct yai_hud_config_knob {
    const char *label; /* NULL/"" = unused slot */
    const char *options[YAI_HUD_CONFIG_KNOB_MAX_OPTIONS];
    int option_count;
    int selected;
    int tab; /* which tab (0..tab_count-1) hosts this knob */
};

struct yai_hud_config_setup {
    /* Tab strip: titles[i] for i in [0,tab_count). NULL titles end it. */
    const char *tab_titles[YAI_HUD_CONFIG_TAB_MAX];
    /* Per-tab read-only rows (newline-separated; "## " = section header).
     * NULL/"" for a tab with no info rows. */
    const char *tab_info[YAI_HUD_CONFIG_TAB_MAX];
    int tab_count;

    /* The show-thinking checkbox + fold-lines slider live on this tab.
     * -1 = do not show them at all. */
    int controls_tab;
    int show_thinking;
    float fold_lines;

    struct yai_hud_config_knob knobs[YAI_HUD_CONFIG_MAX_KNOBS];
    int knob_count;
};

struct yai_hud_config_values {
    int open; /* 0 = dialog not on screen; other fields invalid */
    int active_tab;
    int show_thinking;
    float fold_lines;
    int knob_count;
    int knob_selected[YAI_HUD_CONFIG_MAX_KNOBS]; /* per knob; -1 if absent */
};

/* Toggle the dialog: open centered with `setup`'s state, or close it
 * when already open. Built lazily on first use; widget pools reused. */
struct yetty_ycore_void_result yai_hud_toggle_config(struct yai_hud *hud,
                                                     const struct yai_hud_config_setup *setup);

/* Read the current widget state (and re-enforce radio exclusivity —
 * a radio click only selects, never unselects the rest). */
struct yetty_ycore_void_result yai_hud_config_poll(struct yai_hud *hud,
                                                   struct yai_hud_config_values *out);

/* Authoritative pane pixel size from the host's resize envelope (the
 * tty winsize often carries no pixel fields). Re-places the window
 * top-right until the user has dragged/resized it, then only clamps. */
struct yetty_ycore_void_result yai_hud_set_viewport(struct yai_hud *hud, float width, float height);

/* SIGWINCH fallback: re-read TIOCGWINSZ. A no-op once the host has
 * supplied the viewport via yai_hud_set_viewport (the host re-sends its
 * resize envelope on every pane resize, which is the better signal). */
struct yetty_ycore_void_result yai_hud_viewport_changed(struct yai_hud *hud);

/*---------------------------------------------------------------------------
 * Client-side input. Coordinates are pane-local pixels (the same space
 * as the framework viewport).
 *---------------------------------------------------------------------------*/

/* Button transition. The int value is 1 when the press belongs to the
 * GUI (it landed inside the window rect — widget, titlebar, or the
 * resize grip), 0 when it fell through to the terminal area. The
 * caller uses the value of a PRESS to move its focus. */
struct yetty_ycore_int_result yai_hud_mouse_button(struct yai_hud *hud, float x, float y,
                                                   int button, int pressed);

/* Pointer motion (drives titlebar drag and the yai-side resize). */
struct yetty_ycore_void_result yai_hud_mouse_motion(struct yai_hud *hud, float x, float y);

/* Wheel scroll at (x, y). */
struct yetty_ycore_void_result yai_hud_mouse_wheel(struct yai_hud *hud, float x, float y,
                                                   float delta_y);

/* Hit-test: int value is 1 when (x, y) lies inside the window rect.
 * The caller uses this to decide GUI-consume vs reinject-to-host. */
struct yetty_ycore_int_result yai_hud_contains_point(struct yai_hud *hud, float x, float y);

/* Keyboard bytes routed to the GUI while it owns the focus. */
struct yetty_ycore_void_result yai_hud_feed_keys(struct yai_hud *hud, const char *bytes,
                                                 size_t len);

/* Clear the remote figure and tear the framework down. Best-effort:
 * every step runs; the first failure is surfaced at the end. */
struct yetty_ycore_void_result yai_hud_destroy(struct yai_hud *hud);

#endif /* YAI_HUD_H */
