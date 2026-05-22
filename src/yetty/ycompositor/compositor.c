/*
 * compositor.c — standalone positioned-figure compositor.
 *
 * No terminal_layer base, no vtable. The terminal calls into us via
 * the direct API in compositor.h. Wire decoding uses ydraw-core's
 * yetty_ydraw_drawable_iterator for the outer envelope walk and
 * yetty_ydraw_drawable_command_parse for the inner CMD_GROUP body
 * walk — they stay in ydraw-core (also used by scrolling-canvas).
 *
 * Wire model — new YCOMPOSITOR_BIN envelope:
 *
 *   CMD_GROUP id=W:   create a yfigure_group at rect (x, y, w, h)
 *                     and a child ygrid figure inside it. Body
 *                     contains prims with coords LOCAL to (x, y).
 *                     Error if id already exists.
 *
 *   CMD_UPDATE id=W:  re-emit existing group W (new rect + new
 *                     body). Error if id not present.
 *
 *   CMD_DELETE id=W:  remove group W. Error if not present.
 *
 *   CMD_ZERO:         clear every figure on the compositor.
 *
 * CMD_GROUP / CMD_UPDATE payload layout (canonical drawable shape;
 * groups carry the same style header so geometry sits at the
 * standard word offset):
 *
 *   u32 z_order        (unused for groups, =0)
 *   u32 fill_color     (unused, =0)
 *   u32 stroke_color   (unused, =0)
 *   u32 stroke_width   (unused, =0)
 *   f32 x              ← group origin
 *   f32 y
 *   f32 w
 *   f32 h
 *   bytes body[...]    ← nested record stream, coords local to (x, y)
 */

#include <yetty/ycompositor/compositor.h>

#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/ydraw-core/flyweight.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ysdf/handler.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ywire/wire-statemachine.h>

/*===========================================================================
 * Per-id (group, ygrid) entry — linear table for v1, hash-map later.
 *=========================================================================*/

struct ygroup_entry {
    uint32_t id;
    struct yetty_yfigure_group *group;
    struct yetty_ygrid_grid *ygrid;
};

struct yetty_ycompositor {
    /* Pane geometry. */
    uint32_t cols, rows;
    float cell_w, cell_h;

    /* Stash for ygrid creation (needs GPU device/queue/format etc.). */
    const struct yetty_context *context;

    /* Z-ordered top-level figures. Owned. The compositor calls
     * f->ops->destroy on each on _destroy. */
    struct yetty_yfigure_figure **figures;
    size_t figure_count, figure_cap;

    /* id → (group, ygrid). Borrowed pointers — `group` is also in
     * figures[] and owns the ygrid as a child. */
    struct ygroup_entry *groups;
    size_t group_count, group_cap;

    /* Damage region accumulated across the frame. */
    struct yetty_ycore_rectangle damage_union;
    int damage_active;
    int dirty;

    /* Flyweight registry for the wire iterator. Built once at create. */
    struct yetty_ydraw_flyweight_registry *registry;

    /* request_render callback (set by terminal). */
    yetty_ycompositor_request_render_fn request_render_fn;
    void *request_render_userdata;

    /* Default font attached at slot 0 to every per-group ygrid the
     * compositor creates. Borrowed — set by the host via
     * yetty_ycompositor_set_default_font; the host owns lifetime and
     * must outlive every group that uses it. NULL = no default font;
     * groups with TEXT_SPAN content will silently drop glyphs in that
     * case. */
    struct yetty_ydraw_font *default_font;

    /* Offset of the compositor's render area within the target. Wire
     * rects come in pane-local; this is added to convert them to
     * absolute target pixel space. Every stored figure rect is already
     * absolute — i.e. `pane_local + viewport_offset`. */
    float viewport_offset_x;
    float viewport_offset_y;
};

/*===========================================================================
 * Wire payload constants — match the layout documented at top.
 *=========================================================================*/

/* In-payload header that prefixes the body of every CMD_GROUP /
 * CMD_UPDATE record. The first four words are the canonical drawable
 * style header (always zero for groups — kept so geometry sits at the
 * same word offset as any SDF prim's geometry). The last four are the
 * group's rect as f32, parent-relative.
 *
 * Single struct lives here so the byte offsets aren't duplicated
 * across parse_group_payload + feed_group_body. The _Static_assert
 * catches any platform / packing surprise at compile time. */
struct compositor_group_header {
    uint32_t z_order;
    uint32_t fill_color;
    uint32_t stroke_color;
    uint32_t stroke_width;
    float x, y, w, h;
};
_Static_assert(sizeof(struct compositor_group_header) == 32,
               "CMD_GROUP header must be exactly 32 bytes (style + rect)");

#define GROUP_HEADER_BYTES (sizeof(struct compositor_group_header))

/* Glyph type — same as ydraw-layer's YDRAW_SDF_GLYPH. Used to decide
 * whether a record entering a group's ygrid is renderable. */
#define GLYPH_TYPE 200u

/*===========================================================================
 * Rect helpers
 *=========================================================================*/

static inline int rect_is_empty(struct yetty_ycore_rectangle r)
{
    return r.max.x <= r.min.x || r.max.y <= r.min.y;
}

static inline int rect_overlaps(struct yetty_ycore_rectangle a, struct yetty_ycore_rectangle b)
{
    return !(a.max.x <= b.min.x || a.min.x >= b.max.x ||
             a.max.y <= b.min.y || a.min.y >= b.max.y);
}

