// ypaint-yaml - YAML parser for ypaint primitives

#include <yetty/ydraw-yaml/ypaint-yaml.h>
#include <yetty/ysdf/handler.h>
#include <yetty/yplot/yplot-gen.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/ydraw-core/complex-prim-types.h>
#include <yetty/ytrace/ytrace.h>
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if YETTY_HAS_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

/* Resolve a font NAME (e.g. "DejaVu Serif", "Liberation Mono", "Arial") to
 * an absolute TTF/OTF path via fontconfig. Returns a malloc'd string on
 * success, NULL on miss / no fontconfig at build time. The exact match is
 * fontconfig's "best match" — same as `fc-match <name>`. */
static char *resolve_font_to_path(const char *name)
{
#if YETTY_HAS_FONTCONFIG
    if (!name || !*name) {
        return NULL;
    }
    static int s_fc_inited = 0;
    if (!s_fc_inited) {
        if (!FcInit()) {
            ywarn("ypaint_yaml: FcInit failed");
            return NULL;
        }
        s_fc_inited = 1;
    }

    FcPattern *pat = FcNameParse((const FcChar8 *)name);
    if (!pat) {
        return NULL;
    }
    FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult result;
    FcPattern *match = FcFontMatch(NULL, pat, &result);
    char *out = NULL;
    if (match) {
        FcChar8 *file = NULL;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            out = strdup((const char *)file);
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pat);
    return out;
#else
    (void)name;
    return NULL;
#endif
}

