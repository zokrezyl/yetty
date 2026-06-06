/*
 * flame.c — yclass class `yflame:flame`: a flame-graph client/model.
 *
 * `flame` is a client-side object: it parses folded stack samples into a call
 * tree, lays it out as nested rectangles, and renders that to a ydraw drawable
 * list. It does NOT render itself — a flame is a frontend. In one-shot mode the
 * caller ships the drawable list into the scrolling layer (YDRAW_BIN, like
 * ycat); in interactive mode it ships the same list to a remote server figure
 * via `yview` (YCOMPOSITOR_BIN) and drives hover-highlight + click-to-zoom by
 * re-rendering. `yfigure` is the backend that displays it; this class is the
 * frontend that produces the picture.
 *
 * Being a yclass class, `make codegen` emits the public header (flame.h), the
 * method dispatch, model.yaml, and the FFI / host-language binding surface. The
 * only hand-written file is this annotated .c; flame.gen.c is #included at the
 * foot. Every slot is `local@` — a flame is an in-process model/emitter, never
 * proxied over RPC; the model still records the methods so bindings emit them.
 *
 * Input is the Brendan Gregg / stackcollapse "folded" format: one
 * `frame1;frame2;frame3 <count>` line per collapsed stack.
 */
#include <yetty/yclass/class.h>
#include <yetty/yflame/flame.h>

#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef YCLASS_CODEGEN
/* Public flags — copied verbatim into the generated flame.h. */
#define YETTY_YFLAME_FLAG_LABELS 0x1u /* draw truncated frame-name labels */
#define YETTY_YFLAME_FLAG_ICICLE 0x2u /* root at top, growing down (vs flame: bottom-up) */
/* hit_test returns these (in place of a node id) when a navigation button is
 * under the point: the caller should focus the parent / reset to root. */
#define YETTY_YFLAME_HIT_UP (-2)
#define YETTY_YFLAME_HIT_ROOT (-3)
#endif

enum {
    YFLAME_DEFAULT_FRAME_HEIGHT = 18,
    YFLAME_DEFAULT_WIDTH = 1200,
    YFLAME_TEXT_PAD = 3,    /* left inset of a label inside its box (px) */
    YFLAME_BOX_GAP_NUM = 1, /* ~1px gap carved between adjacent boxes */
    YFLAME_BTN_HEIGHT = 24, /* navigation buttons in the top bar (px) */
    YFLAME_BTN_WIDTH = 84,
    YFLAME_BTN_PAD = 5,
    /* Reserved top strip the buttons live in; the graph starts below it so the
     * buttons never cover the deepest (top) frames. */
    YFLAME_BAR_HEIGHT = YFLAME_BTN_PAD + YFLAME_BTN_HEIGHT + YFLAME_BTN_PAD,
};

/*=============================================================================
 * Call tree (internal — not exposed)
 *===========================================================================*/

struct yflame_frame {
    char *name;
    uint64_t value; /* total samples passing through this frame */

    struct yflame_frame *parent; /* NULL for the synthetic root */
    struct yflame_frame **children;
    size_t child_count;
    size_t child_cap;

    uint32_t id; /* preorder index; the public node handle */

    /* Filled in by the layout pass (relative to the current focus). */
    float x_start;
    float x_end;
    uint32_t depth;
};

/*=============================================================================
 * Class data
 *===========================================================================*/

struct [[clang::annotate("class@yflame:flame")]] flame_data {
    /* Render configuration. */
    float width;
    float frame_height;
    float min_width;
    uint32_t flags;

    /* Parsed tree + a flat preorder index (id -> node). */
    struct yflame_frame *root;
    struct yflame_frame **nodes;
    uint32_t node_count;

    /* Interaction state. */
    int32_t focus_id;     /* subtree shown full-width; 0 = root */
    int32_t highlight_id; /* hovered node, or -1 */

    /* Last layout (of the focus subtree). */
    uint32_t max_depth;
    float total_height;
};

static struct flame_data *flame_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yflame_flame_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        yetty_ycore_error_destroy(slice_r.error);
        return NULL;
    }
    return (struct flame_data *)slice_r.value;
}

/*=============================================================================
 * Tree construction
 *===========================================================================*/

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
    child->parent = parent;
    parent->children[parent->child_count++] = child;
    return child;
}

static int frame_name_compare(const void *left, const void *right)
{
    const struct yflame_frame *const *a = left;
    const struct yflame_frame *const *b = right;
    return strcmp((*a)->name, (*b)->name);
}