static inline struct yetty_ycore_rectangle rect_union(
    struct yetty_ycore_rectangle a, struct yetty_ycore_rectangle b)
{
    if (rect_is_empty(a)) return b;
    if (rect_is_empty(b)) return a;
    struct yetty_ycore_rectangle r;
    r.min.x = a.min.x < b.min.x ? a.min.x : b.min.x;
    r.min.y = a.min.y < b.min.y ? a.min.y : b.min.y;
    r.max.x = a.max.x > b.max.x ? a.max.x : b.max.x;
    r.max.y = a.max.y > b.max.y ? a.max.y : b.max.y;
    return r;
}

static void damage_add(struct yetty_ycompositor *c, struct yetty_ycore_rectangle r)
{
    if (rect_is_empty(r)) return;
    if (!c->damage_active) {
        c->damage_union = r;
        c->damage_active = 1;
    } else {
        c->damage_union = rect_union(c->damage_union, r);
    }
    c->dirty = 1;
}

/*===========================================================================
 * Figure list helpers
 *=========================================================================*/

static ssize_t find_figure_index(const struct yetty_ycompositor *c,
                                 const struct yetty_yfigure_figure *f)
{
    for (size_t i = 0; i < c->figure_count; ++i)
        if (c->figures[i] == f)
            return (ssize_t)i;
    return -1;
}

