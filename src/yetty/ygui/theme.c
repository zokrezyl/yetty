/*
 * theme.c — ygui theme implementation.
 *
 * Mirrors the structure of the pre-"New gui" ygui_theme.c (see
 * 8f1211d^:src/yetty/ygui/ygui_theme.c). The 8f1211d rewrite removed
 * the theme path entirely, leaving every widget pinned to private
 * COLOR_* macros, which silently disabled all style.* config overrides.
 * This module brings that path back: defaults populated here, config
 * keys overlaid via apply_config, widgets consult engine->theme at
 * paint time.
 */

#include <yetty/ygui/theme.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/util.h>

#include <stdlib.h>

struct yetty_ygui_theme *yetty_ygui_theme_create_default(void)
{
    struct yetty_ygui_theme *t = (struct yetty_ygui_theme *)calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }

    /* Brand defaults — match the historical values used across the
     * widget tree before the rewrite, and the documented defaults in
     * build-tools/yetty/platform/config-defaults.yaml.
     *
     * Packing is yetty-canonical (byte 0 = R, byte 1 = G, byte 2 = B,
     * byte 3 = A), so #1E262C → 0xFF2C261E and #6BA892 → 0xFF92A86B. */

    /* ygui general — ladder of dark cool grays + mint accent. */
    t->bg_primary = 0xFF14100Bu;   /* #0B1014 */
    t->bg_secondary = 0xFF1F1A14u; /* #141A1F */
    t->bg_surface = 0xFF2C261Eu;   /* #1E262C */
    t->bg_hover = 0xFF38342Au;     /* #2A3438 */
    t->bg_header = 0xFF474A36u;    /* #364A47 */
    t->bg_dropdown = 0xFF1F1A14u;  /* #141A1F */

    t->border = 0xFF474A36u;       /* #364A47 */
    t->border_light = 0xFF505545u; /* #455550 */
    t->border_muted = 0xFF626155u; /* #556162 */

    t->text_primary = 0xFFE4E5E0u; /* #E0E5E4 */
    t->text_muted = 0xFFA8A79Fu;   /* #9FA7A8 */

    t->accent = 0xFF92A86Bu;        /* #6BA892 mint / copper-oxide */
    t->accent_bright = 0xFFA5C574u; /* slightly brighter mint      */
    t->selection_bg = 0xFF79895Au;  /* #5A8979 deeper mint         */

    t->thumb_normal = 0xFF474A36u; /* #364A47 */
    t->thumb_hover = 0xFF626155u;  /* #556162 */

    t->overlay_modal = 0x80000000u;
    t->shadow = 0x40000000u;
    t->tooltip_bg = 0xF01F1A14u; /* near-opaque BG_LIFTED */

    /* yui chrome — strip is the darker rung, tab-inactive one step
     * lighter so unselected pills are visible against the strip; the
     * tab-active is the darkest rung so the selected pill merges with
     * the workspace bg. */
    t->yui_strip = 0xFF1F1A14u;        /* #141A1F */
    t->yui_tab_active = 0xFF14100Bu;   /* #0B1014 */
    t->yui_tab_inactive = 0xFF2C261Eu; /* #1E262C */
    t->yui_accent = 0xFF92A86Bu;       /* #6BA892 mint underline */
    t->yui_glyph = 0xFFE4E5E0u;        /* #E0E5E4 */
    t->yui_pill = 0xFF474A36u;         /* #364A47 */
    t->yui_winbtn_bg = 0xFF1F1A14u;    /* #141A1F */
    t->yui_close_bg = 0xFF1C1A59u;     /* #591A1C red-ish */
    t->yui_popup_body = 0xFF1F1A14u;   /* #141A1F */
    t->yui_popup_row = 0xFF2C261Eu;    /* #1E262C */

    /* Sizes — logical units. Same numbers the old theme used. */
    t->pad_small = 2.0f;
    t->pad_medium = 4.0f;
    t->pad_large = 8.0f;

    t->radius_small = 2.0f;
    t->radius_medium = 4.0f;
    t->radius_large = 8.0f;

    t->row_height = 24.0f;
    t->font_size = 14.0f;
    t->scrollbar_size = 12.0f;
    t->separator_size = 1.0f;

    return t;
}