/* Sort each node's children alphabetically (the canonical, mergeable order). */
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

/* Folded line: `frame1;frame2 <count>`. Count split at the LAST space (names
 * may contain spaces). Adds count to every frame on the path. */
static struct yetty_ycore_void_result parse_folded(const char *input, size_t len,
                                                   struct yflame_frame *root)
{
    size_t pos = 0;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && input[pos] != '\n') {
            pos++;
        }
        size_t line_end = pos;
        if (pos < len) {
            pos++;
        }
        if (line_end > line_start && input[line_end - 1] == '\r') {
            line_end--;
        }
        if (line_end == line_start) {
            continue;
        }

        size_t space = line_end;
        while (space > line_start && input[space - 1] != ' ') {
            space--;
        }
        if (space == line_start) {
            continue;
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
            continue;
        }

        size_t stack_end = space - 1;
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

static uint32_t count_nodes(const struct yflame_frame *node)
{
    uint32_t total = 1;
    for (size_t i = 0; i < node->child_count; i++) {
        total += count_nodes(node->children[i]);
    }
    return total;
}

/* Preorder flatten: assign each node an id and record it in `nodes`. */
static void flatten(struct yflame_frame *node, struct yflame_frame **nodes, uint32_t *next_id)
{
    node->id = *next_id;
    nodes[*next_id] = node;
    (*next_id)++;
    for (size_t i = 0; i < node->child_count; i++) {
        flatten(node->children[i], nodes, next_id);
    }
}

/*=============================================================================
 * Layout — lay out the focus subtree into [x0,x1], focus at depth 0.
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

static struct yflame_frame *flame_focus_node(struct flame_data *flame)
{
    int32_t id = flame->focus_id;
    if (id < 0 || (uint32_t)id >= flame->node_count) {
        id = 0;
    }
    return flame->nodes ? flame->nodes[id] : NULL;
}

/* Re-lay out the focus subtree against the current width; update height. */
static struct yflame_frame *flame_relayout(struct flame_data *flame)
{
    struct yflame_frame *focus = flame_focus_node(flame);
    if (!focus) {
        return NULL;
    }
    float width = flame->width > 0.0f ? flame->width : (float)YFLAME_DEFAULT_WIDTH;
    float frame_height =
        flame->frame_height > 0.0f ? flame->frame_height : (float)YFLAME_DEFAULT_FRAME_HEIGHT;
    flame->max_depth = 0;
    layout(focus, 0.0f, width, 0, &flame->max_depth);
    /* Reserve the top bar for the buttons; the graph occupies the rest. */
    flame->total_height =
        (float)YFLAME_BAR_HEIGHT + (float)(flame->max_depth + 1) * frame_height;
    return focus;
}

/*=============================================================================
 * Colour + labels
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

/* ydraw/ygui colours are ABGR — byte0=R, byte1=G, byte2=B, byte3=A — so a
 * #RRGGBB packs as 0xAABBGGRR (#6BA892 → 0xFF92A86B). See ygui/theme.c. */