static struct yetty_ycore_void_result grow_figures(struct yetty_ycompositor *c)
{
    if (c->figure_count < c->figure_cap)
        return YETTY_OK_VOID();
    size_t cap = c->figure_cap ? c->figure_cap * 2u : 8u;
    struct yetty_yfigure_figure **grown =
        (struct yetty_yfigure_figure **)realloc(c->figures, cap * sizeof(*grown));
    if (!grown)
        return YETTY_ERR(yetty_ycore_void, "ycompositor: figure table oom");
    c->figures = grown;
    c->figure_cap = cap;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Group map helpers
 *=========================================================================*/

static ssize_t find_group_index(const struct yetty_ycompositor *c, uint32_t id)
{
    for (size_t i = 0; i < c->group_count; ++i)
        if (c->groups[i].id == id)
            return (ssize_t)i;
    return -1;
}

static struct yetty_ycore_void_result grow_groups(struct yetty_ycompositor *c)
{
    if (c->group_count < c->group_cap)
        return YETTY_OK_VOID();
    size_t cap = c->group_cap ? c->group_cap * 2u : 8u;
    struct ygroup_entry *grown =
        (struct ygroup_entry *)realloc(c->groups, cap * sizeof(*grown));
    if (!grown)
        return YETTY_ERR(yetty_ycore_void, "ycompositor: group table oom");
    c->groups = grown;
    c->group_cap = cap;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Header parsing for CMD_GROUP / CMD_UPDATE payload
 *=========================================================================*/

struct group_payload {
    float x, y, w, h;
    const uint8_t *body;
    uint32_t body_len;
};

static struct yetty_ycore_void_result parse_group_payload(
    const uint8_t *payload, uint32_t payload_size, struct group_payload *out)
{
    if (payload_size < GROUP_HEADER_BYTES)
        return YETTY_ERR(yetty_ycore_void, "ycompositor: group payload too small for header");
    struct compositor_group_header hdr;
    memcpy(&hdr, payload, sizeof(hdr));
    out->x = hdr.x;
    out->y = hdr.y;
    out->w = hdr.w;
    out->h = hdr.h;
    out->body = payload + GROUP_HEADER_BYTES;
    out->body_len = payload_size - GROUP_HEADER_BYTES;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Body walker — iterate prims inside a CMD_GROUP body and feed the
 * group's ygrid. The body itself uses the same FAM record stream as
 * the outer envelope; we use ydraw-core's _command_parse to step
 * through it.
 *=========================================================================*/

/* Translate an SDF prim's position fields by (dx, dy) in place.
 * Mirrors scene-canvas's translate_sdf_prim_inplace — every SDF prim
 * has geom[0..1] as a position pair; multi-position prims carry extra
 * pairs at fixed offsets. Generic-translator candidate for shared
 * yrender util once we have more callers. */
static void compositor_translate_sdf_inplace(uint32_t *prim_words, uint32_t word_count,
                                             float dx, float dy)
{
    if (word_count < 7u)
        return;
    uint32_t type = prim_words[0];
    float *geom = (float *)&prim_words[5];
    geom[0] += dx;
    geom[1] += dy;
    switch (type) {
    case YETTY_YSDF_SEGMENT:
        if (word_count >= 9u) { geom[2] += dx; geom[3] += dy; }
        break;
    case YETTY_YSDF_TRIANGLE:
        if (word_count >= 11u) {
            geom[2] += dx; geom[3] += dy;
            geom[4] += dx; geom[5] += dy;
        }
        break;
    case YETTY_YSDF_LINEAR_GRADIENT_BOX:
        if (word_count >= 14u) {
            geom[5] += dx; geom[6] += dy;
            geom[7] += dx; geom[8] += dy;
        }
        break;
    case YETTY_YSDF_RADIAL_GRADIENT_BOX:
        if (word_count >= 12u) { geom[5] += dx; geom[6] += dy; }
        break;
    default:
        break;
    }
}

/* Translate a GLYPH wire record's x/y in place. The wire layout has
 * x at word index 2, y at index 3 (matches scrolling-canvas's
 * YDRAW_GLYPH_WORDS / SCENE_GLYPH_WORDS shape). */
static void compositor_translate_glyph_inplace(uint32_t *prim_words, uint32_t word_count,
                                               float dx, float dy)
{
    if (word_count < 7u)
        return;
    float *fx = (float *)&prim_words[2];
    float *fy = (float *)&prim_words[3];
    *fx += dx;
    *fy += dy;
}

/* Walk a group's body. `origin_x, origin_y` is the absolute offset
 * accumulated from the parent chain so far; prims added to the ygrid
 * are translated by it before they hit add_record.
 *
 * Nested CMD_GROUP records are walked recursively into the SAME ygrid
 * (ent->ygrid), accumulating their parent-relative rects into the
 * origin. This is the "one ygrid per top-level window, many nested
 * groups inside" model — children's coordinate frames are honoured
 * but everything paints to one shared spatial bucket. */
#define COMPOSITOR_MAX_PRIM_WORDS     32u

static struct yetty_ycore_void_result feed_group_body(
    struct yetty_ycompositor *c, struct ygroup_entry *ent,
    const uint8_t *body, uint32_t body_len, float origin_x, float origin_y)
{
    uint32_t off = 0;
    while (off < body_len) {
        struct yetty_ydraw_command cmd;
        struct yetty_ycore_size_result pr = yetty_ydraw_drawable_command_parse(
            c->registry, body + off, body_len - off, &cmd);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr,
                            "ycompositor: group body parse");
        if (pr.value == 0)
            return YETTY_ERR(yetty_ycore_void, "ycompositor: parser made no progress");

        if (cmd.kind == YETTY_YDRAW_COMMAND_ADD) {
            uint32_t type = cmd.flyweight.data[0];
            if (type == YETTY_YDRAW_CMD_GROUP) {
                /* Nested group — read its with-rect header (if present)
                 * to accumulate the parent-relative rect into origin
                 * and recurse into the nested body. The nested group's
                 * prims flow into the SAME ygrid as the outer parent
                 * (one ygrid per top-level window). */
                uint32_t nested_payload_size;
                memcpy(&nested_payload_size,
                       (const uint8_t *)cmd.flyweight.data + 8, 4);
                const uint8_t *nested_header =
                    (const uint8_t *)cmd.flyweight.data + 12;
                const uint8_t *nested_body;
                uint32_t nested_body_len;
                float child_origin_x = origin_x;
                float child_origin_y = origin_y;
                if (nested_payload_size >= GROUP_HEADER_BYTES) {
                    struct compositor_group_header hdr;
                    memcpy(&hdr, nested_header, sizeof(hdr));
                    child_origin_x += hdr.x;
                    child_origin_y += hdr.y;
                    nested_body = nested_header + GROUP_HEADER_BYTES;
                    nested_body_len = nested_payload_size - GROUP_HEADER_BYTES;
                } else {
                    nested_body = nested_header;
                    nested_body_len = nested_payload_size;
                }
                struct yetty_ycore_void_result nr = feed_group_body(
                    c, ent, nested_body, nested_body_len,
                    child_origin_x, child_origin_y);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, nr,
                                    "ycompositor: nested group");
            } else if (type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
                /* TEXT_SPAN: shift x, y in a heap copy so the ygrid's
                 * expansion uses absolute coords. The wire bytes are
                 * const, so we copy. ts->x at payload byte 0,
                 * ts->y at byte 4 (just past the 8-byte header). */
                size_t rec_size = (size_t)pr.value;
                uint8_t *copy = (uint8_t *)malloc(rec_size);
                if (!copy)
                    return YETTY_ERR(yetty_ycore_void,
                                     "ycompositor: TEXT_SPAN copy oom");
                memcpy(copy, body + off, rec_size);
                if (origin_x != 0.0f || origin_y != 0.0f) {
                    float ts_x, ts_y;
                    memcpy(&ts_x, copy + 8 + 0, 4);
                    memcpy(&ts_y, copy + 8 + 4, 4);
                    ts_x += origin_x;
                    ts_y += origin_y;
                    memcpy(copy + 8 + 0, &ts_x, 4);
                    memcpy(copy + 8 + 4, &ts_y, 4);
                }
                struct yetty_ycore_void_result ar =
                    yetty_ygrid_add_record(ent->ygrid, copy, rec_size);
                free(copy);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ar,
                                    "ycompositor: ygrid add_record (TEXT_SPAN)");
            } else {
                /* SDF prim or GLYPH — translate positions by origin
                 * before handing to the ygrid. Wire bytes are const so
                 * copy into a stack buffer. SDF max word count is 16
                 * (LINEAR_GRADIENT_BOX); GLYPH is 7. */
                size_t rec_size = (size_t)pr.value;
                size_t word_count = rec_size / sizeof(uint32_t);
                if (word_count == 0 || word_count > COMPOSITOR_MAX_PRIM_WORDS) {
                    /* Unsupported prim shape — pass through unmodified
                     * (preserves today's drop-unknown semantics). */
                    struct yetty_ycore_void_result ar = yetty_ygrid_add_record(
                        ent->ygrid, body + off, rec_size);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar,
                                        "ycompositor: ygrid add_record (passthrough)");
                } else {
                    uint32_t scratch[COMPOSITOR_MAX_PRIM_WORDS];
                    memcpy(scratch, body + off, rec_size);
                    if (origin_x != 0.0f || origin_y != 0.0f) {
                        if (type == GLYPH_TYPE) {
                            compositor_translate_glyph_inplace(
                                scratch, (uint32_t)word_count, origin_x, origin_y);
                        } else {
                            compositor_translate_sdf_inplace(
                                scratch, (uint32_t)word_count, origin_x, origin_y);
                        }
                    }
                    struct yetty_ycore_void_result ar = yetty_ygrid_add_record(
                        ent->ygrid, (const uint8_t *)scratch, rec_size);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar,
                                        "ycompositor: ygrid add_record");
                }
            }
        }
        /* DELETE/UPDATE inside a group body: not v1; ignore. */

        off += (uint32_t)pr.value;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Group create / update / delete
 *=========================================================================*/

