/*
 * yflame.c — flame-graph figure.
 *
 * Pipeline: folded-stack text → call tree → recursive nested-rectangle
 * layout → ydraw drawable list of filled boxes + MSDF labels → YDRAW_BIN OSC
 * envelope. See include/yetty/yflame/yflame.h for the contract.
 *
 * Rendering follows ydiagram (generic ydraw box/text primitives), not yplot
 * (a single GPU-evaluated prim): a flame graph is a labelled tree of
 * rectangles, which the canvas already knows how to draw.
 */

#include <yetty/yflame/yflame.h>

#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    YFLAME_DEFAULT_FRAME_HEIGHT = 18,
    YFLAME_DEFAULT_BOUNDS_W = 1200,
    YFLAME_TEXT_PAD = 3,        /* left inset of a label inside its box (px) */
    YFLAME_BOX_GAP_NUM = 1,     /* ~1px gap carved between adjacent boxes */
};

/*=============================================================================
 * Call tree
 *===========================================================================*/

struct yflame_frame {
    char *name;
    uint64_t value; /* total samples passing through this frame */

    struct yflame_frame **children;
    size_t child_count;
    size_t child_cap;

    /* Filled in by the layout pass. */
    float x_start;
    float x_end;
    uint32_t depth;
};

static struct yflame_frame *frame_create(const char *name, size_t name_len)
{
    struct yflame_frame *frame = calloc(1, sizeof(struct yflame_frame));
    if (!frame) {
        return NULL;
    }
    frame->name = malloc(name_len + 1);
    if (!frame->name) {
        free(frame);
        return NULL;
    }
    memcpy(frame->name, name, name_len);
    frame->name[name_len] = '\0';
    return frame;
}

static void frame_destroy(struct yflame_frame *frame)
{
    if (!frame) {
        return;
    }
    for (size_t i = 0; i < frame->child_count; i++) {
        frame_destroy(frame->children[i]);
    }
    free(frame->children);
    free(frame->name);
    free(frame);
}

/* Find a direct child by name, or create and append one. Returns NULL on OOM. */
static struct yflame_frame *frame_child(struct yflame_frame *parent, const char *name,
                                        size_t name_len)
{
    for (size_t i = 0; i < parent->child_count; i++) {
        struct yflame_frame *child = parent->children[i];
        if (strlen(child->name) == name_len && memcmp(child->name, name, name_len) == 0) {
            return child;
        }
    }

    if (parent->child_count == parent->child_cap) {
        size_t new_cap = parent->child_cap ? parent->child_cap * 2 : 4;
        struct yflame_frame **grown =
            realloc(parent->children, new_cap * sizeof(struct yflame_frame *));
        if (!grown) {
            return NULL;
        }
        parent->children = grown;
        parent->child_cap = new_cap;
    }

    struct yflame_frame *child = frame_create(name, name_len);
    if (!child) {
        return NULL;
    }
    parent->children[parent->child_count++] = child;
    return child;
}

static int frame_name_compare(const void *left, const void *right)
{
    const struct yflame_frame *const *a = left;
    const struct yflame_frame *const *b = right;
    return strcmp((*a)->name, (*b)->name);
}

/* Sort each node's children alphabetically (the canonical flame-graph order,
 * so the same profile always lays out identically and merges cleanly). */
static void frame_sort(struct yflame_frame *frame)
{
    if (frame->child_count > 1) {
        qsort(frame->children, frame->child_count, sizeof(struct yflame_frame *),
              frame_name_compare);
    }
    for (size_t i = 0; i < frame->child_count; i++) {
        frame_sort(frame->children[i]);
    }
}

/*=============================================================================
 * Folded-stack parsing
 *
 * Each line: `frame1;frame2;frame3 <count>`. Frame names may contain spaces,
 * so the count is split off at the LAST space. A line adds <count> to every
 * frame along its path (so each node's value is the total samples through it).
 *===========================================================================*/

