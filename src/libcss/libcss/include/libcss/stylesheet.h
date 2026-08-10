/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef libcss_stylesheet_h_
#define libcss_stylesheet_h_

#ifdef __cplusplus
extern "C" {
#endif

#include <libcss/errors.h>
#include <libcss/types.h>
#include <libcss/properties.h>

/**
 * Callback to resolve an URL
 *
 * \param pw    Client data
 * \param dict  String internment context
 * \param base  Base URI (absolute)
 * \param rel   URL to resolve, either absolute or relative to base
 * \param abs   Pointer to location to receive result
 * \return CSS_OK on success, appropriate error otherwise.
 */
typedef css_error (*css_url_resolution_fn)(void *pw, const char *base, lwc_string *rel,
                                           lwc_string **abs);

/**
 * Callback to be notified of the need for an imported stylesheet
 *
 * \param pw      Client data
 * \param parent  Stylesheet requesting the import
 * \param url     URL of the imported sheet
 * \return CSS_OK on success, appropriate error otherwise
 *
 * \note This function will be invoked for notification purposes
 *       only. The client may use this to trigger a parallel fetch
 *       of the imported stylesheet. The imported sheet must be
 *       registered with its parent using the post-parse import
 *       registration API.
 */
typedef css_error (*css_import_notification_fn)(void *pw, css_stylesheet *parent, lwc_string *url);

/**
 * Callback use to resolve system colour names to RGB values
 *
 * \param pw     Client data
 * \param name   System colour name
 * \param color  Pointer to location to receive color value
 * \return CSS_OK       on success,
 *         CSS_INVALID  if the name is unknown.
 */
typedef css_error (*css_color_resolution_fn)(void *pw, lwc_string *name, css_color *color);

/** System font callback result data. */
typedef struct css_system_font {
    enum css_font_style_e style;
    enum css_font_variant_e variant;
    enum css_font_weight_e weight;
    struct {
        css_fixed size;
        css_unit unit;
    } size;
    struct {
        css_fixed size;
        css_unit unit;
    } line_height;
    /* Note: must be a single family name only */
    lwc_string *family;
} css_system_font;

/**
 * Callback use to resolve system font names to font values
 *
 * \param pw           Client data
 * \param name         System font identifier
 * \param system_font  Pointer to system font descriptor to be filled
 * \return CSS_OK       on success,
 *         CSS_INVALID  if the name is unknown.
 */
typedef css_error (*css_font_resolution_fn)(void *pw, lwc_string *name,
                                            css_system_font *system_font);

typedef enum css_stylesheet_params_version {
    CSS_STYLESHEET_PARAMS_VERSION_1 = 1
} css_stylesheet_params_version;

/**
 * Parameter block for css_stylesheet_create()
 */
typedef struct css_stylesheet_params {
    /** ABI version of this structure */
    uint32_t params_version;

    /** The language level of the stylesheet */
    css_language_level level;

    /** The charset of the stylesheet data, or NULL to detect */
    const char *charset;
    /** URL of stylesheet */
    const char *url;
    /** Title of stylesheet */
    const char *title;

    /** Permit quirky parsing of stylesheet */
    bool allow_quirks;
    /** This stylesheet is an inline style */
    bool inline_style;

    /** URL resolution function */
    css_url_resolution_fn resolve;
    /** Client private data for resolve */
    void *resolve_pw;

    /** Import notification function */
    css_import_notification_fn import;
    /** Client private data for import */
    void *import_pw;

    /** Colour resolution function */
    css_color_resolution_fn color;
    /** Client private data for color */
    void *color_pw;

    /** Font resolution function */
    css_font_resolution_fn font;
    /** Client private data for font */
    void *font_pw;
} css_stylesheet_params;

css_error css_stylesheet_create(const css_stylesheet_params *params, css_stylesheet **stylesheet);
css_error css_stylesheet_destroy(css_stylesheet *sheet);

css_error css_stylesheet_append_data(css_stylesheet *sheet, const uint8_t *data, size_t len);
css_error css_stylesheet_data_done(css_stylesheet *sheet);

css_error css_stylesheet_next_pending_import(css_stylesheet *parent, lwc_string **url);
css_error css_stylesheet_register_import(css_stylesheet *parent, css_stylesheet *child);

/**
 * Cascade-layer key of the next pending import (the same import
 * css_stylesheet_next_pending_import() returns), or 0 if it has no
 * `layer`/`layer(name)` clause. The client stamps this on the imported sheet
 * as its base layer so the imported rules land in the right layer.
 */
css_error css_stylesheet_next_pending_import_layer(css_stylesheet *parent, uint64_t *layer);

css_error css_stylesheet_get_language_level(css_stylesheet *sheet, css_language_level *level);
css_error css_stylesheet_get_url(css_stylesheet *sheet, const char **url);
css_error css_stylesheet_get_title(css_stylesheet *sheet, const char **title);
css_error css_stylesheet_quirks_allowed(css_stylesheet *sheet, bool *allowed);
css_error css_stylesheet_used_quirks(css_stylesheet *sheet, bool *quirks);

css_error css_stylesheet_get_disabled(css_stylesheet *sheet, bool *disabled);
css_error css_stylesheet_set_disabled(css_stylesheet *sheet, bool disabled);

css_error css_stylesheet_size(css_stylesheet *sheet, size_t *size);

/**
 * Cascade-layer registry.
 *
 * Cascade layers (@layer) are ordered document-wide: the same layer name
 * must map to one priority across every author sheet of a document, and a
 * layer's sub-layers are ordered within its slot. A registry holds that
 * shared layer tree. Create one per document, hand it to every author sheet
 * with css_stylesheet_set_layer_registry() before feeding data, and destroy
 * it once the sheets are gone. A sheet with no registry falls back to a
 * private one (correct for a single standalone sheet).
 */
typedef struct css_layer_registry css_layer_registry;

css_error css_layer_registry_create(css_layer_registry **registry);
css_error css_layer_registry_destroy(css_layer_registry *registry);

/**
 * Attach a shared layer registry to a sheet. Must be called after
 * css_stylesheet_create() and before any css_stylesheet_append_data().
 *
 * \param base_layer  Optional layer key (from a prior parse, e.g. the
 *                    layer named by `@import ... layer(name)`) that the whole
 *                    sheet is nested inside; 0 for a top-level sheet.
 */
css_error css_stylesheet_set_layer_registry(css_stylesheet *sheet, css_layer_registry *registry,
                                            uint64_t base_layer);

/**
 * Resolve a layer name path (e.g. "framework.base") to its sort key in a
 * registry, creating the layer if needed. Used to translate an
 * `@import ... layer(name)` prelude into the base_layer for the imported
 * sheet. \a names is a '.'-separated path; NULL/empty means an anonymous
 * layer. Returns the key via \a key (0 only if registry is NULL).
 */
css_error css_layer_registry_resolve(css_layer_registry *registry, const char *names,
                                     uint64_t base_layer, uint64_t *key);

#ifdef __cplusplus
}
#endif

#endif