static struct yetty_ycore_void_result group_create(
    struct yetty_ycompositor *c, uint32_t id,
    const uint8_t *payload, uint32_t payload_size)
{
    if (find_group_index(c, id) >= 0)
        return YETTY_ERR(yetty_ycore_void, "ycompositor: CMD_GROUP id already exists");

    struct group_payload gp;
    struct yetty_ycore_void_result hr = parse_group_payload(payload, payload_size, &gp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "ycompositor: group_create parse");
    ydebug("ycompositor: group_create id=%u rect=(%.1f, %.1f, %.1fx%.1f) body=%u",
           id, gp.x, gp.y, gp.w, gp.h, gp.body_len);

    /* Translate pane-local wire rect to absolute target rect. */
    float abs_x = gp.x + c->viewport_offset_x;
    float abs_y = gp.y + c->viewport_offset_y;
    struct yetty_ycore_rectangle group_rect = {
        .min = {.x = abs_x, .y = abs_y},
        .max = {.x = abs_x + gp.w, .y = abs_y + gp.h},
    };
    struct yetty_yfigure_group_ptr_result grp_r = yetty_yfigure_group_create(group_rect);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grp_r, "ycompositor: yfigure_group_create");
    struct yetty_yfigure_group *grp = grp_r.value;

    /* The ygrid is the only child of the group for v1. Per the figure
     * rule, every figure->rect is in ABSOLUTE target pixel space — even
     * children of a group. The group is a lifecycle container, not a
     * coordinate frame, so the ygrid overlays the group's rect exactly. */
    struct yetty_ycore_rectangle ygrid_rect = group_rect;
    /* Grid cells for the ygrid's spatial bucketing — use the compositor's
     * cell size so prims bucket by terminal cell. */
    uint32_t gcols = c->cell_w > 0.0f ? (uint32_t)(gp.w / c->cell_w + 0.5f) : 1u;
    uint32_t grows = c->cell_h > 0.0f ? (uint32_t)(gp.h / c->cell_h + 0.5f) : 1u;
    if (gcols == 0) gcols = 1;
    if (grows == 0) grows = 1;
    struct yetty_ygrid_grid_ptr_result yg_r = yetty_ygrid_create(
        ygrid_rect, gcols, grows, c->context);
    if (YETTY_IS_ERR(yg_r)) {
        struct yetty_ycore_void_result dr =
            yetty_yfigure_group_as_figure(grp)->ops->destroy(yetty_yfigure_group_as_figure(grp));
        if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        return YETTY_ERR(yetty_ycore_void, "ycompositor: ygrid_create", yg_r);
    }
    struct yetty_ygrid_grid *yg = yg_r.value;
    struct yetty_yfigure_figure *yg_fig = yetty_ygrid_as_figure(yg);

    /* Attach the compositor's default font at slot 0 so TEXT_SPAN
     * records that arrive in the group body can be expanded into
     * renderable glyphs. ygrid borrows the pointer; the host (which
     * called yetty_ycompositor_set_default_font) owns lifetime. */
    if (c->default_font) {
        struct yetty_ycore_void_result fr =
            yetty_ygrid_set_font(yg, 0u, c->default_font);
        if (YETTY_IS_ERR(fr)) {
            ydebug("ycompositor: ygrid_set_font failed: %s", fr.error.msg);
            yetty_ycore_error_destroy(fr.error);
        }
    }

    struct yetty_ycore_void_result ar =
        yetty_yfigure_group_add_child(grp, yg_fig);
    if (YETTY_IS_ERR(ar)) {
        yg_fig->ops->destroy(yg_fig);
        struct yetty_yfigure_figure *grp_fig = yetty_yfigure_group_as_figure(grp);
        struct yetty_ycore_void_result dr = grp_fig->ops->destroy(grp_fig);
        if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        return YETTY_ERR(yetty_ycore_void, "ycompositor: group_add_child(ygrid)", ar);
    }

    /* Register the group as a top-level figure on the compositor. */
    struct yetty_yfigure_figure *grp_fig = yetty_yfigure_group_as_figure(grp);
    struct yetty_ycore_void_result fr = yetty_ycompositor_add_figure(c, grp_fig);
    if (YETTY_IS_ERR(fr)) {
        struct yetty_ycore_void_result dr = grp_fig->ops->destroy(grp_fig);
        if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        return YETTY_ERR(yetty_ycore_void, "ycompositor: add_figure(group)", fr);
    }

    /* Map id -> (group, ygrid). */
    struct yetty_ycore_void_result gg = grow_groups(c);
    if (YETTY_IS_ERR(gg)) {
        yetty_ycompositor_remove_figure(c, grp_fig);
        struct yetty_ycore_void_result dr = grp_fig->ops->destroy(grp_fig);
        if (YETTY_IS_ERR(dr)) yetty_ycore_error_destroy(dr.error);
        return YETTY_ERR(yetty_ycore_void, "ycompositor: grow_groups", gg);
    }
    c->groups[c->group_count++] = (struct ygroup_entry){
        .id = id, .group = grp, .ygrid = yg,
    };

    /* Feed body prims into the ygrid. The top-level group's rect is
     * already absorbed by the ygrid's figure rect (the figure handles
     * placement), so body prims start with origin (0, 0) — only
     * nested CMD_GROUPs inside the body contribute to origin. */
    struct yetty_ycore_void_result br = feed_group_body(c, &c->groups[c->group_count - 1],
                                                       gp.body, gp.body_len,
                                                       0.0f, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ycompositor: group_create body");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result group_update(
    struct yetty_ycompositor *c, uint32_t id,
    const uint8_t *payload, uint32_t payload_size)
{
    ssize_t idx = find_group_index(c, id);
    if (idx < 0)
        return YETTY_ERR(yetty_ycore_void, "ycompositor: CMD_UPDATE id not found");
    struct ygroup_entry *ent = &c->groups[idx];

    struct group_payload gp;
    struct yetty_ycore_void_result hr = parse_group_payload(payload, payload_size, &gp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "ycompositor: group_update parse");

    /* Translate pane-local wire rect to absolute target rect. */
    float abs_x = gp.x + c->viewport_offset_x;
    float abs_y = gp.y + c->viewport_offset_y;
    struct yetty_ycore_rectangle new_rect = {
        .min = {.x = abs_x, .y = abs_y},
        .max = {.x = abs_x + gp.w, .y = abs_y + gp.h},
    };
    struct yetty_ycore_void_result sr = yetty_ycompositor_set_figure_rect(
        c, yetty_yfigure_group_as_figure(ent->group), new_rect);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ycompositor: group_update set_rect");
    /* The ygrid child overlays the group's rect; keep them in sync so
     * ygrid_render sets the right viewport on the target. */
    struct yetty_yfigure_figure *yg_fig = yetty_ygrid_as_figure(ent->ygrid);
    yg_fig->rect = new_rect;
    yg_fig->dirty = 1;

    /* Wipe the existing prim store and refill. Same origin contract as
     * group_create: top-level rect lives on the figure, body starts
     * with origin (0, 0). */
    struct yetty_ycore_void_result cr = yetty_ygrid_clear(ent->ygrid);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "ycompositor: group_update clear ygrid");
    struct yetty_ycore_void_result br =
        feed_group_body(c, ent, gp.body, gp.body_len, 0.0f, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "ycompositor: group_update body");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result group_delete(
    struct yetty_ycompositor *c, uint32_t id)
{
    ssize_t idx = find_group_index(c, id);
    if (idx < 0) {
        /* Stale DELETE — the producer's pending_deletes queue can
         * carry ids the receiver no longer has (e.g. after a forced
         * full-redraw on this side wiped state the producer doesn't
         * know about). Don't bubble an error: the dispatch loop
         * treats every per-command error as fatal and would silently
         * drop the rest of the envelope, including any hover
         * redraw that follows the stale DELETE. Warn + continue. */
        ydebug("ycompositor: CMD_DELETE id=%u not found, ignoring", id);
        return YETTY_OK_VOID();
    }
    struct ygroup_entry ent = c->groups[idx];