static uint32_t color_abgr(uint32_t r, uint32_t g, uint32_t b)
{
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

/* Brand palette: predominantly mint, converging toward red only for the heavy
 * (hot) frames. Each frame's base shade is interpolated between the two brand
 * mints — BRAND_ACCENT_DEEP #5A8979 (90,137,121) and BRAND_ACCENT_BRIGHT
 * #74C5A5 (116,197,165) — by a per-name hash, then lerped toward red in
 * proportion to the frame's sample weight (fraction of the whole profile),
 * biased (frac²) so only genuinely hot paths redden. */
static uint32_t frame_color(const char *name, uint64_t value, uint64_t root_value)
{
    uint32_t hash = name_hash(name);
    float shade = (float)(hash & 0xFFu) / 255.0f; /* 0..1 within the mint band */
    float r = 90.0f + shade * (116.0f - 90.0f);   /* BRAND_ACCENT_DEEP → _BRIGHT */
    float g = 137.0f + shade * (197.0f - 137.0f);
    float b = 121.0f + shade * (165.0f - 121.0f);

    float frac = root_value > 0u ? (float)((double)value / (double)root_value) : 0.0f;
    if (frac > 1.0f) {
        frac = 1.0f;
    }
    float t = frac * frac; /* stay mint unless the frame is genuinely hot */
    r = r + t * (214.0f - r);
    g = g + t * (69.0f - g);
    b = b + t * (65.0f - b);
    return color_abgr((uint32_t)r, (uint32_t)g, (uint32_t)b);
}

/* Back `len` off any trailing UTF-8 continuation byte. */
static size_t utf8_clamp(const char *text, size_t len)
{
    while (len > 0 && ((uint8_t)text[len] & 0xC0u) == 0x80u) {
        len--;
    }
    return len;
}

/*=============================================================================
 * Emit — one box (+ optional label) per visible frame in the focus subtree.
 *===========================================================================*/

static struct yetty_ycore_void_result emit(struct yetty_ydraw_drawable_list *buf,
                                          const struct yflame_frame *node,
                                          const struct flame_data *flame, float frame_height,
                                          float min_width, int icicle, int labels,
                                          int32_t highlight_id, uint32_t *z)
{
    float box_width = node->x_end - node->x_start;
    if (box_width >= min_width) {
        float top;
        if (icicle) {
            top = (float)YFLAME_BAR_HEIGHT + (float)node->depth * frame_height;
        } else {
            /* total_height already includes the bar, so the deepest row lands
             * just below it and the root sits at the bottom. */
            top = flame->total_height - (float)(node->depth + 1) * frame_height;
        }

        float gap = (float)YFLAME_BOX_GAP_NUM;
        float half_w = (box_width - gap) * 0.5f;
        float half_h = (frame_height - gap) * 0.5f;
        if (half_w < 0.0f) {
            half_w = box_width * 0.5f;
        }
        if (half_h < 0.0f) {
            half_h = frame_height * 0.5f;
        }

        int highlighted = ((int32_t)node->id == highlight_id);
        struct yetty_ysdf_box geom = {
            .center_x = (node->x_start + node->x_end) * 0.5f,
            .center_y = top + frame_height * 0.5f,
            .half_width = half_w,
            .half_height = half_h,
            .corner_radius = 2.0f,
        };
        /* Hovered box gets a bright-mint outline (BRAND_ACCENT_BRIGHT). */
        uint32_t stroke_color = highlighted ? 0xFFA5C574u : 0u;
        float stroke_width = highlighted ? 2.0f : 0.0f;
        uint64_t root_value = flame->root ? flame->root->value : node->value;
        struct yetty_ycore_void_result box_result = yetty_ydraw_drawable_list_add_cmd_add_box(
            buf, /*id=*/0, /*z_order=*/(*z)++, frame_color(node->name, node->value, root_value),
            stroke_color, stroke_width, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, box_result, "yflame: add box");

        if (labels) {
            float font_size = frame_height * 0.74f;
            float advance = font_size * 0.6f;
            float avail = box_width - 2.0f * (float)YFLAME_TEXT_PAD;
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
                float text_x = node->x_start + (float)YFLAME_TEXT_PAD;
                float text_y = top + frame_height * 0.5f + font_size / 3.0f;
                /* Dark label (BRAND_BG #0B1014) for contrast on the mint box. */
                struct yetty_ycore_void_result text_result = yetty_ydraw_drawable_list_add_text(
                    buf, text_x, text_y, &text_view, font_size, /*color=*/0xFF14100Bu,
                    /*layer=*/(*z)++, /*font_id=*/-1, /*rotation=*/0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, text_result, "yflame: add label");
            }
        }
    }

    for (size_t i = 0; i < node->child_count; i++) {
        struct yetty_ycore_void_result child_result =
            emit(buf, node->children[i], flame, frame_height, min_width, icicle, labels,
                 highlight_id, z);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, child_result, "yflame: emit child");
    }
    return YETTY_OK_VOID();
}

/* Find the visible frame at (x, target_depth) within the focus subtree. */
static const struct yflame_frame *hit_find(const struct yflame_frame *node, float x,
                                          uint32_t target_depth)
{
    if (node->depth == target_depth) {
        if (x >= node->x_start && x < node->x_end) {
            return node;
        }
        return NULL;
    }
    if (node->depth < target_depth) {
        for (size_t i = 0; i < node->child_count; i++) {
            const struct yflame_frame *found = hit_find(node->children[i], x, target_depth);
            if (found) {
                return found;
            }
        }
    }
    return NULL;
}

/*=============================================================================
 * Navigation buttons — a top-left overlay: "up" (one level) and "root".
 * Drawn as the same SDF box + MSDF text as the frames, just chrome-coloured.
 *===========================================================================*/