/* Read an entire file into a malloc'd buffer. Returns NULL on error. */
static uint8_t *read_file_bytes(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

//=============================================================================
// Registry
//=============================================================================

#define MAX_FACTORIES 64
#define PRIMITIVE_TYPE_NAME_LEN 32

struct yetty_ydraw_yaml_factory_entry {
    char primitive_type_name[PRIMITIVE_TYPE_NAME_LEN];
    yetty_ydraw_yaml_factory_fn factory;
};

struct yetty_ydraw_yaml_parser {
    struct yetty_ydraw_yaml_factory_entry entries[MAX_FACTORIES];
    size_t count;
};

//=============================================================================
// Low level API
//=============================================================================

struct yetty_ydraw_yaml_parser *yetty_ydraw_yaml_parser_create(void)
{
    struct yetty_ydraw_yaml_parser *parser = calloc(1, sizeof(*parser));
    return parser;
}

void yetty_ydraw_yaml_parser_destroy(struct yetty_ydraw_yaml_parser *parser)
{
    free(parser);
}

struct yetty_ycore_void_result yetty_ydraw_yaml_parser_register(
    struct yetty_ydraw_yaml_parser *parser, const char *primitive_type_name,
    yetty_ydraw_yaml_factory_fn factory)
{
    if (!parser) {
        return YETTY_ERR(yetty_ycore_void, "null parser");
    }
    if (!primitive_type_name || !factory) {
        return YETTY_ERR(yetty_ycore_void, "null primitive_type_name or factory");
    }
    if (parser->count >= MAX_FACTORIES) {
        return YETTY_ERR(yetty_ycore_void, "max factories reached");
    }

    strncpy(parser->entries[parser->count].primitive_type_name, primitive_type_name,
            PRIMITIVE_TYPE_NAME_LEN - 1);
    parser->entries[parser->count].factory = factory;
    parser->count++;

    return YETTY_OK_VOID();
}

static yetty_ydraw_yaml_factory_fn find_factory(struct yetty_ydraw_yaml_parser *parser,
                                                 const char *primitive_type_name)
{
    for (size_t i = 0; i < parser->count; i++) {
        if (strcmp(parser->entries[i].primitive_type_name, primitive_type_name) == 0) {
            return parser->entries[i].factory;
        }
    }
    return NULL;
}

struct yetty_ycore_void_result yetty_ydraw_yaml_parser_parse(
    struct yetty_ydraw_yaml_parser *parser, struct yetty_ydraw_core_buffer *buffer,
    const char *yaml, size_t len)
{
    if (!parser || !buffer || !yaml) {
        return YETTY_ERR(yetty_ycore_void, "null argument");
    }

    struct yaml_parser_s yaml_parser;
    yaml_event_t event;
    int done = 0, err = 0;
    int depth = 0;
    int in_body = 0;
    int in_prim = 0;
    char primitive_type_name[PRIMITIVE_TYPE_NAME_LEN] = {0};
    char prop_key[PRIMITIVE_TYPE_NAME_LEN] = {0};

    if (!yaml_parser_initialize(&yaml_parser)) {
        return YETTY_ERR(yetty_ycore_void, "yaml_parser_initialize failed");
    }

    yaml_parser_set_input_string(&yaml_parser, (const unsigned char *)yaml, len);

    while (!done && !err) {
        if (!yaml_parser_parse(&yaml_parser, &event)) {
            err = 1;
            break;
        }

        switch (event.type) {
        case YAML_STREAM_END_EVENT:
            done = 1;
            break;

        case YAML_MAPPING_START_EVENT:
            depth++;
            ydebug("ypaint_yaml: MAPPING_START depth=%d in_body=%d in_prim=%d", depth, in_body,
                   in_prim);
            if (in_body && !in_prim) {
                in_prim = 1;
                primitive_type_name[0] = 0;
                ydebug("ypaint_yaml: entering primitive");
            }
            break;

        case YAML_MAPPING_END_EVENT:
            ydebug("ypaint_yaml: MAPPING_END depth=%d->%d in_prim=%d", depth, depth - 1, in_prim);
            depth--;
            if (in_prim && depth == 1) {
                in_prim = 0;
                ydebug("ypaint_yaml: exiting primitive");
            }
            break;

        case YAML_SEQUENCE_START_EVENT:
            ydebug("ypaint_yaml: SEQUENCE_START depth=%d prop_key='%s'", depth, prop_key);
            if (depth == 1 && strcmp(prop_key, "body") == 0) {
                in_body = 1;
                ydebug("ypaint_yaml: entering body");
            }
            break;

        case YAML_SEQUENCE_END_EVENT:
            ydebug("ypaint_yaml: SEQUENCE_END depth=%d in_body=%d", depth, in_body);
            if (in_body && depth == 1) {
                in_body = 0;
                ydebug("ypaint_yaml: exiting body");
            }
            break;

        case YAML_SCALAR_EVENT: {
            const char *val = (const char *)event.data.scalar.value;

            if (in_prim && primitive_type_name[0] == 0) {
                strncpy(primitive_type_name, val, PRIMITIVE_TYPE_NAME_LEN - 1);
                ydebug("ypaint_yaml: got primitive type '%s' at depth=%d", primitive_type_name,
                       depth);

                yetty_ydraw_yaml_factory_fn factory = find_factory(parser, primitive_type_name);
                if (factory) {
                    ydebug("ypaint_yaml: calling factory for '%s'", primitive_type_name);
                    struct yetty_ycore_void_result res =
                        factory(buffer, &yaml_parser, primitive_type_name);
                    if (YETTY_IS_ERR(res)) {
                        ydebug("ypaint_yaml: factory for '%s' failed: %s", primitive_type_name,
                               res.error.msg);
                        yaml_event_delete(&event);
                        yaml_parser_delete(&yaml_parser);
                        return res;
                    }
                    ydebug("ypaint_yaml: factory for '%s' succeeded", primitive_type_name);
                } else {
                    ydebug("ypaint_yaml: unknown type '%s' (registered=%zu)", primitive_type_name,
                           parser->count);
                    yaml_event_delete(&event);
                    yaml_parser_delete(&yaml_parser);
                    return YETTY_ERR(yetty_ycore_void, "unknown primitive type");
                }
            } else if (depth == 1) {
                strncpy(prop_key, val, PRIMITIVE_TYPE_NAME_LEN - 1);
            }
            break;
        }

        default:
            break;
        }

        yaml_event_delete(&event);
    }

    yaml_parser_delete(&yaml_parser);

    return err ? YETTY_ERR(yetty_ycore_void, "yaml parse error") : YETTY_OK_VOID();
}

//=============================================================================
// External factory registration (forward declarations)
//=============================================================================

#include <yetty/ysdf/yaml-factory.gen.h>

//=============================================================================
// Internal text factory
//=============================================================================

static uint32_t parse_text_color(const char *str)
{
    if (!str || !*str) {
        return 0xFFFFFFFF;
    }
    if (str[0] == '#') {
        str++;
    }
    size_t len = strlen(str);
    char buf[9] = {0};
    if (len == 3) {
        buf[0] = buf[1] = str[0];
        buf[2] = buf[3] = str[1];
        buf[4] = buf[5] = str[2];
        buf[6] = buf[7] = 'F';
    } else if (len == 6) {
        memcpy(buf, str, 6);
        buf[6] = buf[7] = 'F';
    } else if (len == 8) {
        memcpy(buf, str, 8);
    } else {
        return 0xFFFFFFFF;
    }
    char *end;
    unsigned long v = strtoul(buf, &end, 16);
    if (*end) {
        return 0xFFFFFFFF;
    }
    uint32_t r = (v >> 24) & 0xFF;
    uint32_t g = (v >> 16) & 0xFF;
    uint32_t b = (v >> 8) & 0xFF;
    uint32_t a = v & 0xFF;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

static struct yetty_ycore_void_result text_factory(struct yetty_ydraw_core_buffer *buffer,
                                                   struct yaml_parser_s *yaml_parser,
                                                   const char *primitive_type_name)
{
    (void)primitive_type_name;

    char prop_key[32] = {0};
    float x = 0, y = 0;
    float font_size = 14.0f;
    uint32_t color = 0xFFFFFFFF;
    char content[1024] = {0};
    /* Optional font name (resolved via fontconfig). When set, the parser
     * adds a FONT prim with the resolved TTF blob and references it from
     * the text span by font_id; downstream the canvas materialises the
     * font through whichever MSDF generator is configured. NULL/empty
     * means "use the canvas default font" (font_id = -1). */
    char font_name[128] = {0};
    int expect_value = 0;
    int in_array = 0;
    int array_idx = 0;
    float array_vals[8] = {0};

    yaml_event_t event;
    int depth = 0;
    int done = 0;

    while (!done) {
        if (!yaml_parser_parse(yaml_parser, &event)) {
            return YETTY_ERR(yetty_ycore_void, "yaml parse error in text");
        }

        switch (event.type) {
        case YAML_MAPPING_START_EVENT:
            depth++;
            break;
        case YAML_MAPPING_END_EVENT:
            depth--;
            if (depth == 0) {
                done = 1;
            }
            break;
        case YAML_SEQUENCE_START_EVENT:
            in_array = 1;
            array_idx = 0;
            break;
        case YAML_SEQUENCE_END_EVENT:
            if (strcmp(prop_key, "position") == 0 && array_idx >= 2) {
                x = array_vals[0];
                y = array_vals[1];
            }
            in_array = 0;
            expect_value = 0;
            break;
        case YAML_SCALAR_EVENT: {
            const char *val = (const char *)event.data.scalar.value;
            if (in_array) {
                if (array_idx < 8) {
                    array_vals[array_idx++] = strtof(val, NULL);
                }
            } else if (!expect_value) {
                strncpy(prop_key, val, sizeof(prop_key) - 1);
                expect_value = 1;
            } else {
                if (strcmp(prop_key, "content") == 0) {
                    strncpy(content, val, sizeof(content) - 1);
                } else if (strcmp(prop_key, "font-size") == 0 ||
                           strcmp(prop_key, "font_size") == 0) {
                    font_size = strtof(val, NULL);
                } else if (strcmp(prop_key, "color") == 0) {
                    color = parse_text_color(val);
                } else if (strcmp(prop_key, "font") == 0) {
                    strncpy(font_name, val, sizeof(font_name) - 1);
                }
                expect_value = 0;
            }
            break;
        }
        default:
            break;
        }
        yaml_event_delete(&event);
    }

    if (content[0] != 0) {
        /* If a font name was given, resolve it via fontconfig, read the
         * file, and emit a FONT prim. Failures (no fontconfig, name
         * doesn't match any installed font, file read error) fall back
         * silently to the canvas default font — same behaviour as
         * leaving `font:` unset. */
        int32_t font_id = -1;
        if (font_name[0] != 0) {
            char *path = resolve_font_to_path(font_name);
            if (path) {
                size_t ttf_len = 0;
                uint8_t *ttf = read_file_bytes(path, &ttf_len);
                if (ttf) {
                    struct yetty_ycore_buffer ttf_buf = {
                        .data = ttf, .size = ttf_len, .capacity = ttf_len};
                    struct yetty_ycore_int_result fr =
                        yetty_ydraw_core_buffer_add_font(buffer, &ttf_buf, font_name);
                    if (YETTY_IS_OK(fr)) {
                        font_id = fr.value;
                        ydebug("ypaint_yaml: font '%s' -> %s (font_id=%d)", font_name, path,
                               font_id);
                    } else {
                        ywarn("ypaint_yaml: add_font failed for '%s': %s", font_name, fr.error.msg);
                    }
                    free(ttf);
                } else {
                    ywarn("ypaint_yaml: cannot read font file '%s' for name '%s'", path, font_name);
                }
                free(path);
            } else {
                ywarn("ypaint_yaml: font '%s' not found via fontconfig", font_name);
            }
        }

        struct yetty_ycore_buffer text_buf = {
            .data = (uint8_t *)content, .size = strlen(content), .capacity = strlen(content)};
        struct yetty_ycore_void_result res = yetty_ydraw_core_buffer_add_text(
            buffer, x, y, &text_buf, font_size, color, 0, font_id, 0.0f);
        if (YETTY_IS_ERR(res)) {
            ydebug("ypaint_yaml: failed to add text: %s", res.error.msg);
            return res;
        }
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result register_text_factory(struct yetty_ydraw_yaml_parser *parser)
{
    return yetty_ydraw_yaml_parser_register(parser, "text", text_factory);
}

// yplot factory is generated - see yplot-gen-yaml.c

//=============================================================================
// High level API
//=============================================================================

struct yetty_ydraw_core_buffer_result yetty_ydraw_yaml_parse(const char *yaml, size_t len)
{
    if (!yaml || len == 0) {
        return YETTY_ERR(yetty_ydraw_core_buffer, "null or empty yaml");
    }

    struct yetty_ydraw_core_buffer_result buf_res =
        yetty_ydraw_core_buffer_config_buffer_create(NULL);
    if (YETTY_IS_ERR(buf_res)) {
        return buf_res;
    }

    struct yetty_ydraw_core_buffer *buffer = buf_res.value;

    struct yetty_ydraw_yaml_parser *parser = yetty_ydraw_yaml_parser_create();
    if (!parser) {
        yetty_ydraw_core_buffer_destroy(buffer);
        return YETTY_ERR(yetty_ydraw_core_buffer, "failed to create parser");
    }

    struct yetty_ycore_void_result reg_res = yetty_ysdf_register_yaml_factories(parser);
    if (YETTY_IS_ERR(reg_res)) {
        ydebug("ypaint_yaml: ysdf registration failed: %s", reg_res.error.msg);
    } else {
        ydebug("ypaint_yaml: ysdf factories registered, count=%zu", parser->count);
    }
    struct yetty_ycore_void_result text_reg_res = register_text_factory(parser);
    if (YETTY_IS_ERR(text_reg_res)) {
        ydebug("ypaint_yaml: text factory registration failed: %s", text_reg_res.error.msg);
    }
    struct yetty_ycore_void_result yplot_reg_res = yetty_yplot_register_yaml_factory(parser);
    if (YETTY_IS_ERR(yplot_reg_res)) {
        ydebug("ypaint_yaml: yplot factory registration failed: %s", yplot_reg_res.error.msg);
    }
    ydebug("ypaint_yaml: text+yplot factories registered, total count=%zu", parser->count);

    struct yetty_ycore_void_result parse_res =
        yetty_ydraw_yaml_parser_parse(parser, buffer, yaml, len);

    yetty_ydraw_yaml_parser_destroy(parser);

    if (YETTY_IS_ERR(parse_res)) {
        yetty_ydraw_core_buffer_destroy(buffer);
        return YETTY_ERR(yetty_ydraw_core_buffer, parse_res.error.msg);
    }

    return YETTY_OK(yetty_ydraw_core_buffer, buffer);
}