    /* Pull from compositor's top-level figures (damages the rect). */
    struct yetty_yfigure_figure *grp_fig = yetty_yfigure_group_as_figure(ent.group);
    struct yetty_ycore_void_result rr = yetty_ycompositor_remove_figure(c, grp_fig);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ycompositor: group_delete remove_figure");

    /* Destroy cascade: group destroys ygrid child too. */
    struct yetty_ycore_void_result dr = grp_fig->ops->destroy(grp_fig);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "ycompositor: group_delete destroy");

    /* Shift map tail. */
    for (size_t i = (size_t)idx + 1; i < c->group_count; ++i)
        c->groups[i - 1] = c->groups[i];
    c->group_count--;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Wire envelope handler — drives ydraw-core's drawable_iterator across
 * one envelope and dispatches commands. Long-lived coroutine, same
 * shape as scene-canvas's scene_process_input.
 *=========================================================================*/

static struct yetty_ycore_void_result clear_all_groups(struct yetty_ycompositor *c)
{
    while (c->group_count > 0) {
        struct yetty_ycore_void_result dr = group_delete(c, c->groups[0].id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "ycompositor: clear_all_groups");
    }
    return YETTY_OK_VOID();
}

/* Dispatch one already-parsed wire command into the compositor's
 * group machinery. Factored out of process_input so the bytes-based
 * process_records entry point can share the same logic without going
 * through the wire-statemachine. */