static void button_rect(int idx, float *x0, float *y0, float *x1, float *y1)
{
    *y0 = (float)YFLAME_BTN_PAD;
    *y1 = (float)(YFLAME_BTN_PAD + YFLAME_BTN_HEIGHT);
    *x0 = (float)(YFLAME_BTN_PAD + idx * (YFLAME_BTN_WIDTH + YFLAME_BTN_PAD));
    *x1 = *x0 + (float)YFLAME_BTN_WIDTH;
}

static struct yetty_ycore_void_result emit_button(struct yetty_ydraw_drawable_list *buf, int idx,
                                                  const char *label, uint32_t *z)
{
    float x0, y0, x1, y1;
    button_rect(idx, &x0, &y0, &x1, &y1);
    struct yetty_ysdf_box geom = {
        .center_x = (x0 + x1) * 0.5f,
        .center_y = (y0 + y1) * 0.5f,
        .half_width = (x1 - x0) * 0.5f,
        .half_height = (y1 - y0) * 0.5f,
        .corner_radius = 3.0f,
    };
    /* BRAND_BG_ROW fill, BRAND_ACCENT (mint) outline — reads as chrome (ABGR). */
    struct yetty_ycore_void_result box_result = yetty_ydraw_drawable_list_add_cmd_add_box(
        buf, /*id=*/0, /*z_order=*/(*z)++, /*fill=*/0xFF2C261Eu, /*stroke=*/0xFF92A86Bu,
        /*stroke_width=*/1.5f, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, box_result, "yflame: button box");

    size_t label_len = strlen(label);
    struct yetty_ycore_buffer text_view = {
        .data = (uint8_t *)(uintptr_t)label,
        .capacity = label_len,
        .size = label_len,
    };
    float font_size = (float)YFLAME_BTN_HEIGHT * 0.5f;
    /* BRAND_TEXT_PRIMARY #E0E5E4 (ABGR). */
    struct yetty_ycore_void_result text_result = yetty_ydraw_drawable_list_add_text(
        buf, x0 + (float)YFLAME_BTN_PAD * 2.0f, (y0 + y1) * 0.5f + font_size / 3.0f, &text_view,
        font_size, /*color=*/0xFFE4E5E0u, /*layer=*/(*z)++, /*font_id=*/-1, /*rotation=*/0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_result, "yflame: button label");
    return YETTY_OK_VOID();
}

/* Returns 1 if (x,y) is on the "up" button, 2 for "root", else 0. */
static int button_hit(float x, float y)
{
    for (int i = 0; i < 2; i++) {
        float x0, y0, x1, y1;
        button_rect(i, &x0, &y0, &x1, &y1);
        if (x >= x0 && x < x1 && y >= y0 && y < y1) {
            return i + 1;
        }
    }
    return 0;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* configure: set graph width, row height, min visible box width, and flags.
 * 0 selects the default for each. Call after create(), before parse(). */
[[clang::annotate("override@yflame:flame:configure")]] [[clang::annotate(
    "local@yflame:configure")]]
static struct yetty_ycore_void_result flame_configure(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float width,
                                                      float frame_height, float min_width,
                                                      uint32_t flags)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (!flame) {
        return YETTY_ERR(yetty_ycore_void, "yflame configure: bad object");
    }
    flame->width = width;
    flame->frame_height = frame_height;
    flame->min_width = min_width;
    flame->flags = flags ? flags : YETTY_YFLAME_FLAG_LABELS;
    return YETTY_OK_VOID();
}

/* parse: ingest folded-stack text, build the call tree, index it. Resets focus
 * to the root and clears any hover highlight. */
[[clang::annotate("override@yflame:flame:parse")]] [[clang::annotate("local@yflame:parse")]]
static struct yetty_ycore_void_result flame_parse(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj,
                                                  const char *input, size_t len)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (!flame) {
        return YETTY_ERR(yetty_ycore_void, "yflame parse: bad object");
    }
    if (!input && len > 0) {
        return YETTY_ERR(yetty_ycore_void, "yflame parse: NULL input");
    }

    /* Drop any previous tree (re-parse). */
    frame_destroy(flame->root);
    free(flame->nodes);
    flame->root = NULL;
    flame->nodes = NULL;
    flame->node_count = 0;

    struct yflame_frame *root = frame_create("all", 3);
    if (!root) {
        return YETTY_ERR(yetty_ycore_void, "yflame parse: out of memory");
    }
    struct yetty_ycore_void_result parsed = parse_folded(input, len, root);
    if (YETTY_IS_ERR(parsed)) {
        frame_destroy(root);
        return YETTY_ERR(yetty_ycore_void, "yflame parse: failed", parsed);
    }
    if (root->value == 0) {
        frame_destroy(root);
        return YETTY_ERR(yetty_ycore_void, "yflame parse: no samples in input");
    }

    frame_sort(root);
    uint32_t node_count = count_nodes(root);
    struct yflame_frame **nodes = malloc((size_t)node_count * sizeof(struct yflame_frame *));
    if (!nodes) {
        frame_destroy(root);
        return YETTY_ERR(yetty_ycore_void, "yflame parse: node index alloc");
    }
    uint32_t next_id = 0;
    flatten(root, nodes, &next_id);

    flame->root = root;
    flame->nodes = nodes;
    flame->node_count = node_count;
    flame->focus_id = 0;
    flame->highlight_id = -1;
    return YETTY_OK_VOID();
}