static struct yetty_ycore_void_result parse_folded(const char *input, size_t len,
                                                   struct yflame_frame *root)
{
    size_t pos = 0;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && input[pos] != '\n') {
            pos++;
        }
        size_t line_end = pos; /* exclusive */
        if (pos < len) {
            pos++; /* skip the newline */
        }
        /* Trim a trailing CR (CRLF inputs). */
        if (line_end > line_start && input[line_end - 1] == '\r') {
            line_end--;
        }
        if (line_end == line_start) {
            continue; /* blank line */
        }

        /* Split count off the last space. */
        size_t space = line_end;
        while (space > line_start && input[space - 1] != ' ') {
            space--;
        }
        if (space == line_start) {
            continue; /* no space → not a folded line; skip */
        }

        char count_buf[32];
        size_t count_len = line_end - space;
        if (count_len == 0 || count_len >= sizeof(count_buf)) {
            continue;
        }
        memcpy(count_buf, input + space, count_len);
        count_buf[count_len] = '\0';
        char *count_endp = NULL;
        unsigned long long count = strtoull(count_buf, &count_endp, 10);
        if (!count_endp || *count_endp != '\0' || count == 0) {
            continue; /* trailing token wasn't a positive integer */
        }

        size_t stack_end = space - 1; /* drop the separating space */

        /* Walk the `;`-separated frames, adding count along the path. */
        root->value += count;
        struct yflame_frame *node = root;
        size_t frame_start = line_start;
        for (size_t i = line_start; i <= stack_end; i++) {
            int at_end = (i == stack_end);
            if (at_end || input[i] == ';') {
                size_t frame_len = (at_end ? i + 1 : i) - frame_start;
                if (frame_len > 0) {
                    struct yflame_frame *child = frame_child(node, input + frame_start, frame_len);
                    if (!child) {
                        return YETTY_ERR(yetty_ycore_void, "yflame: out of memory building tree");
                    }
                    child->value += count;
                    node = child;
                }
                frame_start = i + 1;
            }
        }
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Layout — assign each node a pixel x-span and depth.
 *===========================================================================*/

static void layout(struct yflame_frame *node, float x_start, float x_end, uint32_t depth,
                   uint32_t *max_depth)
{
    node->x_start = x_start;
    node->x_end = x_end;
    node->depth = depth;
    if (depth > *max_depth) {
        *max_depth = depth;
    }
    if (node->value == 0) {
        return;
    }

    float span = x_end - x_start;
    float cursor = x_start;
    for (size_t i = 0; i < node->child_count; i++) {
        struct yflame_frame *child = node->children[i];
        float child_span = span * (float)((double)child->value / (double)node->value);
        layout(child, cursor, cursor + child_span, depth + 1, max_depth);
        cursor += child_span;
    }
}

/*=============================================================================
 * Colour — the classic warm "flame" palette, hashed per frame name.
 *===========================================================================*/

static uint32_t name_hash(const char *name)
{
    uint32_t hash = 2166136261u; /* FNV-1a */
    for (const char *cursor = name; *cursor; cursor++) {
        hash ^= (uint8_t)*cursor;
        hash *= 16777619u;
    }
    return hash;
}

/* Warm palette: red 205..255, green 0..230, blue 0..55 — the Brendan Gregg
 * flame colours. Returns 0xAARRGGBB. */