static struct yetty_ycore_void_result dispatch_one_command(
    struct yetty_ycompositor *c, const struct yetty_ydraw_command *cmd)
{
    if (cmd->kind == YETTY_YDRAW_COMMAND_ADD) {
        uint32_t type = cmd->flyweight.data[0];
        ydebug("ycompositor: ADD type=0x%08x", type);
        if (type == YETTY_YDRAW_CMD_ZERO) {
            return clear_all_groups(c);
        }
        if (type == YETTY_YDRAW_CMD_GROUP) {
            uint32_t id, payload_size;
            memcpy(&id, (const uint8_t *)cmd->flyweight.data + 4, 4);
            memcpy(&payload_size, (const uint8_t *)cmd->flyweight.data + 8, 4);
            const uint8_t *payload = (const uint8_t *)cmd->flyweight.data + 12;
            ydebug("ycompositor: CMD_GROUP id=%u payload_size=%u", id, payload_size);
            return group_create(c, id, payload, payload_size);
        }
        /* Stray top-level prim (no enclosing group). v1 ignores. */
        ydebug("ycompositor: top-level prim type=0x%08x ignored (need CMD_GROUP)", type);
        return YETTY_OK_VOID();
    }
    if (cmd->kind == YETTY_YDRAW_COMMAND_DELETE) {
        return group_delete(c, cmd->id);
    }
    if (cmd->kind == YETTY_YDRAW_COMMAND_UPDATE) {
        return group_update(c, cmd->update.id, cmd->update.data, cmd->update.size);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ycompositor_process_records(
    struct yetty_ycompositor *c, const uint8_t *bytes, size_t bytes_len)
{
    if (!c)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_process_records: NULL compositor");
    if (bytes_len == 0)
        return YETTY_OK_VOID();
    if (!bytes)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_process_records: NULL bytes");

    size_t offset = 0;
    while (offset < bytes_len) {
        struct yetty_ydraw_command cmd;
        struct yetty_ycore_size_result pr = yetty_ydraw_drawable_command_parse(
            c->registry, bytes + offset, bytes_len - offset, &cmd);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ycompositor_process_records: parse");
        if (pr.value == 0)
            return YETTY_ERR(yetty_ycore_void,
                             "ycompositor_process_records: parser made no progress");
        struct yetty_ycore_void_result dr = dispatch_one_command(c, &cmd);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "ycompositor_process_records: dispatch");
        offset += (size_t)pr.value;
    }

    c->dirty = 1;
    if (c->request_render_fn) {
        struct yetty_ycore_void_result rr =
            c->request_render_fn(c->request_render_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                            "ycompositor_process_records: request_render");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ycompositor_process_input(
    struct yetty_ycompositor *c,
    struct yetty_ywire_wire_statemachine *sm)
{
    if (!c || !sm)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_process_input: NULL");
    ydebug("ycompositor_process_input: entered (compositor coro alive)");

    for (;;) {
        struct yetty_ydraw_drawable_iterator iter = {0};
        struct yetty_ycore_void_result ir =
            yetty_ydraw_drawable_iterator_init(&iter, sm, c->registry);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ir, "ycompositor: iter_init");
        ydebug("ycompositor_process_input: iter_init OK, starting envelope walk");

        struct yetty_ycore_void_result envelope_err = YETTY_OK_VOID();
        int envelope_had_err = 0;
        for (;;) {
            struct yetty_ydraw_drawable_iterator_status_result sr =
                yetty_ydraw_drawable_iterator_next(&iter);
            if (YETTY_IS_ERR(sr)) {
                envelope_err = YETTY_ERR(yetty_ycore_void, "ycompositor: iter_next", sr);
                envelope_had_err = 1;
                break;
            }
            if (sr.value == YETTY_YDRAW_ITERATOR_EOE)
                break;
            if (sr.value != YETTY_YDRAW_ITERATOR_OK) {
                envelope_err = YETTY_ERR(yetty_ycore_void,
                                          "ycompositor: iter_next unexpected status");
                envelope_had_err = 1;
                break;
            }

            struct yetty_ycore_void_result dr = dispatch_one_command(c, &iter.command);

            if (YETTY_IS_ERR(dr)) {
                envelope_err = YETTY_ERR(yetty_ycore_void, "ycompositor: cmd dispatch", dr);
                envelope_had_err = 1;
                break;
            }
        }
        yetty_ydraw_drawable_iterator_destroy(&iter);

        if (envelope_had_err)
            return envelope_err;

        /* Envelope complete — request a render frame. */
        c->dirty = 1;
        if (c->request_render_fn) {
            struct yetty_ycore_void_result rr =
                c->request_render_fn(c->request_render_userdata);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                                "ycompositor: request_render");
        }
        yetty_yplatform_coro_yield();
    }
}

/*===========================================================================
 * Render
 *=========================================================================*/

struct yetty_ycore_int_result yetty_ycompositor_render(
    struct yetty_ycompositor *c,
    struct yetty_ydraw_target *target, int force)
{
    if (!c)
        return YETTY_ERR(yetty_ycore_int, "ycompositor_render: NULL");
    ydebug("ycompositor_render: figures=%zu force=%d damage_active=%d",
           c->figure_count, force, c->damage_active);

    /* Fold per-figure dirty bits into damage. */
    for (size_t i = 0; i < c->figure_count; ++i)
        if (c->figures[i]->dirty)
            damage_add(c, c->figures[i]->rect);

    if (force)
        for (size_t i = 0; i < c->figure_count; ++i)
            damage_add(c, c->figures[i]->rect);

