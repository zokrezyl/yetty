/*
 * app.h — central yperf application state.
 *
 * `struct yperf_app` owns the parsed profile, the reused `yflame` object that
 * renders the flame graph, the symbol-table cursor + sort, and the ygui engine
 * and widget tree. Heap-allocated once in main() and threaded by pointer
 * through the harness and the UI, mirroring ytop/ydu.
 */
#ifndef YPERF_APP_H
#define YPERF_APP_H

#include "profile.h"

struct yetty_yclass_object;
struct yperf_ui;

struct yperf_app {
    /* ygui engine + root of the widget tree. */
    struct yetty_yclass_object *engine;
    struct yetty_yclass_object *root_widget;

    /* Host display's HiDPI factor (framebuffer px / logical px), learned from
     * yetty_client_input_resize.content_scale. The host sends pane size and
     * pointer coords in FRAMEBUFFER px while ygui lays out in LOGICAL px and
     * the host's yscene scales absolute-coords figures back up by this — so
     * divide once at each inbound boundary. 1.0 until the first RESIZE
     * envelope, and for a host too old to populate the field. */
    float content_scale;
    struct yperf_ui *ui; /* opaque widget handles, owned by ui.c */

    /* Flame render + profile model. */
    struct yetty_yclass_object *flame; /* yflame object, reconfigured each render */
    struct yperf_profile *profile;

    /* Symbol-table interaction. */
    int selected; /* index into profile->symbols, in current sort order */
    enum yperf_sort_mode sort_mode;
    int icicle; /* 0 = flame (root at bottom), 1 = icicle (root at top) */
    char source[1024];

    /* Free-text search. When editing, keystrokes build `search`; the matching
     * frames are highlighted in the flame. An empty query falls back to
     * cross-highlighting the selected symbol. */
    int search_active;
    char search[128];
    size_t search_len;

    /* Filter drill-down: `profile` is reduced to stacks containing `filter`
     * (rebuilt from `orig_profile`'s folded text). `orig_profile` is the full,
     * unfiltered capture — restored verbatim (keeping its timeline) when the
     * filter is cleared. When no filter is active, `profile == orig_profile`.
     * Empty `filter` = no filter. */
    char filter[256];
    struct yperf_profile *orig_profile;

    /* Diff mode: baseline folded text handed to the flame as a set_baseline
     * after every (re)parse; frames colour by delta. NULL = no diff. */
    char *baseline_folded;
    size_t baseline_folded_len;

    /* One-shot startup zoom from --focus <symbol>; applied once after the first
     * flame build, then cleared. Empty = none. */
    char pending_focus[256];

    int running;
};

#endif /* YPERF_APP_H */
