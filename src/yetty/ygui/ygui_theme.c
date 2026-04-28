/*
 * ygui_theme.c - Theme implementation
 */

#include "ygui_internal.h"

/*=============================================================================
 * Theme Lifecycle
 *===========================================================================*/

ygui_theme_t *ygui_theme_create(void)
{
    ygui_theme_t *theme = (ygui_theme_t *)calloc(1, sizeof(ygui_theme_t));
    if (!theme) {
        ygui_set_error("Failed to allocate theme");
        return NULL;
    }
    return theme;
}

ygui_theme_t *ygui_theme_create_default(void)
{
    ygui_theme_t *theme = ygui_theme_create();
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

void ygui_theme_destroy(ygui_theme_t *theme)
{
    free(theme);
}

/*=============================================================================
 * Theme Setters
 *===========================================================================*/

void ygui_theme_set_padding(ygui_theme_t *theme, float sm, float med, float lg)
{
    if (!theme) {
        return;
    }
    theme->pad_small = sm;
    theme->pad_medium = med;
    theme->pad_large = lg;
}

void ygui_theme_set_radius(ygui_theme_t *theme, float sm, float med, float lg)
{
    if (!theme) {
        return;
    }
    theme->radius_small = sm;
    theme->radius_medium = med;
    theme->radius_large = lg;
}

void ygui_theme_set_row_height(ygui_theme_t *theme, float height)
{
    if (!theme) {
        return;
    }
    theme->row_height = height;
}

void ygui_theme_set_font_size(ygui_theme_t *theme, float size)
{
    if (!theme) {
        return;
    }
    theme->font_size = size;
}

void ygui_theme_set_scrollbar_size(ygui_theme_t *theme, float size)
{
    if (!theme) {
        return;
    }
    theme->scrollbar_size = size;
}

void ygui_theme_set_bg_primary(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_primary = color;
}

void ygui_theme_set_bg_surface(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_surface = color;
}

void ygui_theme_set_bg_hover(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_hover = color;
}

void ygui_theme_set_text_primary(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->text_primary = color;
}

void ygui_theme_set_text_muted(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->text_muted = color;
}

void ygui_theme_set_accent(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->accent = color;
}

void ygui_theme_set_border(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->border = color;
}

void ygui_theme_set_border_muted(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->border_muted = color;
}

void ygui_theme_set_bg_dropdown(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->bg_dropdown = color;
}

void ygui_theme_set_overlay_modal(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->overlay_modal = color;
}

void ygui_theme_set_shadow(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->shadow = color;
}

void ygui_theme_set_tooltip_bg(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->tooltip_bg = color;
}

void ygui_theme_set_selection_bg(ygui_theme_t *theme, uint32_t color)
{
    if (!theme) {
        return;
    }
    theme->selection_bg = color;
}