    /* No-figures path: there's nothing to render, but we must still
     * fall through to clear damage_active / dirty / damage_union below
     * — otherwise the residual damage from a CMD_DELETE that just
     * emptied us keeps `is_dirty()` true forever, the terminal forces
     * lower layers to repaint every single frame, and our own flags
     * never reset. */
    struct yetty_ycore_rectangle damage = c->damage_union;
    int drew = 0;
    if (c->damage_active) {
        for (size_t i = 0; i < c->figure_count; ++i) {
            struct yetty_yfigure_figure *f = c->figures[i];
            if (!rect_overlaps(f->rect, damage))
                continue;
            struct yetty_ycore_void_result r = f->ops->render(f, target);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "ycompositor_render: figure render");
            f->dirty = 0;
            drew = 1;
        }
    }

    c->damage_active = 0;
    c->damage_union = (struct yetty_ycore_rectangle){0};
    c->dirty = 0;
    return YETTY_OK(yetty_ycore_int, drew);
}

/*===========================================================================
 * Status getters
 *=========================================================================*/

int yetty_ycompositor_is_dirty(const struct yetty_ycompositor *c)
{
    if (!c) return 0;
    if (c->dirty || c->damage_active) return 1;
    for (size_t i = 0; i < c->figure_count; ++i)
        if (c->figures[i]->dirty) return 1;
    return 0;
}

int yetty_ycompositor_is_empty(const struct yetty_ycompositor *c)
{
    return !c || c->figure_count == 0;
}

