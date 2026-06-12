/*
 * ycat.c - type registry + top-level dispatch.
 *
 * Maintains a small static array of handlers indexed by enum yetty_ycat_type,
 * plus the name ↔ enum mappings that the CLI uses for --card / --type.
 * Handler implementations live in handler-*.c and self-register via
 * yetty_ycat_register_handler() from init_handlers() on first use.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ytrace/ytrace.h>

#include <stddef.h>
#include <string.h>
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/* Forward decls: handlers defined in handler-*.c files. */
extern struct yetty_ydraw_drawable_list_result yetty_ycat_handler_image(
    const uint8_t *bytes, size_t len, const char *path_hint,
    const struct yetty_ycat_config *config);

extern struct yetty_ydraw_drawable_list_result yetty_ycat_handler_svg(
    const uint8_t *bytes, size_t len, const char *path_hint,
    const struct yetty_ycat_config *config);

#ifdef YETTY_YCAT_HAS_DIAGRAM
extern struct yetty_ydraw_drawable_list_result yetty_ycat_handler_mermaid(
    const uint8_t *bytes, size_t len, const char *path_hint,
    const struct yetty_ycat_config *config);
#endif

#ifdef YETTY_YCAT_HAS_YVIDEO
extern struct yetty_ydraw_drawable_list_result yetty_ycat_handler_video(
    const uint8_t *bytes, size_t len, const char *path_hint,
    const struct yetty_ycat_config *config);
#endif

#ifdef YETTY_YCAT_HAS_LOTTIE
extern struct yetty_ydraw_drawable_list_result yetty_ycat_handler_lottie(
    const uint8_t *bytes, size_t len, const char *path_hint,
    const struct yetty_ycat_config *config);
#endif

#ifdef YETTY_YCAT_HAS_YMUSIC
extern struct yetty_ydraw_drawable_list_result yetty_ycat_handler_music(
    const uint8_t *bytes, size_t len, const char *path_hint,
    const struct yetty_ycat_config *config);
#endif

#ifdef YETTY_YCAT_HAS_YSHADERTOY
extern struct yetty_ydraw_drawable_list_result yetty_ycat_handler_shadertoy(
    const uint8_t *bytes, size_t len, const char *path_hint,
    const struct yetty_ycat_config *config);
#endif

/* Streaming handlers (multi-envelope: PDF page-per-envelope, markdown
 * screen-height-tile-per-envelope). */
extern struct yetty_ycore_void_result yetty_ycat_handler_markdown_streaming(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config,
    yetty_ycat_emit_fn emit, void *emit_user_data);

extern struct yetty_ycore_void_result yetty_ycat_handler_pdf_streaming(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config,
    yetty_ycat_emit_fn emit, void *emit_user_data);

/*=============================================================================
 * Type name mapping
 *===========================================================================*/

static const struct {
    enum yetty_ycat_type type;
    const char *name;
} type_names[] = {
    {YETTY_YCAT_TYPE_UNKNOWN, "unknown"},   {YETTY_YCAT_TYPE_TEXT, "text"},
    {YETTY_YCAT_TYPE_MARKDOWN, "markdown"}, {YETTY_YCAT_TYPE_PDF, "pdf"},
    {YETTY_YCAT_TYPE_IMAGE, "image"},       {YETTY_YCAT_TYPE_SVG, "svg"},
    {YETTY_YCAT_TYPE_MERMAID, "mermaid"},   {YETTY_YCAT_TYPE_VIDEO, "video"},
    {YETTY_YCAT_TYPE_LOTTIE, "lottie"},     {YETTY_YCAT_TYPE_MUSIC, "music"},
    {YETTY_YCAT_TYPE_SHADERTOY, "shadertoy"},
    /* alias rows — type_name() returns the first match above, from_name()
     * accepts either spelling for --card. */
    {YETTY_YCAT_TYPE_MUSIC, "lilypond"},
    {YETTY_YCAT_TYPE_SHADERTOY, "wgsl"},
};

const char *yetty_ycat_type_name(enum yetty_ycat_type type)
{
    for (size_t i = 0; i < sizeof(type_names) / sizeof(type_names[0]); i++) {
        if (type_names[i].type == type) {
            return type_names[i].name;
        }
    }
    return "unknown";
}

