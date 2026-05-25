/*
 * ygui_theme.c - Theme implementation
 */

#include "ygui_internal.h"

#include <yetty/yconfig/config.h>
#include <yetty/ycore/util.h>

/*=============================================================================
 * Theme Lifecycle
 *===========================================================================*/

struct yetty_ygui_old_theme *yetty_ygui_old_theme_create(void)
{
    struct yetty_ygui_old_theme *theme =
        (struct yetty_ygui_old_theme *)calloc(1, sizeof(struct yetty_ygui_old_theme));
    if (!theme) {
        yetty_ygui_old_set_error("Failed to allocate theme");
        return NULL;
    }
    return theme;
}

struct yetty_ygui_old_theme *yetty_ygui_old_theme_create_default(void)
{
    struct yetty_ygui_old_theme *theme = yetty_ygui_old_theme_create();
    if (!theme) {
        return NULL;
    }

    /* Spacing */
    theme->pad_small = 2.0f;
    theme->pad_medium = 4.0f;
    theme->pad_large = 8.0f;

    /* Corner radius */
    theme->radius_small = 2.0f;
    theme->radius_medium = 4.0f;
    theme->radius_large = 8.0f;

    /* Sizing */
    theme->row_height = 24.0f;
    theme->scrollbar_size = 12.0f;
    theme->scroll_speed = 20.0f;
    theme->font_size = 14.0f;
    theme->separator_size = 1.0f;

    /* Elevation (Material-style soft shadows). */
    theme->elevation_low = 1.5f;
    theme->elevation_medium = 4.0f;
    theme->elevation_high = 8.0f;
    theme->elevation_alpha = 0.55f;
    theme->enable_gradient = 1;

    /* Colors (ABGR format — byte order R, G, B, A; literals are written
     * 0xAABBGGRR). Yetty brand palette, Each role is the brand equivalent of the previous
     * brown ladder, preserving the visual ordering (darker bg → lighter
     * surface, darker border → lighter muted border).
     *
     *   bg_primary    BRAND_BG          #0B1014
     *   bg_secondary  BRAND_BG_LIFTED   #141A1F
     *   bg_surface    BRAND_BG_ROW     #1E262C
     *   bg_hover      tween             #2A3438
     *   bg_header     BRAND_BORDER      #364A47
     *   bg_dropdown   BRAND_BG_LIFTED   #141A1F
     *   border        BRAND_BORDER      #364A47
     *   border_light  tween             #455550
     *   border_muted  BRAND_TEXT_MUTED  #556162
     *   text_primary  BRAND_TEXT_PRIMARY #E0E5E4
     *   text_muted    BRAND_TEXT_SECONDARY #9FA7A8
     *   accent        BRAND_ACCENT      #6BA892
     *   thumb_normal  BRAND_BORDER      #364A47
     *   thumb_hover   BRAND_TEXT_MUTED  #556162
     *   selection_bg  BRAND_ACCENT_DEEP #5A8979
     *   tooltip_bg    BRAND_BG_LIFTED @ alpha 0xF0
     */
    theme->bg_primary = 0xFF14100B;
    theme->bg_secondary = 0xFF1F1A14;
    theme->bg_surface = 0xFF2C261E;
    theme->bg_hover = 0xFF38342A;
    theme->bg_header = 0xFF474A36;
    theme->bg_dropdown = 0xFF1F1A14;
    theme->border = 0xFF474A36;
    theme->border_light = 0xFF505545;
    theme->border_muted = 0xFF626155;
    theme->text_primary = 0xFFE4E5E0;
    theme->text_muted = 0xFFA8A79F;
    theme->accent = 0xFF92A86B;
    theme->thumb_normal = 0xFF474A36;
    theme->thumb_hover = 0xFF626155;
    theme->overlay_modal = 0x80000000;
    theme->shadow = 0x40000000;
    theme->tooltip_bg = 0xF01F1A14;
    theme->selection_bg = 0xFF79895A;

    return theme;
}

void yetty_ygui_old_theme_destroy(struct yetty_ygui_old_theme *theme)
{
    free(theme);
}

/*=============================================================================
 * Theme Setters
 *===========================================================================*/

void yetty_ygui_old_theme_set_padding(struct yetty_ygui_old_theme *theme, float sm, float med, float lg)
{
    if (!theme) {
        return;
    }
    theme->pad_small = sm;
    theme->pad_medium = med;
    theme->pad_large = lg;
}

void yetty_ygui_old_theme_set_radius(struct yetty_ygui_old_theme *theme, float sm, float med, float lg)
{
    if (!theme) {
        return;
    }
    theme->radius_small = sm;
    theme->radius_medium = med;
    theme->radius_large = lg;
}

void yetty_ygui_old_theme_set_row_height(struct yetty_ygui_old_theme *theme, float height)
{
    if (!theme) {
        return;
    }
    theme->row_height = height;
}

