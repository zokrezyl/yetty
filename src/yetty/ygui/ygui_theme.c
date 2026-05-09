/*
 * ygui_theme.c - Theme implementation
 */

#include "ygui_internal.h"

/*=============================================================================
 * Theme Lifecycle
 *===========================================================================*/

struct yetty_ygui_theme *yetty_ygui_theme_create(void)
{
    struct yetty_ygui_theme *theme =
        (struct yetty_ygui_theme *)calloc(1, sizeof(struct yetty_ygui_theme));
    if (!theme) {
        yetty_ygui_set_error("Failed to allocate theme");
        return NULL;
    }
    return theme;
}

struct yetty_ygui_theme *yetty_ygui_theme_create_default(void)
{
    struct yetty_ygui_theme *theme = yetty_ygui_theme_create();
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

    /* Colors (ABGR format) */
    theme->bg_primary = 0xFF1A1A2E;
    theme->bg_secondary = 0xFF222233;
    theme->bg_surface = 0xFF2A2A3E;
    theme->bg_hover = 0xFF333344;
    theme->bg_header = 0xFF3A3A4E;
    theme->bg_dropdown = 0xFF1E1E2E;
    theme->border = 0xFF444455;
    theme->border_light = 0xFF555566;
    theme->border_muted = 0xFF666677;
    theme->text_primary = 0xFFFFFFFF;
    theme->text_muted = 0xFFAAAAAA;
    theme->accent = 0xFF4488FF;
    theme->thumb_normal = 0xFF444455;
    theme->thumb_hover = 0xFF555566;
    theme->overlay_modal = 0x80000000;
    theme->shadow = 0x40000000;
    theme->tooltip_bg = 0xF0222233;
    theme->selection_bg = 0xFF2244AA;

    return theme;
}

void yetty_ygui_theme_destroy(struct yetty_ygui_theme *theme)
{
    free(theme);
}

/*=============================================================================
 * Theme Setters
 *===========================================================================*/

void yetty_ygui_theme_set_padding(struct yetty_ygui_theme *theme, float sm, float med, float lg)
{
    if (!theme) {
        return;
    }
    theme->pad_small = sm;
    theme->pad_medium = med;
    theme->pad_large = lg;
}

void yetty_ygui_theme_set_radius(struct yetty_ygui_theme *theme, float sm, float med, float lg)
{
    if (!theme) {
        return;
    }
    theme->radius_small = sm;
    theme->radius_medium = med;
    theme->radius_large = lg;
}

void yetty_ygui_theme_set_row_height(struct yetty_ygui_theme *theme, float height)
{
    if (!theme) {
        return;
    }
    theme->row_height = height;
}

void yetty_ygui_theme_set_font_size(struct yetty_ygui_theme *theme, float size)
{
    if (!theme) {
        return;
    }
    theme->font_size = size;
}

void yetty_ygui_theme_set_scrollbar_size(struct yetty_ygui_theme *theme, float size)
{
    if (!theme) {
        return;
    }
    theme->scrollbar_size = size;
}

void yetty_ygui_theme_set_bg_primary(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_primary = color;
}

void yetty_ygui_theme_set_bg_surface(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_surface = color;
}

void yetty_ygui_theme_set_bg_hover(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_hover = color;
}

void yetty_ygui_theme_set_text_primary(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->text_primary = color;
}

void yetty_ygui_theme_set_text_muted(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->text_muted = color;
}

void yetty_ygui_theme_set_accent(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->accent = color;
}

void yetty_ygui_theme_set_border(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->border = color;
}

void yetty_ygui_theme_set_border_muted(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->border_muted = color;
}

void yetty_ygui_theme_set_bg_dropdown(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_dropdown = color;
}

void yetty_ygui_theme_set_overlay_modal(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->overlay_modal = color;
}

void yetty_ygui_theme_set_shadow(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->shadow = color;
}

void yetty_ygui_theme_set_tooltip_bg(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->tooltip_bg = color;
}

void yetty_ygui_theme_set_selection_bg(struct yetty_ygui_theme *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->selection_bg = color;
}

void yetty_ygui_theme_set_elevation(struct yetty_ygui_theme *theme, float low, float medium,
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

void yetty_ygui_theme_set_gradient(struct yetty_ygui_theme *theme, int enable)
{
    if (!theme) {
        return;
    }
    theme->enable_gradient = enable ? 1 : 0;
}