/*===========================================================================
 * Resize
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ycompositor_resize(
    struct yetty_ycompositor *c,
    struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size)
{
    if (!c)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_resize: NULL");
    c->cols = grid_size.cols;
    c->rows = grid_size.rows;
    c->cell_w = cell_size.width;
    c->cell_h = cell_size.height;
    /* Mark every figure as needing a repaint into the (potentially
     * resized) target. */
    for (size_t i = 0; i < c->figure_count; ++i)
        damage_add(c, c->figures[i]->rect);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Figure management
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ycompositor_add_figure(
    struct yetty_ycompositor *c, struct yetty_yfigure_figure *f)
{
    if (!c || !f)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_add_figure: NULL arg");
    if (find_figure_index(c, f) >= 0)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_add_figure: already present");
    struct yetty_ycore_void_result g = grow_figures(c);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, g, "ycompositor_add_figure: grow_figures");
    c->figures[c->figure_count++] = f;
    f->dirty = 1;
    damage_add(c, f->rect);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ycompositor_remove_figure(
    struct yetty_ycompositor *c, struct yetty_yfigure_figure *f)
{
    if (!c || !f)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_remove_figure: NULL arg");
    ssize_t idx = find_figure_index(c, f);
    if (idx < 0)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_remove_figure: not present");
    damage_add(c, f->rect);
    for (size_t i = (size_t)idx + 1; i < c->figure_count; ++i)
        c->figures[i - 1] = c->figures[i];
    c->figure_count--;
    c->figures[c->figure_count] = NULL;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ycompositor_raise_figure(
    struct yetty_ycompositor *c, struct yetty_yfigure_figure *f)
{
    if (!c || !f)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_raise_figure: NULL arg");
    ssize_t idx = find_figure_index(c, f);
    if (idx < 0)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_raise_figure: not present");
    if ((size_t)idx == c->figure_count - 1)
        return YETTY_OK_VOID();
    for (size_t i = (size_t)idx + 1; i < c->figure_count; ++i)
        c->figures[i - 1] = c->figures[i];
    c->figures[c->figure_count - 1] = f;
    damage_add(c, f->rect);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ycompositor_set_figure_rect(
    struct yetty_ycompositor *c, struct yetty_yfigure_figure *f,
    struct yetty_ycore_rectangle new_rect)
{
    if (!c || !f)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_set_figure_rect: NULL arg");
    if (find_figure_index(c, f) < 0)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_set_figure_rect: not present");
    damage_add(c, f->rect);
    f->rect = new_rect;
    damage_add(c, new_rect);
    f->dirty = 1;
    return YETTY_OK_VOID();
}

void yetty_ycompositor_set_request_render(
    struct yetty_ycompositor *c,
    yetty_ycompositor_request_render_fn fn, void *user)
{
    if (!c) return;
    c->request_render_fn = fn;
    c->request_render_userdata = user;
}

void yetty_ycompositor_set_default_font(
    struct yetty_ycompositor *c, struct yetty_ydraw_font *font)
{
    if (!c) return;
    c->default_font = font;
}

void yetty_ycompositor_set_viewport_offset(
    struct yetty_ycompositor *c, float offset_x, float offset_y)
{
    if (!c) return;
    float dx = offset_x - c->viewport_offset_x;
    float dy = offset_y - c->viewport_offset_y;
    if (dx == 0.0f && dy == 0.0f)
        return;
    ydebug("ycompositor: viewport_offset %+.1f,%+.1f -> %.1f,%.1f (delta %+.1f,%+.1f)",
           c->viewport_offset_x, c->viewport_offset_y, offset_x, offset_y, dx, dy);
    c->viewport_offset_x = offset_x;
    c->viewport_offset_y = offset_y;
    /* Shift every stored figure rect by the delta. Each top-level
     * figure is a yfigure_group and (for v1) has exactly one ygrid
     * child whose rect overlays the group's — both move together. */
    for (size_t i = 0; i < c->group_count; ++i) {
        struct yetty_yfigure_figure *gf =
            yetty_yfigure_group_as_figure(c->groups[i].group);
        struct yetty_yfigure_figure *yf =
            yetty_ygrid_as_figure(c->groups[i].ygrid);
        damage_add(c, gf->rect);
        gf->rect.min.x += dx;
        gf->rect.min.y += dy;
        gf->rect.max.x += dx;
        gf->rect.max.y += dy;
        yf->rect.min.x += dx;
        yf->rect.min.y += dy;
        yf->rect.max.x += dx;
        yf->rect.max.y += dy;
        damage_add(c, gf->rect);
        gf->dirty = 1;
        yf->dirty = 1;
    }
}

/*===========================================================================
 * Lifecycle
 *=========================================================================*/

struct yetty_ycompositor_ptr_result yetty_ycompositor_create(
    uint32_t cols, uint32_t rows, float cell_w, float cell_h,
    const struct yetty_context *context)
{
    if (!context)
        return YETTY_ERR(yetty_ycompositor_ptr, "ycompositor_create: NULL context");
    struct yetty_ycompositor *c =
        (struct yetty_ycompositor *)calloc(1, sizeof(struct yetty_ycompositor));
    if (!c)
        return YETTY_ERR(yetty_ycompositor_ptr, "ycompositor_create: oom");
    c->cols = cols;
    c->rows = rows;
    c->cell_w = cell_w;
    c->cell_h = cell_h;
    c->context = context;

    /* Build the flyweight registry the iterator needs. Handlers for
     * cmd / SDF / FONT / TEXT_SPAN — all from ydraw-core, the parsing
     * tier that survives the migration. Complex prims (yplot/yimage/
     * ymesh) are not wired up: v1 expects only ygui's SDF + glyph
     * stream. */
    struct yetty_ydraw_flyweight_registry_ptr_result rr =
        yetty_ydraw_flyweight_registry_create();
    if (YETTY_IS_ERR(rr)) {
        free(c);
        return YETTY_ERR(yetty_ycompositor_ptr, "ycompositor: registry create", rr);
    }
    c->registry = rr.value;
    yetty_ydraw_flyweight_registry_set_default(c->registry, yetty_ysdf_handler);
    struct yetty_ycore_void_result hr;
    hr = yetty_ydraw_flyweight_registry_add(
        c->registry, YETTY_YDRAW_CMD_BASE, YETTY_YDRAW_CMD_END,
        yetty_ydraw_cmd_handler);
    if (YETTY_IS_ERR(hr)) goto fail_reg;
    hr = yetty_ydraw_flyweight_registry_add(
        c->registry, YETTY_YDRAW_TYPE_FONT, YETTY_YDRAW_TYPE_FONT,
        yetty_ydraw_font_drawable_handler);
    if (YETTY_IS_ERR(hr)) goto fail_reg;
    hr = yetty_ydraw_flyweight_registry_add(
        c->registry, YETTY_YDRAW_TYPE_TEXT_SPAN, YETTY_YDRAW_TYPE_TEXT_SPAN,
        yetty_ydraw_text_span_drawable_handler);
    if (YETTY_IS_ERR(hr)) goto fail_reg;
    /* HAS_ID range — covers CMD_GROUP / CMD_DELETE / CMD_UPDATE (the
     * iterator special-cases DELETE and UPDATE in its size-determination
     * phase but still does a registry lookup at command-population time
     * for ADD-tagged records including GROUP) AND any complex prim type
     * (yplot / yimage / ymesh) ygui might emit. raw_figure_handler's
     * size op reads `12 + payload_size`, matching the HAS_ID layout —
     * works uniformly for cmds and complex prims. */
    hr = yetty_ydraw_flyweight_registry_add(
        c->registry, YETTY_YDRAW_COMPLEX_TYPE_BASE, 0xFFFFFFFFu,
        yetty_ydraw_raw_figure_handler);
    if (YETTY_IS_ERR(hr)) goto fail_reg;

    return YETTY_OK(yetty_ycompositor_ptr, c);

fail_reg:
    yetty_ydraw_flyweight_registry_destroy(c->registry);
    free(c);
    return YETTY_ERR(yetty_ycompositor_ptr, "ycompositor: registry add handler", hr);
}

struct yetty_ycore_void_result yetty_ycompositor_destroy(struct yetty_ycompositor *c)
{
    if (!c)
        return YETTY_OK_VOID();
    /* Destroy every top-level figure (cascades to children). */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    int have_err = 0;
    for (size_t i = 0; i < c->figure_count; ++i) {
        struct yetty_yfigure_figure *f = c->figures[i];
        struct yetty_ycore_void_result r = f->ops->destroy(f);
        if (YETTY_IS_ERR(r)) {
            if (!have_err) { first_err = r; have_err = 1; }
            else yetty_ycore_error_destroy(r.error);
        }
    }
    free(c->figures);
    free(c->groups);
    if (c->registry)
        yetty_ydraw_flyweight_registry_destroy(c->registry);
    free(c);
    if (have_err)
        return YETTY_ERR(yetty_ycore_void, "ycompositor_destroy: figure destroy failed",
                         first_err);
    return YETTY_OK_VOID();
}