void yetty_ygui_old_theme_set_font_size(struct yetty_ygui_old_theme *theme, float size)
{
    if (!theme) {
        return;
    }
    theme->font_size = size;
}

void yetty_ygui_old_theme_set_scrollbar_size(struct yetty_ygui_old_theme *theme, float size)
{
    if (!theme) {
        return;
    }
    theme->scrollbar_size = size;
}

void yetty_ygui_old_theme_set_bg_primary(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_primary = color;
}

void yetty_ygui_old_theme_set_bg_surface(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_surface = color;
}

void yetty_ygui_old_theme_set_bg_hover(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_hover = color;
}

void yetty_ygui_old_theme_set_text_primary(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->text_primary = color;
}

void yetty_ygui_old_theme_set_text_muted(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->text_muted = color;
}

void yetty_ygui_old_theme_set_accent(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->accent = color;
}

void yetty_ygui_old_theme_set_border(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->border = color;
}

void yetty_ygui_old_theme_set_border_muted(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->border_muted = color;
}

void yetty_ygui_old_theme_set_bg_dropdown(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_dropdown = color;
}

void yetty_ygui_old_theme_set_overlay_modal(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->overlay_modal = color;
}

void yetty_ygui_old_theme_set_shadow(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->shadow = color;
}

void yetty_ygui_old_theme_set_tooltip_bg(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->tooltip_bg = color;
}

void yetty_ygui_old_theme_set_selection_bg(struct yetty_ygui_old_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->selection_bg = color;
}

void yetty_ygui_old_theme_set_elevation(struct yetty_ygui_old_theme *theme, float low, float medium,
                                    float high, float alpha)
{
    if (!theme) {
        return;
    }
    theme->elevation_low = low;
    theme->elevation_medium = medium;
    theme->elevation_high = high;
    theme->elevation_alpha = alpha;
}

void yetty_ygui_old_theme_set_gradient(struct yetty_ygui_old_theme *theme, int enable)
{
    if (!theme) {
        return;
    }
    theme->enable_gradient = enable ? 1 : 0;
}

/*=============================================================================
 * Config-driven overlay
 *===========================================================================*/

/* Read one hex color string from `config` at `path`. On parse success, write
 * the decoded ABGR u32 into `*dst`. Missing keys and malformed strings leave
 * `*dst` untouched so the caller's default survives. */
static void apply_color(struct yetty_ygui_old_theme *theme, uint32_t *dst,
                        const struct yetty_yconfig_config *config, const char *path)
{
    (void)theme;
    const char *s = config->ops->get_string(config, path, NULL);
    if (!s || !*s) {
        return;
    }
    uint32_t v = 0;
    if (yetty_ycore_parse_hex_color(s, &v)) {
        *dst = v;
    }
}

void yetty_ygui_old_theme_apply_config(struct yetty_ygui_old_theme *theme,
                                   const struct yetty_yconfig_config *config)
{
    if (!theme || !config || !config->ops || !config->ops->get_string) {
        return;
    }

    /* Background ladder. */
    apply_color(theme, &theme->bg_primary, config, "style/ygui/bg-primary");
    apply_color(theme, &theme->bg_secondary, config, "style/ygui/bg-secondary");
    apply_color(theme, &theme->bg_surface, config, "style/ygui/bg-surface");
    apply_color(theme, &theme->bg_hover, config, "style/ygui/bg-hover");
    apply_color(theme, &theme->bg_header, config, "style/ygui/bg-header");
    apply_color(theme, &theme->bg_dropdown, config, "style/ygui/bg-dropdown");

    /* Borders. */
    apply_color(theme, &theme->border, config, "style/ygui/border");
    apply_color(theme, &theme->border_light, config, "style/ygui/border-light");
    apply_color(theme, &theme->border_muted, config, "style/ygui/border-muted");

    /* Text. */
    apply_color(theme, &theme->text_primary, config, "style/ygui/text-primary");
    apply_color(theme, &theme->text_muted, config, "style/ygui/text-muted");

    /* Accent + selection. */
    apply_color(theme, &theme->accent, config, "style/ygui/accent");
    apply_color(theme, &theme->selection_bg, config, "style/ygui/selection-bg");

    /* Scrollbar thumb. */
    apply_color(theme, &theme->thumb_normal, config, "style/ygui/thumb-normal");
    apply_color(theme, &theme->thumb_hover, config, "style/ygui/thumb-hover");

    /* Overlays. */
    apply_color(theme, &theme->overlay_modal, config, "style/ygui/overlay-modal");
    apply_color(theme, &theme->shadow, config, "style/ygui/shadow");
    apply_color(theme, &theme->tooltip_bg, config, "style/ygui/tooltip-bg");
}
