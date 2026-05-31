#ifndef YETTY_YGUI_THEME_H
#define YETTY_YGUI_THEME_H

#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward decl — kept opaque to consumers of widget headers. */
struct yetty_yconfig_config;

/*
 * Theme — single source of truth for chrome colors + a handful of sizes
 * that the ygui paint pipeline consults at render time.
 *
 * Colors are packed in the canonical yetty layout: byte 0 = R, byte 1 =
 * G, byte 2 = B, byte 3 = A. On little-endian that's `0xAABBGGRR` as a
 * u32 literal. Matches yetty_ycore_parse_hex_color, the ydraw shaders'
 * `unpack_color`, and every hardcoded literal in the widget tree.
 *
 * Two namespaces share this struct:
 *
 *   ygui_*       general widget palette (button surfaces, panel bg,
 *                table rows, text, borders, …). Overridable via the
 *                config block at `style.ygui.*`.
 *
 *   yui_*        yetty's own window chrome (tab strip background, the
 *                pill colors for active vs inactive tabs, the
 *                close/minimize/maximize button strip background, the
 *                accent bar under the active tab). Overridable via
 *                `style.yui.*`.
 *
 * Sizes (pad_*, radius_*, row_height, font_size, …) are in *logical*
 * units. Callers that draw at HiDPI multiply through
 * yetty_ygui_framework_content_scale at paint time.
 *
 * Allocation: yetty_ygui_theme_create_default() returns a malloc'd
 * struct populated with the yetty brand palette. The engine owns its
 * theme by default; yetty_ygui_framework_set_theme transfers ownership
 * to the caller iff they constructed a replacement.
 */
struct yetty_ygui_theme {
    /*---- ygui general palette (style.ygui.*) ------------------------*/
    uint32_t bg_primary;   /* base canvas bg                         */
    uint32_t bg_secondary; /* "lifted" surface (panels, dropdowns)   */
    uint32_t bg_surface;   /* row / cell surface                     */
    uint32_t bg_hover;     /* hovered row / button                   */
    uint32_t bg_header;    /* table headers                          */
    uint32_t bg_dropdown;  /* dropdown / popup body                  */

    uint32_t border;       /* hairline border                        */
    uint32_t border_light; /* lighter ladder step                    */
    uint32_t border_muted; /* even lighter — text-muted equivalent   */

    uint32_t text_primary; /* main text                              */
    uint32_t text_muted;   /* secondary / disabled                   */

    uint32_t accent;        /* brand accent (mint / copper-oxide)    */
    uint32_t accent_bright; /* hover outline / pressed glow          */
    uint32_t selection_bg;  /* selected row / range                  */

    uint32_t thumb_normal; /* scrollbar thumb                        */
    uint32_t thumb_hover;  /* scrollbar thumb hovered                */

    uint32_t overlay_modal; /* dimmer behind modal popups            */
    uint32_t shadow;        /* drop shadows                          */
    uint32_t tooltip_bg;    /* tooltip background                    */

    /*---- yui chrome palette (style.yui.*) ---------------------------*/
    uint32_t yui_strip;        /* tab strip background               */
    uint32_t yui_tab_active;   /* selected pill body                 */
    uint32_t yui_tab_inactive; /* unselected pill body               */
    uint32_t yui_accent;       /* 3px bar under the active pill      */
    uint32_t yui_glyph;        /* button glyph color (×, −, □, ≡)    */
    uint32_t yui_pill;         /* "v" / "+" pill background          */
    uint32_t yui_winbtn_bg;    /* min/max button background          */
    uint32_t yui_close_bg;     /* close button (red tint)            */
    uint32_t yui_popup_body;   /* "v" popup menu body                */
    uint32_t yui_popup_row;    /* popup menu row                     */

    /*---- Sizes (logical units) --------------------------------------*/
    float pad_small;
    float pad_medium;
    float pad_large;

    float radius_small;
    float radius_medium;
    float radius_large;

    float row_height;
    float font_size;
    float scrollbar_size;
    float separator_size;
};

/* Allocate + populate with the yetty brand defaults. Caller owns the
 * pointer; free via yetty_ygui_theme_destroy. Returns NULL on OOM. */
struct yetty_ygui_theme *yetty_ygui_theme_create_default(void);

/* Free a theme created by yetty_ygui_theme_create_default. NULL-safe. */
void yetty_ygui_theme_destroy(struct yetty_ygui_theme *theme);

/* Overlay any non-empty `style.ygui.*` / `style.yui.*` keys from
 * `config` onto `theme`. Missing keys and malformed hex strings leave
 * the existing field untouched, so the brand defaults survive. */
struct yetty_ycore_void_result yetty_ygui_theme_apply_config(
    struct yetty_ygui_theme *theme, const struct yetty_yconfig_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_THEME_H */