static uint32_t warm_color(const char *name)
{
    uint32_t hash = name_hash(name);
    uint32_t red = 205u + (((hash) & 0xFFu) * 50u) / 255u;
    uint32_t green = (((hash >> 8) & 0xFFu) * 230u) / 255u;
    uint32_t blue = (((hash >> 16) & 0xFFu) * 55u) / 255u;
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

/*=============================================================================
 * Label truncation — approximate advance, clamped to a UTF-8 boundary.
 *===========================================================================*/

/* Back `len` off any trailing UTF-8 continuation byte so we never cut a
 * multi-byte codepoint in half. */
static size_t utf8_clamp(const char *text, size_t len)
{
    while (len > 0 && ((uint8_t)text[len] & 0xC0u) == 0x80u) {
        len--;
    }
    return len;
}

/*=============================================================================
 * Emit — one box (+ optional label) per visible frame, recursively.
 *===========================================================================*/

static struct yetty_ycore_void_result emit(struct yetty_ydraw_drawable_list *buf,
                                          const struct yflame_frame *node,
                                          const struct yetty_yflame_render_config *config,
                                          float frame_height, float min_width, float total_height,
                                          int icicle, int labels, uint32_t *z)
{
    float width = node->x_end - node->x_start;
    if (width >= min_width) {
        float top;
        if (icicle) {
            top = config->bounds_y + (float)node->depth * frame_height;
        } else {
            top = config->bounds_y + total_height - (float)(node->depth + 1) * frame_height;
        }

        float gap = (float)YFLAME_BOX_GAP_NUM;
        float half_w = (width - gap) * 0.5f;
        float half_h = (frame_height - gap) * 0.5f;
        if (half_w < 0.0f) {
            half_w = width * 0.5f;
        }
        if (half_h < 0.0f) {
            half_h = frame_height * 0.5f;
        }
        struct yetty_ysdf_box geom = {
            .center_x = (node->x_start + node->x_end) * 0.5f,
            .center_y = top + frame_height * 0.5f,
            .half_width = half_w,
            .half_height = half_h,
            .corner_radius = 2.0f,
        };
        struct yetty_ycore_void_result box_result = yetty_ydraw_drawable_list_add_cmd_add_box(
            buf, /*id=*/0, /*z_order=*/(*z)++, warm_color(node->name), /*stroke_color=*/0,
            /*stroke_width=*/0.0f, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, box_result, "yflame: add box");

        if (labels) {
            float font_size = frame_height * 0.6f;
            float advance = font_size * 0.6f; /* approximate monospace-ish advance */
            float avail = width - 2.0f * (float)YFLAME_TEXT_PAD;
            size_t max_chars = (advance > 0.0f && avail > 0.0f) ? (size_t)(avail / advance) : 0;
            size_t name_len = strlen(node->name);
            size_t draw_len = name_len < max_chars ? name_len : max_chars;
            draw_len = utf8_clamp(node->name, draw_len);
            if (draw_len > 0) {
                struct yetty_ycore_buffer text_view = {
                    .data = (uint8_t *)(uintptr_t)node->name,
                    .capacity = draw_len,
                    .size = draw_len,
                };
                /* Baseline near the vertical centre of the box. */
                float text_x = node->x_start + (float)YFLAME_TEXT_PAD;
                float text_y = top + frame_height * 0.5f + font_size / 3.0f;
                struct yetty_ycore_void_result text_result = yetty_ydraw_drawable_list_add_text(
                    buf, text_x, text_y, &text_view, font_size, /*color=*/0xFF000000u,
                    /*layer=*/(*z)++, /*font_id=*/-1, /*rotation=*/0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, text_result, "yflame: add label");
            }
        }
    }

    for (size_t i = 0; i < node->child_count; i++) {
        struct yetty_ycore_void_result child_result = emit(buf, node->children[i], config,
                                                          frame_height, min_width, total_height,
                                                          icicle, labels, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, child_result, "yflame: emit child");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ydraw_drawable_list_result yetty_yflame_render(
    const char *input, size_t len, const struct yetty_yflame_render_config *config)
{
    if (!input && len > 0) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame: input is NULL");
    }

    struct yetty_yflame_render_config defaults = {0};
    if (!config) {
        config = &defaults;
    }
    float bounds_w = config->bounds_w > 0.0f ? config->bounds_w : (float)YFLAME_DEFAULT_BOUNDS_W;
    float frame_height =
        config->frame_height > 0.0f ? config->frame_height : (float)YFLAME_DEFAULT_FRAME_HEIGHT;
    float min_width = config->min_width > 0.0f ? config->min_width : 0.5f;
    uint32_t flags = config->flags ? config->flags : YETTY_YFLAME_FLAG_LABELS;
    int icicle = (flags & YETTY_YFLAME_FLAG_ICICLE) != 0;
    int labels = (flags & YETTY_YFLAME_FLAG_LABELS) != 0;

    struct yflame_frame *root = frame_create("all", 3);
    if (!root) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame: out of memory");
    }

    struct yetty_ycore_void_result parsed = parse_folded(input, len, root);
    if (YETTY_IS_ERR(parsed)) {
        frame_destroy(root);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame: parse failed", parsed);
    }
    if (root->value == 0) {
        frame_destroy(root);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame: no samples in input");
    }

    frame_sort(root);

    uint32_t max_depth = 0;
    layout(root, config->bounds_x, config->bounds_x + bounds_w, 0, &max_depth);
    float total_height = (float)(max_depth + 1) * frame_height;

    struct yetty_ydraw_drawable_list_config buffer_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = config->bounds_x + bounds_w,
        .scene_max_y = config->bounds_y + total_height,
    };
    struct yetty_ydraw_drawable_list_result list =
        yetty_ydraw_drawable_list_config_buffer_create(&buffer_config);
    if (YETTY_IS_ERR(list)) {
        frame_destroy(root);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame: drawable list create failed", list);
    }

    uint32_t z = 0;
    struct yetty_ycore_void_result emitted =
        emit(list.value, root, config, frame_height, min_width, total_height, icicle, labels, &z);
    frame_destroy(root);
    if (YETTY_IS_ERR(emitted)) {
        yetty_ydraw_drawable_list_destroy(list.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame: emit failed", emitted);
    }

    return YETTY_OK(yetty_ydraw_drawable_list, list.value);
}

struct yetty_ycore_size_result yetty_yflame_osc_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "yflame_osc_bin_emit: NULL buffer or out");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)buffer, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_size, "yflame_osc_bin_emit: empty serialize");
    }

    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_result = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(emit_result)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "yflame_osc_bin_emit: yface_emit failed", emit_result);
    }

    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
