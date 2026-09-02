/* ygui2 — shared plain-data definitions (hand-written; everything else in
 * the module's public API is codegen-generated from the annotated .c).
 *
 * ygui2 is the drawable-contract widget toolkit (src/yetty/ygui2/strategy.md):
 * the widget tree projects onto the yvterm group tree, rendering is
 * exclusively DCS drawable envelopes, and updates are incremental — offsets
 * move widgets, per-widget reopens repaint them, complexes stream. */
#ifndef YETTY_YGUI2_DEFS_H
#define YETTY_YGUI2_DEFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flex layout parameters of one widget. The framework's layout pass fills
 * widget rects from these — all client-side; the wire only ever sees the
 * resulting offsets. Sizes are logical pixels. */
enum yetty_ygui2_direction {
    YETTY_YGUI2_DIRECTION_COLUMN = 0,
    YETTY_YGUI2_DIRECTION_ROW = 1,
};

struct yetty_ygui2_layout {
    /* Main-axis flex: fixed basis (px, 0 = content/none) + grow weight. */
    float basis;
    float grow;
    /* Cross-axis: 0 = stretch to the container's content box. */
    float cross_size;
    float min_main;
    /* Container fields (used when the widget has children). */
    uint32_t direction; /* enum yetty_ygui2_direction */
    float gap;
    float pad_left, pad_top, pad_right, pad_bottom;
};

/* Key codes delivered to widget on_key and the app key callback. Printable
 * and C0 bytes pass through verbatim (so ENTER/TAB/ESCAPE/BACKSPACE are
 * their byte values); CSI specials map above 255. */
enum yetty_ygui2_key {
    YETTY_YGUI2_KEY_TAB = 9,
    YETTY_YGUI2_KEY_ENTER = 13,
    YETTY_YGUI2_KEY_ESCAPE = 27,
    YETTY_YGUI2_KEY_BACKSPACE = 127,
    YETTY_YGUI2_KEY_UP = 256,
    YETTY_YGUI2_KEY_DOWN = 257,
    YETTY_YGUI2_KEY_RIGHT = 258,
    YETTY_YGUI2_KEY_LEFT = 259,
};

/* Shared palette, packed wire color format (0xAABBGGRR — the SDF fill
 * word). Zero-valued widget color overrides fall back to these roles; the
 * defaults are the brand palette. */
struct yetty_ygui2_theme {
    uint32_t bg;             /* canvas / darkest surface */
    uint32_t bg_lifted;      /* raised surface (popup body, input field) */
    uint32_t bg_row;         /* row / hover / inactive surface */
    uint32_t border;         /* separators, outlines */
    uint32_t text_muted;     /* disabled / tertiary text */
    uint32_t text_secondary; /* secondary text */
    uint32_t text_primary;   /* primary labels */
    uint32_t accent_deep;    /* subtle accent (glow, done-state) */
    uint32_t accent;         /* the brand accent — primary actions */
    uint32_t accent_bright;  /* hover / focus highlight */
};

/* Sink: where emitted DCS envelopes go when no write_fd is attached —
 * headless tests capture here, in-process hosts feed their own ingest. */
typedef void (*yetty_ygui2_sink_fn)(const uint8_t *bytes, size_t byte_count, void *userdata);

struct yetty_yclass_object;

/* Click callback (button and clickable widgets). */
typedef void (*yetty_ygui2_click_cb)(struct yetty_yclass_object *widget, void *userdata);

/* Item-selection callback (popup_menu, dropdown). */
typedef void (*yetty_ygui2_select_cb)(struct yetty_yclass_object *widget, uint32_t index,
                                      void *userdata);

/* App-level key callback (compatibility shape with ygui): return nonzero
 * to consume. */
typedef int (*yetty_ygui2_key_cb)(uint32_t key, uint32_t mods, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI2_DEFS_H */