/* render: lay out the focus subtree and emit it as a fresh ydraw drawable list
 * (caller owns it). Pointer return -> local-only. */
[[clang::annotate("override@yflame:flame:render")]] [[clang::annotate("local@yflame:render")]]
static struct yetty_ydraw_drawable_list_result flame_render(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (!flame || !flame->root) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame render: nothing parsed");
    }
    struct yflame_frame *focus = flame_relayout(flame);
    if (!focus) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame render: layout failed");
    }
    ydebug("yflame render: focus_id=%d focus_name=%s max_depth=%u frame_h=%.2f total_h=%.1f",
           flame->focus_id, focus->name, flame->max_depth, (double)flame->frame_height,
           (double)flame->total_height);
    float width = flame->width > 0.0f ? flame->width : (float)YFLAME_DEFAULT_WIDTH;
    float frame_height =
        flame->frame_height > 0.0f ? flame->frame_height : (float)YFLAME_DEFAULT_FRAME_HEIGHT;
    float min_width = flame->min_width > 0.0f ? flame->min_width : 0.5f;
    int icicle = (flame->flags & YETTY_YFLAME_FLAG_ICICLE) != 0;
    int labels = (flame->flags & YETTY_YFLAME_FLAG_LABELS) != 0;

    struct yetty_ydraw_drawable_list_config buffer_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = width,
        .scene_max_y = flame->total_height,
    };
    struct yetty_ydraw_drawable_list_result list_r =
        yetty_ydraw_drawable_list_config_buffer_create(&buffer_config);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_r, "yflame render: list create");

    uint32_t z = 0;
    struct yetty_ycore_void_result emitted = emit(list_r.value, focus, flame, frame_height,
                                                 min_width, icicle, labels, flame->highlight_id, &z);
    if (YETTY_IS_ERR(emitted)) {
        yetty_ydraw_drawable_list_destroy(list_r.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame render: emit", emitted);
    }

    /* Navigation buttons last → highest z → drawn on top of the frames. */
    struct yetty_ycore_void_result up_btn = emit_button(list_r.value, 0, "up", &z);
    if (YETTY_IS_ERR(up_btn)) {
        yetty_ydraw_drawable_list_destroy(list_r.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame render: up button", up_btn);
    }
    struct yetty_ycore_void_result root_btn = emit_button(list_r.value, 1, "root", &z);
    if (YETTY_IS_ERR(root_btn)) {
        yetty_ydraw_drawable_list_destroy(list_r.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yflame render: root button", root_btn);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, list_r.value);
}

/* hit_test: id of the frame at content-coordinate (x,y) in the current focus
 * layout, or -1 if none. */
[[clang::annotate("override@yflame:flame:hit_test")]] [[clang::annotate("local@yflame:hit_test")]]
static struct yetty_ycore_int_result flame_hit_test(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float x,
                                                    float y)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (!flame || !flame->root) {
        return YETTY_ERR(yetty_ycore_int, "yflame hit_test: nothing parsed");
    }
    /* The navigation buttons are a fixed overlay — test them before the tree. */
    int btn = button_hit(x, y);
    if (btn == 1) {
        return YETTY_OK(yetty_ycore_int, YETTY_YFLAME_HIT_UP);
    }
    if (btn == 2) {
        return YETTY_OK(yetty_ycore_int, YETTY_YFLAME_HIT_ROOT);
    }
    struct yflame_frame *focus = flame_relayout(flame);
    if (!focus) {
        return YETTY_OK(yetty_ycore_int, -1);
    }
    float frame_height =
        flame->frame_height > 0.0f ? flame->frame_height : (float)YFLAME_DEFAULT_FRAME_HEIGHT;
    int icicle = (flame->flags & YETTY_YFLAME_FLAG_ICICLE) != 0;
    /* The top bar (above the graph) holds only the buttons — no frames there. */
    if (y < (float)YFLAME_BAR_HEIGHT || y >= flame->total_height) {
        return YETTY_OK(yetty_ycore_int, -1);
    }
    uint32_t target_depth;
    if (icicle) {
        target_depth = (uint32_t)((y - (float)YFLAME_BAR_HEIGHT) / frame_height);
    } else {
        target_depth = (uint32_t)((flame->total_height - y) / frame_height);
    }
    const struct yflame_frame *hit = hit_find(focus, x, target_depth);
    return YETTY_OK(yetty_ycore_int, hit ? (int)hit->id : -1);
}

/* focus: zoom so the given node fills the full width (its subtree is shown). */
[[clang::annotate("override@yflame:flame:focus")]] [[clang::annotate("local@yflame:focus")]]
static struct yetty_ycore_void_result flame_focus(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj, int32_t node_id)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (!flame) {
        return YETTY_ERR(yetty_ycore_void, "yflame focus: bad object");
    }
    ydebug("yflame focus: %d -> %d (node_count=%u)", flame->focus_id, node_id, flame->node_count);
    if (node_id >= 0 && (uint32_t)node_id < flame->node_count) {
        flame->focus_id = node_id;
    }
    return YETTY_OK_VOID();
}

/* focus_parent: zoom out one level (focus the current node's parent). */
[[clang::annotate("override@yflame:flame:focus_parent")]] [[clang::annotate(
    "local@yflame:focus_parent")]]
static struct yetty_ycore_void_result flame_focus_parent(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (!flame) {
        return YETTY_ERR(yetty_ycore_void, "yflame focus_parent: bad object");
    }
    struct yflame_frame *node = flame_focus_node(flame);
    ydebug("yflame focus_parent: focus_id=%d node=%s parent=%s", flame->focus_id,
           node ? node->name : "(null)", (node && node->parent) ? node->parent->name : "(none)");
    if (node && node->parent) {
        flame->focus_id = (int32_t)node->parent->id;
    }
    return YETTY_OK_VOID();
}

/* reset: zoom back out to the whole graph (root). */
[[clang::annotate("override@yflame:flame:reset")]] [[clang::annotate("local@yflame:reset")]]
static struct yetty_ycore_void_result flame_reset(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (!flame) {
        return YETTY_ERR(yetty_ycore_void, "yflame reset: bad object");
    }
    flame->focus_id = 0;
    return YETTY_OK_VOID();
}

/* set_highlight: mark a node as hovered (-1 clears) for the next render. */
[[clang::annotate("override@yflame:flame:set_highlight")]] [[clang::annotate(
    "local@yflame:set_highlight")]]
static struct yetty_ycore_void_result flame_set_highlight(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          int32_t node_id)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (!flame) {
        return YETTY_ERR(yetty_ycore_void, "yflame set_highlight: bad object");
    }
    flame->highlight_id = (node_id >= 0 && (uint32_t)node_id < flame->node_count) ? node_id : -1;
    return YETTY_OK_VOID();
}

/* destroy: free the tree, the node index, and the object. */
[[clang::annotate("override@yflame:flame:destroy")]] [[clang::annotate("local@yflame:destroy")]]
static struct yetty_ycore_void_result flame_obj_destroy(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct flame_data *flame = flame_from_obj(obj);
    if (flame) {
        frame_destroy(flame->root);
        free(flame->nodes);
        flame->root = NULL;
        flame->nodes = NULL;
        flame->node_count = 0;
    }
    return yetty_yclass_object_free(obj);
}

/*=============================================================================
 * One-shot helper — serialize a rendered drawable list as a YDRAW_BIN OSC
 * envelope (the ycat/scrolling path). Exposed as a free function for the CLI.
 *===========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yflame_emit_osc(const struct yetty_ydraw_drawable_list *list,
                                                     int fd)
{
    if (!list) {
        return YETTY_ERR(yetty_ycore_void, "yflame emit_osc: NULL list");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)list, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "yflame emit_osc: empty serialize");
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_void_result emit_result = yetty_yface_emit_to_fd(
        fd, YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_result, "yflame emit_osc: yface_emit_to_fd");
    return YETTY_OK_VOID();
}

#include "flame.gen.c"