enum yetty_ycat_type yetty_ycat_type_from_name(const char *name)
{
    if (!name) {
        return YETTY_YCAT_TYPE_UNKNOWN;
    }
    for (size_t i = 0; i < sizeof(type_names) / sizeof(type_names[0]); i++) {
        if (strcasecmp(name, type_names[i].name) == 0) {
            return type_names[i].type;
        }
    }
    return YETTY_YCAT_TYPE_UNKNOWN;
}

/*=============================================================================
 * Handler registry
 *===========================================================================*/

#define YCAT_MAX_TYPE 16

static yetty_ycat_handler_fn handlers[YCAT_MAX_TYPE];
static yetty_ycat_handler_streaming_fn handlers_streaming[YCAT_MAX_TYPE];
static int handlers_initialized = 0;

static void init_handlers(void)
{
    if (handlers_initialized) {
        return;
    }
    handlers_initialized = 1;
    handlers[YETTY_YCAT_TYPE_IMAGE] = yetty_ycat_handler_image;
    handlers[YETTY_YCAT_TYPE_SVG] = yetty_ycat_handler_svg;
#ifdef YETTY_YCAT_HAS_DIAGRAM
    handlers[YETTY_YCAT_TYPE_MERMAID] = yetty_ycat_handler_mermaid;
#endif
#ifdef YETTY_YCAT_HAS_YVIDEO
    handlers[YETTY_YCAT_TYPE_VIDEO] = yetty_ycat_handler_video;
#endif
#ifdef YETTY_YCAT_HAS_LOTTIE
    handlers[YETTY_YCAT_TYPE_LOTTIE] = yetty_ycat_handler_lottie;
#endif
#ifdef YETTY_YCAT_HAS_YMUSIC
    handlers[YETTY_YCAT_TYPE_MUSIC] = yetty_ycat_handler_music;
#endif
#ifdef YETTY_YCAT_HAS_YSHADERTOY
    handlers[YETTY_YCAT_TYPE_SHADERTOY] = yetty_ycat_handler_shadertoy;
#endif
    handlers_streaming[YETTY_YCAT_TYPE_MARKDOWN] = yetty_ycat_handler_markdown_streaming;
    handlers_streaming[YETTY_YCAT_TYPE_PDF] = yetty_ycat_handler_pdf_streaming;
}

yetty_ycat_handler_fn yetty_ycat_get_handler(enum yetty_ycat_type type)
{
    init_handlers();
    if ((int)type < 0 || (int)type >= YCAT_MAX_TYPE) {
        return NULL;
    }
    return handlers[type];
}

int yetty_ycat_register_handler(enum yetty_ycat_type type, yetty_ycat_handler_fn fn)
{
    init_handlers();
    if ((int)type < 0 || (int)type >= YCAT_MAX_TYPE) {
        return -1;
    }
    handlers[type] = fn;
    return 0;
}

yetty_ycat_handler_streaming_fn yetty_ycat_get_handler_streaming(enum yetty_ycat_type type)
{
    init_handlers();
    if ((int)type < 0 || (int)type >= YCAT_MAX_TYPE) {
        return NULL;
    }
    return handlers_streaming[type];
}

int yetty_ycat_register_handler_streaming(enum yetty_ycat_type type,
                                          yetty_ycat_handler_streaming_fn fn)
{
    init_handlers();
    if ((int)type < 0 || (int)type >= YCAT_MAX_TYPE) {
        return -1;
    }
    handlers_streaming[type] = fn;
    return 0;
}

/*=============================================================================
 * Dispatch
 *===========================================================================*/

struct yetty_ydraw_drawable_list_result yetty_ycat_render(const uint8_t *bytes, size_t len,
                                                          const char *path_hint,
                                                          const struct yetty_ycat_config *config)
{
    enum yetty_ycat_type type = yetty_ycat_detect(bytes, len, path_hint);
    yetty_ycat_handler_fn fn = yetty_ycat_get_handler(type);
    if (!fn) {
        ydebug("ycat_render: no handler for type=%s", yetty_ycat_type_name(type));
        return YETTY_ERR(yetty_ydraw_drawable_list, "no handler for detected type");
    }
    return fn(bytes, len, path_hint, config);
}