void yetty_ygui_theme_destroy(struct yetty_ygui_theme *theme)
{
    free(theme);
}

/* Read a hex color from `config` at `path`. Missing keys and malformed
 * strings leave `*dst` untouched so the caller's default survives. */
static void apply_color(uint32_t *dst, const struct yetty_yconfig_config *config, const char *path)
{
    if (!config || !config->ops || !config->ops->get_string) {
        return;
    }
    const char *s = config->ops->get_string(config, path, NULL);
    if (!s || !*s) {
        return;
    }
    uint32_t v = 0;
    if (yetty_ycore_parse_hex_color(s, &v)) {
        *dst = v;
    }
}

/* Read a float from `config` at `path`. Same behavior as apply_color
 * for missing / malformed values: dst untouched. Uses get_string +
 * strtof so a key written as `font-size: 14.5` lands as 14.5f. */
static void apply_float(float *dst, const struct yetty_yconfig_config *config, const char *path)
{
    if (!config || !config->ops || !config->ops->get_string) {
        return;
    }
    const char *s = config->ops->get_string(config, path, NULL);
    if (!s || !*s) {
        return;
    }
    char *end = NULL;
    float v = strtof(s, &end);
    if (end != s && v > 0.0f) {
        *dst = v;
    }
}

struct yetty_ycore_void_result yetty_ygui_theme_apply_config(
    struct yetty_ygui_theme *theme, const struct yetty_yconfig_config *config)
{
    if (!theme) {
        return YETTY_ERR(yetty_ycore_void, "theme_apply_config: NULL theme");
    }
    if (!config) {
        return YETTY_OK_VOID(); /* nothing to overlay; defaults stand */
    }

    /* style.ygui.* — general widget palette. */
    apply_color(&theme->bg_primary, config, "style/ygui/bg-primary");
    apply_color(&theme->bg_secondary, config, "style/ygui/bg-secondary");
    apply_color(&theme->bg_surface, config, "style/ygui/bg-surface");
    apply_color(&theme->bg_hover, config, "style/ygui/bg-hover");
    apply_color(&theme->bg_header, config, "style/ygui/bg-header");
    apply_color(&theme->bg_dropdown, config, "style/ygui/bg-dropdown");

    apply_color(&theme->border, config, "style/ygui/border");
    apply_color(&theme->border_light, config, "style/ygui/border-light");
    apply_color(&theme->border_muted, config, "style/ygui/border-muted");

    apply_color(&theme->text_primary, config, "style/ygui/text-primary");
    apply_color(&theme->text_muted, config, "style/ygui/text-muted");

    apply_color(&theme->accent, config, "style/ygui/accent");
    apply_color(&theme->selection_bg, config, "style/ygui/selection-bg");

    apply_color(&theme->thumb_normal, config, "style/ygui/thumb-normal");
    apply_color(&theme->thumb_hover, config, "style/ygui/thumb-hover");

    apply_color(&theme->overlay_modal, config, "style/ygui/overlay-modal");
    apply_color(&theme->shadow, config, "style/ygui/shadow");
    apply_color(&theme->tooltip_bg, config, "style/ygui/tooltip-bg");

    /* style.yui.* — yetty's window chrome. */
    apply_color(&theme->yui_strip, config, "style/yui/strip");
    apply_color(&theme->yui_tab_active, config, "style/yui/tab-active");
    apply_color(&theme->yui_tab_inactive, config, "style/yui/tab-inactive");
    apply_color(&theme->yui_accent, config, "style/yui/accent");
    apply_color(&theme->yui_glyph, config, "style/yui/glyph");
    apply_color(&theme->yui_pill, config, "style/yui/pill");
    apply_color(&theme->yui_winbtn_bg, config, "style/yui/winbtn-bg");
    apply_color(&theme->yui_close_bg, config, "style/yui/close-bg");
    apply_color(&theme->yui_popup_body, config, "style/yui/popup-body");
    apply_color(&theme->yui_popup_row, config, "style/yui/popup-row");

    /* Numeric scalar overrides — currently just font-size; pad/radius
     * keys can join here as the design evolves. */
    apply_float(&theme->font_size, config, "style/ygui/font-size");

    return YETTY_OK_VOID();
}
