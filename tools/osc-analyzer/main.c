/*
 * osc-analyzer — figure-tree OSC wire decoder.
 *
 * Modes (one of):
 *
 *   osc-analyzer -e <cmd> [args...] [-o FILE]
 *       fork+exec <cmd> under a PTY, pump the master fd into the wire
 *       state machine. Use this for any app that gates on isatty() —
 *       ygreeter, the ymgui demo, ycat, etc. A shell pipe is NOT a
 *       terminal and those apps will refuse to start.
 *
 *   osc-analyzer --interpose [-o FILE]
 *       Read bytes from stdin (already raw, no PTY), copy them
 *       byte-for-byte to stdout, and feed the same bytes through the
 *       wire state machine. For apps whose caller has already set up a
 *       PTY upstream (e.g. via `script(1)` or yetty itself).
 *
 *   osc-analyzer [FILE | -]
 *       Replay a captured wire dump from FILE (or stdin).
 *
 * Decode pipeline reuses yetty's own wire state machine
 * (yetty_ywire_wire_statemachine): coroutine-driven framer, lz4f
 * auto-sniff, dispatch to a registered layer per OSC code. The analyzer
 * registers itself for every wire code published in
 * <yetty/yterm/osc-codes.h>, <yetty/yterm/client-input.h>, and
 * <yetty/ymgui/wire.h>, so every envelope flows through the same path
 * yetty's terminal uses for real.
 *
 * Decoded output goes to stderr by default, or to -o FILE.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define read _read
#define write _write
#define close _close
#define open _open
#else
#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <yetty/yplatform/io.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-core/drawable-list-registry.h>
#include <yetty/ydraw-core/font-resource.h>
#include <yetty/ydraw-core/text-drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ysdf/handler.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterm/client-input.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/yterm/terminal.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ytrace/ytrace.h>

/*===========================================================================
 * Symbolic decoding tables. Every entry comes from the public headers
 * — no hard-coded numerics here. Add a new OSC code to its module
 * header, then add the case below; do not invent local copies.
 *=========================================================================*/

static const char *osc_code_name(int code)
{
    switch (code) {
    case YETTY_OSC_YDRAW_CLEAR:                    return "YDRAW_CLEAR";
    case YETTY_OSC_YDRAW_BIN:                      return "YDRAW_BIN";
    case YETTY_OSC_YDRAW_YAML:                     return "YDRAW_YAML";
    case YETTY_OSC_YDRAW_OVERLAY:                  return "YDRAW_OVERLAY";
    case YETTY_OSC_YDRAW_SCENE_BIN:                return "YDRAW_SCENE_BIN";
    case YETTY_OSC_YCOMPOSITOR_BIN:                return "YCOMPOSITOR_BIN";
    case YETTY_OSC_CS_CLIENT_INPUT_SUB:            return "CS_CLIENT_INPUT_SUB";
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE:   return "SC_CLIENT_INPUT_FIGURE_MOUSE";
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE:  return "SC_CLIENT_INPUT_FIGURE_RESIZE";
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS:   return "SC_CLIENT_INPUT_FIGURE_FOCUS";
    case YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY:     return "SC_CLIENT_INPUT_FIGURE_KEY";
    case YETTY_OSC_SC_CLIENT_INPUT_MOUSE:          return "SC_CLIENT_INPUT_MOUSE";
    case YETTY_OSC_SC_CLIENT_INPUT_RESIZE:         return "SC_CLIENT_INPUT_RESIZE";
    case YETTY_OSC_SC_CLIENT_INPUT_KEY:            return "SC_CLIENT_INPUT_KEY";
    case YMGUI_OSC_CS_CLEAR:                       return "YMGUI_CS_CLEAR";
    case YMGUI_OSC_CS_FRAME:                       return "YMGUI_CS_FRAME";
    case YMGUI_OSC_CS_TEX:                         return "YMGUI_CS_TEX";
    case YMGUI_OSC_CS_CARD_PLACE:                  return "YMGUI_CS_CARD_PLACE";
    case YMGUI_OSC_CS_CARD_REMOVE:                 return "YMGUI_CS_CARD_REMOVE";
    default:                                       return "UNKNOWN";
    }
}

/* All codes the analyzer subscribes to. The SM silently drops envelopes
 * whose code has no registered layer, so anything missing here is
 * invisible — adding a new wire code anywhere in the project means
 * extending this table. */
static const int kAnalyzedCodes[] = {
    YETTY_OSC_YDRAW_CLEAR,
    YETTY_OSC_YDRAW_BIN,
    YETTY_OSC_YDRAW_YAML,
    YETTY_OSC_YDRAW_OVERLAY,
    YETTY_OSC_YDRAW_SCENE_BIN,
    YETTY_OSC_YCOMPOSITOR_BIN,
    YETTY_OSC_CS_CLIENT_INPUT_SUB,
    YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE,
    YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE,
    YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS,
    YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY,
    YETTY_OSC_SC_CLIENT_INPUT_MOUSE,
    YETTY_OSC_SC_CLIENT_INPUT_RESIZE,
    YETTY_OSC_SC_CLIENT_INPUT_KEY,
    YMGUI_OSC_CS_CLEAR,
    YMGUI_OSC_CS_FRAME,
    YMGUI_OSC_CS_TEX,
    YMGUI_OSC_CS_CARD_PLACE,
    YMGUI_OSC_CS_CARD_REMOVE,
};
enum { kAnalyzedCodes_n = (int)(sizeof(kAnalyzedCodes) / sizeof(kAnalyzedCodes[0])) };

static const char *admin_op_name(uint32_t op)
{
    switch (op) {
    case YETTY_YFIGURE_ADMIN_CLEAR_ALL:       return "CLEAR_ALL";
    case YETTY_YFIGURE_ADMIN_CREATE_CHILD:    return "CREATE_CHILD";
    case YETTY_YFIGURE_ADMIN_DELETE_CHILD:    return "DELETE_CHILD";
    case YETTY_YFIGURE_ADMIN_SET_CHILD_RECT:  return "SET_CHILD_RECT";
    case YETTY_YFIGURE_ADMIN_SET_RECT:        return "SET_RECT";
    default:                                   return "UNKNOWN_ADMIN_OP";
    }
}

static const char *figure_kind_name(uint32_t kind)
{
    switch (kind) {
    case YETTY_YFIGURE_KIND_CONTAINER: return "CONTAINER";
    case YETTY_YFIGURE_KIND_YGRID:     return "YGRID";
    case YETTY_YFIGURE_KIND_YMGUI:     return "YMGUI";
    case YETTY_YFIGURE_KIND_YRDAWN:    return "YRDAWN";
    case YETTY_YFIGURE_KIND_YPLOT:     return "YPLOT";
    case YETTY_YFIGURE_KIND_YIMAGE:    return "YIMAGE";
    case YETTY_YFIGURE_KIND_YVIDEO:    return "YVIDEO";
    case YETTY_YFIGURE_KIND_YZOO:      return "YZOO";
    case YETTY_YFIGURE_KIND_YJUNGLE:   return "YJUNGLE";
    default:                            return "UNKNOWN_KIND";
    }
}

static const char *ymgui_sub_op_name(uint32_t op)
{
    switch (op) {
    case YETTY_YMGUI_FIGURE_SUB_CLEAR:           return "SUB_CLEAR";
    case YETTY_YMGUI_FIGURE_SUB_FRAME:           return "SUB_FRAME";
    case YETTY_YMGUI_FIGURE_SUB_TEX_UPLOAD:      return "SUB_TEX_UPLOAD";
    case YETTY_YMGUI_FIGURE_SUB_TEX_RELEASE:     return "SUB_TEX_RELEASE";
    case YETTY_YMGUI_FIGURE_SUB_TERM_INPUT_SUB:  return "SUB_TERM_INPUT_SUB";
    default:                                      return NULL;
    }
}

static const char *ymgui_tex_fmt_name(uint32_t f)
{
    switch (f) {
    case YMGUI_TEX_FMT_R8:    return "R8";
    case YMGUI_TEX_FMT_RGBA8: return "RGBA8";
    default:                  return "UNKNOWN_TEX_FMT";
    }
}

static const char *sdf_type_name(uint32_t type)
{
    /* Strip HAS_ID flag before comparing (an SDF prim with an id has the
     * top bit set in its type word — same lookup table). */
    uint32_t t = type;
    switch (t) {
    case YETTY_YSDF_CIRCLE:               return "circle";
    case YETTY_YSDF_BOX:                  return "box";
    case YETTY_YSDF_SEGMENT:              return "segment";
    case YETTY_YSDF_TRIANGLE:             return "triangle";
    case YETTY_YSDF_ELLIPSE:              return "ellipse";
    case YETTY_YSDF_ARC:                  return "arc";
    case YETTY_YSDF_ROUNDED_BOX:          return "rounded_box";
    case YETTY_YSDF_RHOMBUS:              return "rhombus";
    case YETTY_YSDF_PENTAGON:             return "pentagon";
    case YETTY_YSDF_HEXAGON:              return "hexagon";
    case YETTY_YSDF_STAR:                 return "star";
    case YETTY_YSDF_PIE:                  return "pie";
    case YETTY_YSDF_RING:                 return "ring";
    case YETTY_YSDF_HEART:                return "heart";
    case YETTY_YSDF_CROSS:                return "cross";
    case YETTY_YSDF_ROUNDED_X:            return "rounded_x";
    case YETTY_YSDF_CAPSULE:              return "capsule";
    case YETTY_YSDF_MOON:                 return "moon";
    case YETTY_YSDF_EGG:                  return "egg";
    case YETTY_YSDF_OCTOGON:              return "octogon";
    case YETTY_YSDF_HEXAGRAM:             return "hexagram";
    case YETTY_YSDF_PENTAGRAM:            return "pentagram";
    case YETTY_YSDF_LINEAR_GRADIENT_BOX:  return "linear_gradient_box";
    case YETTY_YSDF_RADIAL_GRADIENT_BOX:  return "radial_gradient_box";
    case YETTY_YSDF_SPHERE_3D:            return "sphere_3d";
    case YETTY_YSDF_BOX_3D:               return "box_3d";
    case YETTY_YSDF_TORUS_3D:             return "torus_3d";
    case YETTY_YSDF_CYLINDER_3D:          return "cylinder_3d";
    default:                              return NULL;
    }
}

/* For SDF primitives, return an array of geometry-field names paired
 * with the type — used so the dump shows `center_x:` not `geom_0:`.
 * Returns NULL when the type isn't an SDF prim or when no names are
 * registered for it. Field count = word_count - 5 (the 5 leading slots
 * are: type, z_order, fill_color, stroke_color, stroke_width). */
static const char *const *sdf_geom_field_names(uint32_t type, int *out_n)
{
    static const char *circle[]    = {"center_x", "center_y", "radius"};
    static const char *box[]       = {"center_x", "center_y", "half_width", "half_height",
                                      "corner_radius"};
    static const char *segment[]   = {"start_x", "start_y", "end_x", "end_y"};
    static const char *triangle[]  = {"vertex_a_x", "vertex_a_y", "vertex_b_x", "vertex_b_y",
                                      "vertex_c_x", "vertex_c_y"};
    static const char *ellipse[]   = {"center_x", "center_y", "radius_x", "radius_y"};
    static const char *arc[]       = {"center_x", "center_y", "radius",
                                      "thickness", "angle_start", "angle_end"};
    static const char *rbox[]      = {"center_x", "center_y", "half_width", "half_height",
                                      "radius_top_right", "radius_bottom_right",
                                      "radius_top_left", "radius_bottom_left"};
    static const char *capsule[]   = {"start_x", "start_y", "end_x", "end_y", "radius"};
    static const char *gen5xy[]    = {"center_x", "center_y", "size", "rotation", "radius"};
    static const char *star[]      = {"center_x", "center_y", "outer_radius", "inner_radius",
                                      "points"};
    static const char *ring[]      = {"center_x", "center_y", "outer_radius", "inner_radius",
                                      "angle_start", "angle_end"};
    static const char *pie[]       = {"center_x", "center_y", "radius", "angle_start",
                                      "angle_end"};
    switch (type) {
    case YETTY_YSDF_CIRCLE:      *out_n = 3; return circle;
    case YETTY_YSDF_BOX:         *out_n = 5; return box;
    case YETTY_YSDF_SEGMENT:     *out_n = 4; return segment;
    case YETTY_YSDF_TRIANGLE:    *out_n = 6; return triangle;
    case YETTY_YSDF_ELLIPSE:     *out_n = 4; return ellipse;
    case YETTY_YSDF_ARC:         *out_n = 6; return arc;
    case YETTY_YSDF_ROUNDED_BOX: *out_n = 8; return rbox;
    case YETTY_YSDF_CAPSULE:     *out_n = 5; return capsule;
    case YETTY_YSDF_STAR:        *out_n = 5; return star;
    case YETTY_YSDF_RING:        *out_n = 6; return ring;
    case YETTY_YSDF_PIE:         *out_n = 5; return pie;
    case YETTY_YSDF_PENTAGON:
    case YETTY_YSDF_HEXAGON:
    case YETTY_YSDF_RHOMBUS:
    case YETTY_YSDF_HEART:
    case YETTY_YSDF_OCTOGON:
    case YETTY_YSDF_HEXAGRAM:
    case YETTY_YSDF_PENTAGRAM:   *out_n = 3; return gen5xy;
    default:                     *out_n = 0; return NULL;
    }
}

/*===========================================================================
 * Output: a single FILE* + a small indented print helper.
 *=========================================================================*/

static FILE *g_out = NULL;

static void out(const char *fmt, ...)
#ifndef _MSC_VER
    __attribute__((format(printf, 1, 2)))
#endif
    ;
static void out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_out, fmt, ap);
    va_end(ap);
    fflush(g_out);
}

static void ind(int depth)
{
    for (int i = 0; i < depth; i++) fputs("  ", g_out);
}

/*===========================================================================
 * Payload walkers
 *
 * walk_records:        sequence of {length:u32 | id:u32 | payload[length]}
 * walk_admin_payload:  body of an id==0 record, leading u32 is admin_op
 * walk_routed_payload: body of an id!=0 record; format depends on the
 *                      first u32 (YETTY_YMGUI_FIGURE_SUB_* tag, or one
 *                      of the YMGUI_WIRE_MAGIC_* self-describing
 *                      headers, or a nested record stream, or a flat
 *                      prim stream).
 * walk_ygrid_body:     contents of an ygrid figure body (init_payload of
 *                      CREATE_CHILD(YGRID), or the body of a routed
 *                      record with id == figure_id). Records here are
 *                      CMD_GROUP / CMD_DELETE / SDF drawables — NO outer
 *                      {length, id} framing, since this is INSIDE one
 *                      figure's body.
 *=========================================================================*/

static void walk_records(const uint8_t *bytes, size_t bytes_len, int depth);
static void walk_ygrid_body(const uint8_t *bytes, size_t bytes_len, int depth);

/* Glyph wire record laid out by ygrid.c (type id 200, 7 words).
 *   word 0 type, 1 z_order, 2 x, 3 y, 4 font_size,
 *   5 packed (glyph_idx | ((slot+1) << 16)),
 *   6 color */
#define YGRID_GLYPH_TYPE 200u

/* Built once in main(), drives drawable_command_parse so we walk the
 * ygrid body knowing every record's exact size. Without it we'd be
 * guessing (the previous heuristic-step code drifted off after one or
 * two records and printed garbage for the rest of the body). */
static struct yetty_ydraw_drawable_list_registry *g_registry = NULL;

static struct yetty_ydraw_drawable_list_registry *make_full_registry(void)
{
    struct yetty_ydraw_drawable_list_registry_ptr_result rr =
        yetty_ydraw_drawable_list_registry_create();
    if (YETTY_IS_ERR(rr)) {
        fprintf(stderr, "osc-analyzer: registry_create failed: %s\n", rr.error.msg);
        yetty_ycore_error_destroy(rr.error);
        return NULL;
    }
    yetty_ydraw_drawable_list_registry_set_default(rr.value, yetty_ysdf_handler);
    struct yetty_ycore_void_result a;
    a = yetty_ydraw_drawable_list_registry_add(rr.value, YETTY_YDRAW_CMD_BASE,
                                           YETTY_YDRAW_CMD_END, yetty_ydraw_cmd_handler);
    if (YETTY_IS_ERR(a)) goto err;
    a = yetty_ydraw_drawable_list_registry_add(rr.value, YETTY_YDRAW_RESOURCE_FONT,
                                           YETTY_YDRAW_RESOURCE_FONT,
                                           yetty_ydraw_font_resource_handler);
    if (YETTY_IS_ERR(a)) goto err;
    a = yetty_ydraw_drawable_list_registry_add(rr.value, YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST,
                                           YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST,
                                           yetty_ydraw_text_drawable_list_handler);
    if (YETTY_IS_ERR(a)) goto err;
    a = yetty_ydraw_drawable_list_registry_add(rr.value, YETTY_YDRAW_COMPOSITE_TYPE_BASE,
                                           0xFFFFFFFFu, yetty_ydraw_composite_handler);
    if (YETTY_IS_ERR(a)) goto err;
    return rr.value;
err:
    fprintf(stderr, "osc-analyzer: registry_add failed: %s\n", a.error.msg);
    yetty_ycore_error_destroy(a.error);
    yetty_ydraw_drawable_list_registry_destroy(rr.value);
    return NULL;
}

/* Emit a complete YAML description of one record at `bytes[off..off+rec_len)`.
 * Caller already knows the record's parse result; this just formats it. */
static void emit_record_yaml(const uint8_t *bytes, size_t rec_len, int depth, int idx,
                             const struct yetty_ydraw_command *cmd)
{
    if (cmd->kind == YETTY_YDRAW_COMMAND_DELETE) {
        ind(depth); out("- # rec %d\n", idx);
        ind(depth); out("  kind: CMD_DELETE\n");
        ind(depth); out("  id: %u\n", cmd->id);
        return;
    }
    if (cmd->kind == YETTY_YDRAW_COMMAND_UPDATE) {
        ind(depth); out("- # rec %d\n", idx);
        ind(depth); out("  kind: CMD_UPDATE\n");
        ind(depth); out("  id: %u\n", cmd->update.id);
        ind(depth); out("  payload_size: %llu\n", (unsigned long long)cmd->update.size);
        return;
    }
    /* ADD — break down further by type. */
    uint32_t type = cmd->entry.data ? cmd->entry.data[0] : 0u;

    if (type == YETTY_YDRAW_CMD_GROUP) {
        if (rec_len < 12) {
            ind(depth); out("- # rec %d (truncated CMD_GROUP)\n", idx);
            return;
        }
        uint32_t id, payload_size;
        memcpy(&id,           bytes + 4, 4);
        memcpy(&payload_size, bytes + 8, 4);
        ind(depth); out("- # rec %d\n", idx);
        ind(depth); out("  kind: CMD_GROUP\n");
        ind(depth); out("  id: %u\n", id);
        ind(depth); out("  payload_size: %u\n", payload_size);
        if (payload_size > 0 && rec_len >= 12u + payload_size) {
            ind(depth); out("  body:\n");
            walk_ygrid_body(bytes + 12u, payload_size, depth + 2);
        } else {
            ind(depth); out("  body: []\n");
        }
        return;
    }
    if (type == YETTY_YDRAW_CMD_ZERO) {
        ind(depth); out("- # rec %d\n", idx);
        ind(depth); out("  kind: CMD_ZERO\n");
        return;
    }
    if (type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) {
        struct yetty_ydraw_text_drawable_list_view v;
        if (yetty_ydraw_text_drawable_list_parse((const uint32_t *)bytes, &v) == 0) {
            ind(depth); out("- # rec %d\n", idx);
            ind(depth); out("  kind: TEXT_SPAN\n");
            ind(depth); out("  x: %.2f\n", v.x);
            ind(depth); out("  y: %.2f\n", v.y);
            ind(depth); out("  font_size: %.2f\n", v.font_size);
            ind(depth); out("  rotation: %.2f\n", v.rotation);
            ind(depth); out("  color: 0x%08x\n", v.color);
            ind(depth); out("  layer: %u\n", v.layer);
            ind(depth); out("  font_id: %d\n", v.font_id);
            ind(depth); out("  text_len: %u\n", v.text_len);
            /* Print up to 80 chars of the text, quoted, with escapes
             * for control bytes so binary or multiline content doesn't
             * corrupt the YAML stream. */
            ind(depth); out("  text: \"");
            uint32_t shown = v.text_len > 80u ? 80u : v.text_len;
            for (uint32_t i = 0; i < shown; i++) {
                unsigned char c = (unsigned char)v.text[i];
                if (c == '\\' || c == '"')      out("\\%c", c);
                else if (c == '\n')             out("\\n");
                else if (c == '\r')             out("\\r");
                else if (c == '\t')             out("\\t");
                else if (c < 0x20 || c >= 0x7f) out("\\x%02x", c);
                else                            out("%c", c);
            }
            if (v.text_len > shown) out("...");
            out("\"\n");
            return;
        }
        /* fallthrough on parse error */
    }
    if (type == YETTY_YDRAW_RESOURCE_FONT) {
        struct yetty_ydraw_font_resource_view v;
        if (yetty_ydraw_font_resource_parse((const uint32_t *)bytes, &v) == 0) {
            ind(depth); out("- # rec %d\n", idx);
            ind(depth); out("  kind: FONT\n");
            ind(depth); out("  font_id: %d\n", v.font_id);
            ind(depth); out("  name_len: %u\n", v.name_len);
            ind(depth); out("  name: \"");
            for (uint32_t i = 0; i < v.name_len; i++) {
                unsigned char c = (unsigned char)v.name[i];
                if (c == '\\' || c == '"') out("\\%c", c);
                else if (c >= 0x20 && c < 0x7f) out("%c", c);
                else out("\\x%02x", c);
            }
            out("\"\n");
            ind(depth); out("  ttf_len: %u  # <data omitted>\n", v.ttf_len);
            return;
        }
    }
    if (type == YGRID_GLYPH_TYPE && rec_len >= 7u * 4u) {
        const uint32_t *w = (const uint32_t *)bytes;
        float gx, gy, gs;
        memcpy(&gx, &w[2], 4);
        memcpy(&gy, &w[3], 4);
        memcpy(&gs, &w[4], 4);
        uint32_t packed = w[5];
        uint32_t color  = w[6];
        ind(depth); out("- # rec %d\n", idx);
        ind(depth); out("  kind: GLYPH\n");
        ind(depth); out("  z_order: %u\n", w[1]);
        ind(depth); out("  x: %.2f\n", gx);
        ind(depth); out("  y: %.2f\n", gy);
        ind(depth); out("  font_size: %.2f\n", gs);
        ind(depth); out("  glyph_index: %u\n", packed & 0xFFFFu);
        ind(depth); out("  font_slot: %u  # 0 = default-when-zero, else slot-1\n",
                       (packed >> 16) & 0xFFFFu);
        ind(depth); out("  color: 0x%08x\n", color);
        return;
    }
    /* Complex prim (yplot / yimage / yvideo / yzoo / yjungle / etc.) —
     * type >= 0x80000003 and FAM-shaped (word 1 = payload_size). Show
     * type + size only, NEVER the payload bytes (yimage carries
     * megabytes of pixel data; dumping it floods the analyzer log). */
    if (type >= YETTY_YDRAW_COMPOSITE_TYPE_BASE && rec_len >= 8) {
        uint32_t payload_size;
        memcpy(&payload_size, bytes + 4, 4);
        ind(depth); out("- # rec %d\n", idx);
        ind(depth); out("  kind: COMPLEX_PRIM\n");
        ind(depth); out("  type: 0x%08x\n", type);
        ind(depth); out("  payload_size: %u  # <data omitted>\n", payload_size);
        return;
    }
    /* SDF prim — fixed size by type, geometry starting at word 5 (or
     * word 6 when an id slot is present). word_count covers the FULL
     * record (type + 4 style words + geom). */
    {
        uint32_t base_type = type & ~YETTY_YDRAW_HAS_ID_FLAG;
        uint32_t wcount = yetty_ysdf_word_count((enum yetty_ysdf_type)base_type);
        if (wcount == 0) {
            ind(depth); out("- # rec %d\n", idx);
            ind(depth); out("  kind: UNKNOWN\n");
            ind(depth); out("  type: 0x%08x\n", type);
            ind(depth); out("  rec_bytes: %zu\n", rec_len);
            return;
        }
        const uint32_t *w = (const uint32_t *)bytes;
        uint32_t off = 1u;
        const char *name = sdf_type_name(base_type);
        ind(depth); out("- # rec %d\n", idx);
        ind(depth); out("  kind: SDF\n");
        ind(depth); out("  type: %s%s  # 0x%08x%s\n",
                       name ? name : "unknown", "",
                       base_type,
                       (type & YETTY_YDRAW_HAS_ID_FLAG) ? " (HAS_ID)" : "");
        if (type & YETTY_YDRAW_HAS_ID_FLAG) {
            if (off < wcount) { ind(depth); out("  id: %u\n", w[off]); off++; }
        }
        if (off < wcount) { ind(depth); out("  z_order: %u\n", w[off]); off++; }
        if (off < wcount) { ind(depth); out("  fill_color: 0x%08x\n", w[off]); off++; }
        if (off < wcount) { ind(depth); out("  stroke_color: 0x%08x\n", w[off]); off++; }
        if (off < wcount) {
            float sw;
            memcpy(&sw, &w[off], 4);
            ind(depth); out("  stroke_width: %.2f\n", sw);
            off++;
        }
        /* Remaining words are geometry — emit with named fields when
         * we know them, else as a generic float array. */
        int n_geom_names = 0;
        const char *const *names = sdf_geom_field_names(base_type, &n_geom_names);
        uint32_t geom_left = wcount - off;
        if (names && geom_left > 0) {
            ind(depth); out("  geometry:\n");
            for (uint32_t i = 0; i < geom_left && off < wcount; i++, off++) {
                const char *fn = (i < (uint32_t)n_geom_names) ? names[i] : NULL;
                float v;
                memcpy(&v, &w[off], 4);
                ind(depth);
                if (fn) {
                    out("    %s: %.2f\n", fn, v);
                } else {
                    out("    geom_%u: %.2f\n", i, v);
                }
            }
        } else if (geom_left > 0) {
            ind(depth); out("  geometry: [");
            for (uint32_t i = 0; i < geom_left && off < wcount; i++, off++) {
                float v;
                memcpy(&v, &w[off], 4);
                out("%s%.2f", i ? ", " : "", v);
            }
            out("]\n");
        }
    }
}

/* Walk one ygrid-figure body — the bytes between begin_record/end_record
 * for a routed payload, or the init_payload of CREATE_CHILD(YGRID).
 *
 * The bytes are a stream of drawable records, NOT length-prefixed wire
 * records. Uses the flyweight registry to know each record's exact
 * size (no heuristics). CMD_GROUP bodies are walked recursively so the
 * entity tree shows up. Complex prims (yplot/yimage/yvideo) have their
 * payload size printed but not their data — the user's complaint was
 * that yimage bytes flood the log. */
static void walk_ygrid_body(const uint8_t *bytes, size_t bytes_len, int depth)
{
    if (bytes_len == 0) {
        return;
    }
    if (!g_registry) {
        ind(depth);
        out("# (no registry — cannot decode body, %zu bytes)\n", bytes_len);
        return;
    }
    size_t off = 0;
    int idx = 0;
    while (off < bytes_len) {
        struct yetty_ydraw_command cmd;
        struct yetty_ycore_size_result pr = yetty_ydraw_drawable_command_parse(
            g_registry, bytes + off, (uint32_t)(bytes_len - off), &cmd);
        if (YETTY_IS_ERR(pr)) {
            ind(depth);
            out("# parse error at offset %zu: %s\n", off, pr.error.msg);
            yetty_ycore_error_destroy(pr.error);
            return;
        }
        if (pr.value == 0) {
            ind(depth);
            out("# parser made no progress at offset %zu (trailing %zu B)\n",
                off, bytes_len - off);
            return;
        }
        emit_record_yaml(bytes + off, pr.value, depth, idx, &cmd);
        off += pr.value;
        idx++;
    }
}

static void walk_admin_payload(const uint8_t *body, size_t blen, int depth)
{
    if (blen < 4) {
        ind(depth);
        out("(admin payload too short for op)\n");
        return;
    }
    uint32_t op;
    memcpy(&op, body, 4);
    ind(depth);
    out("admin op=%s(%u)\n", admin_op_name(op), op);
    const uint8_t *tail = body + 4;
    size_t tlen = blen - 4;

    switch (op) {
    case YETTY_YFIGURE_ADMIN_CLEAR_ALL:
        break;
    case YETTY_YFIGURE_ADMIN_CREATE_CHILD: {
        if (tlen < 4u + 4u + 16u + 4u) {
            ind(depth + 1);
            out("(CREATE_CHILD header truncated, %zu B)\n", tlen);
            break;
        }
        uint32_t child_id, kind, init_n;
        float r[4];
        memcpy(&child_id, tail + 0, 4);
        memcpy(&kind,     tail + 4, 4);
        memcpy(r,         tail + 8, 16);
        memcpy(&init_n,   tail + 24, 4);
        ind(depth + 1);
        out("child_id=%u kind=%s(%u) rect=(%.1f,%.1f)..(%.1f,%.1f) "
            "init_payload=%u B\n",
            child_id, figure_kind_name(kind), kind,
            r[0], r[1], r[2], r[3], init_n);
        if (init_n > 0 && (size_t)init_n + 28u <= tlen) {
            /* Init payload is the body of the figure being minted. For
             * YGRID, that's a CMD_GROUP/CMD_DELETE/SDF stream. */
            ind(depth + 1);
            out("init payload (figure body):\n");
            if (kind == YETTY_YFIGURE_KIND_YGRID) {
                walk_ygrid_body(tail + 28, init_n, depth + 2);
            } else {
                walk_records(tail + 28, init_n, depth + 2);
            }
        }
        break;
    }
    case YETTY_YFIGURE_ADMIN_DELETE_CHILD: {
        if (tlen < 4) {
            ind(depth + 1);
            out("(DELETE_CHILD truncated)\n");
            break;
        }
        uint32_t child_id;
        memcpy(&child_id, tail, 4);
        ind(depth + 1);
        out("child_id=%u\n", child_id);
        break;
    }
    case YETTY_YFIGURE_ADMIN_SET_CHILD_RECT: {
        if (tlen < 4 + 16) {
            ind(depth + 1);
            out("(SET_CHILD_RECT truncated)\n");
            break;
        }
        uint32_t child_id;
        float r[4];
        memcpy(&child_id, tail + 0, 4);
        memcpy(r,         tail + 4, 16);
        ind(depth + 1);
        out("child_id=%u rect=(%.1f,%.1f)..(%.1f,%.1f)\n",
            child_id, r[0], r[1], r[2], r[3]);
        break;
    }
    case YETTY_YFIGURE_ADMIN_SET_RECT: {
        if (tlen < 16) {
            ind(depth + 1);
            out("(SET_RECT truncated)\n");
            break;
        }
        float r[4];
        memcpy(r, tail, 16);
        ind(depth + 1);
        out("rect=(%.1f,%.1f)..(%.1f,%.1f)\n",
            r[0], r[1], r[2], r[3]);
        break;
    }
    default:
        ind(depth + 1);
        out("(unrecognised admin op — body=%zu B)\n", tlen);
        break;
    }
}

static void walk_ymgui_frame(const uint8_t *body, size_t blen, int depth)
{
    if (blen < sizeof(struct yetty_ymgui_wire_frame)) {
        ind(depth);
        out("(ymgui FRAME truncated, %zu B)\n", blen);
        return;
    }
    const struct yetty_ymgui_wire_frame *fh =
        (const struct yetty_ymgui_wire_frame *)body;
    ind(depth);
    out("ymgui_wire_frame magic=0x%08x version=%u total=%u figure_id=%u "
        "cmd_lists=%u display=(%.1f x %.1f) flags=0x%x%s\n",
        fh->magic, fh->version, fh->total_size, fh->figure_id,
        fh->cmd_list_count, fh->display_size_x, fh->display_size_y, fh->flags,
        (fh->flags & YMGUI_FRAME_FLAG_IDX32) ? " IDX32" : "");
}

static void walk_ymgui_tex(const uint8_t *body, size_t blen, int depth)
{
    if (blen < sizeof(struct yetty_ymgui_wire_tex)) {
        ind(depth);
        out("(ymgui TEX truncated, %zu B)\n", blen);
        return;
    }
    const struct yetty_ymgui_wire_tex *th =
        (const struct yetty_ymgui_wire_tex *)body;
    ind(depth);
    out("ymgui_wire_tex magic=0x%08x version=%u total=%u figure_id=%u "
        "tex_id=%u format=%s(%u) %ux%u\n",
        th->magic, th->version, th->total_size, th->figure_id, th->tex_id,
        ymgui_tex_fmt_name(th->format), th->format, th->width, th->height);
}

static void walk_routed_payload(uint32_t id, const uint8_t *payload, size_t plen,
                                int depth)
{
    if (plen == 0) {
        ind(depth);
        out("(empty routed payload for id=%u)\n", id);
        return;
    }
    uint32_t first = 0;
    memcpy(&first, payload, plen >= 4 ? 4 : plen);

    /* Tagged sub-record (current ymgui figure-tree path). */
    const char *sub = ymgui_sub_op_name(first);
    if (sub) {
        ind(depth);
        out("ymgui_figure sub_op=%s(%u) body=%zu B\n", sub, first, plen - 4);
        if (first == YETTY_YMGUI_FIGURE_SUB_FRAME) {
            walk_ymgui_frame(payload + 4, plen - 4, depth + 1);
        } else if (first == YETTY_YMGUI_FIGURE_SUB_TEX_UPLOAD) {
            walk_ymgui_tex(payload + 4, plen - 4, depth + 1);
        }
        return;
    }

    /* Self-describing form: ymgui_wire_frame / ymgui_wire_tex carry
     * their magic in the first u32; <yetty/ymgui/wire.h> documents both
     * dispatch paths as accepted. */
    if (first == YMGUI_WIRE_MAGIC_FRAME) {
        walk_ymgui_frame(payload, plen, depth);
        return;
    }
    if (first == YMGUI_WIRE_MAGIC_TEX) {
        walk_ymgui_tex(payload, plen, depth);
        return;
    }

    /* ygrid figure body: routed record where the first u32 is a
     * drawable command — CMD_GROUP (0x80000002) or CMD_DELETE
     * (0x80000001). Decode as an ygrid body. */
    if (first == YETTY_YDRAW_CMD_GROUP || first == YETTY_YDRAW_CMD_DELETE) {
        ind(depth);
        out("(ygrid figure body for id=%u)\n", id);
        walk_ygrid_body(payload, plen, depth + 1);
        return;
    }

    /* Nested container record stream — the first u32 is then a record
     * `length` that fits inside plen-8. */
    if (plen >= 8 && first <= plen - 8) {
        ind(depth);
        out("(nested record stream)\n");
        walk_records(payload, plen, depth + 1);
        return;
    }

    /* Otherwise we don't know — dump the head so the operator can
     * eyeball it. */
    ind(depth);
    out("(unrecognised routed payload, first u32=0x%08x, %zu B; first 16:",
        first, plen);
    for (size_t i = 0; i < plen && i < 16; i++)
        out(" %02x", payload[i]);
    out(")\n");
}

static void walk_records(const uint8_t *bytes, size_t bytes_len, int depth)
{
    size_t off = 0;
    int idx = 0;
    while (off < bytes_len) {
        if (bytes_len - off < sizeof(struct yetty_yfigure_wire_record)) {
            ind(depth);
            out("record #%d header TRUNCATED (only %zu bytes left)\n",
                idx, bytes_len - off);
            return;
        }
        struct yetty_yfigure_wire_record hdr;
        memcpy(&hdr, bytes + off, sizeof(hdr));
        off += sizeof(hdr);
        if (hdr.length > bytes_len - off) {
            ind(depth);
            out("record #%d length=%u id=%u OVERRUNS buffer (only %zu B left)\n",
                idx, hdr.length, hdr.id, bytes_len - off);
            return;
        }

        ind(depth);
        if (hdr.id == 0) {
            out("record #%d ADMIN length=%u\n", idx, hdr.length);
            walk_admin_payload(bytes + off, hdr.length, depth + 1);
        } else {
            out("record #%d ROUTED id=%u length=%u\n", idx, hdr.id, hdr.length);
            walk_routed_payload(hdr.id, bytes + off, hdr.length, depth + 1);
        }
        off += hdr.length;
        idx++;
    }
}

/*===========================================================================
 * Mock layers.
 *
 * One mock layer is allocated per OSC code we care about, plus a
 * "raw_sink" layer registered as the SM's default. Each mock has its
 * own persistent layer coroutine inside the SM, so the SM's per-code
 * dispatch / read / yield path is exercised independently for every
 * code. A shared layer registered for all codes would funnel them all
 * through one coroutine — useful for the easy case but it hides
 * multi-layer state-machine bugs.
 *
 *   raw_sink_layer        — default sink, drains everything outside an
 *                           envelope so the SM's out_carry never blocks
 *                           the scanner.
 *   envelope_mock_layer   — one instance per OSC code; reads decoded
 *                           body bytes, accumulates, walks records on
 *                           at_end, then loops.
 *
 * No splitter, no own envelope framer — the SM owns those.
 *=========================================================================*/

struct raw_sink_layer {
    uint64_t bytes_drained;
};

static struct yetty_ycore_void_result raw_sink_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct raw_sink_layer *r = userdata;
    uint8_t junk[1024];
    for (;;) {
        for (;;) {
            struct yetty_ycore_size_result rr =
                yetty_ywire_wire_statemachine_read(sm, junk, sizeof(junk));
            if (YETTY_IS_ERR(rr)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "raw_sink: SM read failed", rr);
            }
            if (rr.value == 0) break;
            r->bytes_drained += (uint64_t)rr.value;
        }
        yetty_yplatform_coro_yield();
    }
}

struct envelope_mock_layer {
    int expected_code;            /* the one OSC code we're registered for */
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    int      env_count;
    int      err_count;
};

static int mock_grow(struct envelope_mock_layer *m, size_t need)
{
    if (need <= m->cap) return 0;
    size_t cap = m->cap ? m->cap : 4096;
    while (cap < need) cap *= 2;
    uint8_t *g = realloc(m->buf, cap);
    if (!g) return -1;
    m->buf = g;
    m->cap = cap;
    return 0;
}

/* Per-envelope summary plus code-specific decode. The mock layer's
 * coroutine has the invariant "one envelope per outer iteration":
 *   - read decoded bytes until at_end goes high
 *   - log the result + dispatch on the layer's pre-bound code
 *   - yield, returning to SM which advances to our layer's NEXT
 *     envelope (or to a different layer's coro for a different code)
 *
 * If the SM ever resumes us with a different code(), that's a SM
 * dispatch bug; we surface it as an error so the analyzer doubles as a
 * dispatch correctness check. */
static struct yetty_ycore_void_result envelope_mock_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm)
{
    struct envelope_mock_layer *m = userdata;

    for (;;) {
        int code = yetty_ywire_wire_statemachine_code(sm);
        if (code != m->expected_code) {
            m->err_count++;
            return YETTY_ERR(yetty_ycore_void,
                "envelope_mock: SM dispatched layer registered for one code "
                "to a different code");
        }

        /* Banner on entry to a fresh envelope. The layer's own coro is
         * re-entered at envelope start (because at_end forced us to
         * yield at the previous envelope's end), so emitting the
         * banner here once per outer iteration is right. */
        m->env_count++;
        m->len = 0;
        struct yetty_ywire_wire_statemachine_args args =
            yetty_ywire_wire_statemachine_args(sm);
        out("\nenvelope osc=%d (%s) args=%zu B",
            code, osc_code_name(code), args.len);
        if (args.len >= sizeof(struct yetty_yface_bin_meta)) {
            const struct yetty_yface_bin_meta *meta =
                (const struct yetty_yface_bin_meta *)args.bytes;
            if (meta->magic == YETTY_YFACE_BIN_MAGIC) {
                out(" meta{compressed=%u raw_size=%llu}",
                    meta->compressed, (unsigned long long)meta->raw_size);
            }
        }
        out("\n");

        /* Read the body until SM signals EOE.
         *
         * In OSC-body mode, wire_statemachine_read returns 0 iff
         * (terminator_seen && out_carry_empty) — i.e. all decoded
         * bytes have been delivered AND the framer has consumed the
         * envelope terminator. Looping on at_end() instead is wrong:
         * the terminator flag flips as soon as the framer crosses
         * ESC\\, while decoded body bytes may still be sitting in
         * out_carry — exiting early truncates the body. The SM yields
         * internally when more wire bytes are needed, so we don't
         * need our own retry. */
        for (;;) {
            uint8_t chunk[4096];
            struct yetty_ycore_size_result rr =
                yetty_ywire_wire_statemachine_read(sm, chunk, sizeof(chunk));
            if (YETTY_IS_ERR(rr)) {
                m->err_count++;
                return YETTY_ERR(yetty_ycore_void,
                                 "envelope_mock: SM read failed", rr);
            }
            if (rr.value == 0) break;
            if (mock_grow(m, m->len + rr.value) < 0) {
                m->err_count++;
                return YETTY_ERR(yetty_ycore_void, "envelope_mock: buf oom");
            }
            memcpy(m->buf + m->len, chunk, rr.value);
            m->len += rr.value;
        }

        out("  body decoded: %zu B; first 16:", m->len);
        for (size_t i = 0; i < m->len && i < 16; i++)
            out(" %02x", m->buf[i]);
        out("\n");

        switch (code) {
        case YETTY_OSC_YCOMPOSITOR_BIN:
            walk_records(m->buf, m->len, 1);
            break;
        case YETTY_OSC_CS_CLIENT_INPUT_SUB:
            if (m->len >= sizeof(struct yetty_client_input_sub)) {
                const struct yetty_client_input_sub *s =
                    (const struct yetty_client_input_sub *)m->buf;
                out("  client_input_sub magic=0x%08x flags=0x%x%s%s%s%s\n",
                    s->magic, s->flags,
                    (s->flags & YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK) ? " MOUSE_CLICK" : "",
                    (s->flags & YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE)  ? " MOUSE_MOVE"  : "",
                    (s->flags & YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL) ? " MOUSE_WHEEL" : "",
                    (s->flags & YETTY_CLIENT_INPUT_SUB_KEY)         ? " KEY"         : "");
            }
            break;
        default:
            /* Other codes: the banner + body-head dump is enough.
             * Producers can extend this switch as new structured
             * payloads land in the public headers. */
            break;
        }

        /* Envelope done — yield. The SM will move on (terminator
         * already consumed by the framer) and re-dispatch us when the
         * next envelope for our code arrives. */
        yetty_yplatform_coro_yield();
    }
}

/*===========================================================================
 * Input pumps — three forms (forkpty / stdin / file), all of them push
 * bytes into the SM via yetty_ywire_wire_statemachine_feed.
 *=========================================================================*/

#ifdef _WIN32
/* memmem is a GNU/BSD extension absent from MSVC's CRT. A proper prototype
 * (returns void*) also avoids the implicit-int → pointer cast warning. */
static void *memmem(const void *hay, size_t hl, const void *need, size_t nl)
{
    if (nl == 0) {
        return (void *)hay;
    }
    if (hl < nl) {
        return NULL;
    }
    const unsigned char *h = (const unsigned char *)hay;
    for (size_t i = 0; i + nl <= hl; i++) {
        if (h[i] == *(const unsigned char *)need && memcmp(h + i, need, nl) == 0) {
            return (void *)(h + i);
        }
    }
    return NULL;
}
#endif

static int pump_fd(struct yetty_ywire_wire_statemachine *sm, int in_fd,
                   int out_fd, int forward_to_stdout)
{
    char chunk[1u << 13];
    for (;;) {
        /* Block until readable, then drain with non-blocking reads. Both
         * steps go through the yplatform io shim so this is portable:
         * POSIX uses poll(2)+read(2); Windows uses PeekNamedPipe-gated
         * ReadFile. That also removes the O_NONBLOCK fcntl dance the
         * callers used to need. */
        int pr = yetty_yplatform_io_wait_readable(in_fd, -1);
        if (pr < 0) {
            fprintf(stderr, "osc-analyzer: wait_readable failed on fd %d\n", in_fd);
            return 1;
        }
        int got_bytes = 0;
        for (;;) {
            struct yetty_yplatform_io_size_result rr =
                yetty_yplatform_io_read_nonblocking(in_fd, chunk, sizeof(chunk));
            if (YETTY_IS_ERR(rr)) {
                fprintf(stderr, "osc-analyzer: read failed: %s\n", rr.error.msg);
                yetty_ycore_error_destroy(rr.error);
                return 1;
            }
            size_t n = rr.value;
            if (n == 0) break;
            got_bytes = 1;
            /* Byte-for-byte passthrough first, then feed the SM and drive
             * its coroutine — a downstream `tee` consumer sees the same
             * bytes in the same order, before any decode work. */
            if (forward_to_stdout) {
                size_t w = 0;
                while (w < n) {
                    int k = (int)write(out_fd, chunk + w, (unsigned int)(n - w));
                    if (k < 0) {
                        fprintf(stderr, "osc-analyzer: passthrough write failed\n");
                        return 1;
                    }
                    w += (size_t)k;
                }
            }
            struct yetty_ycore_void_result fr =
                yetty_ywire_wire_statemachine_feed(sm, chunk, n);
            if (YETTY_IS_ERR(fr)) {
                fprintf(stderr, "osc-analyzer: SM feed failed: %s\n", fr.error.msg);
                yetty_ycore_error_destroy(fr.error);
                return 1;
            }
            struct yetty_ycore_void_result pr2 =
                yetty_ywire_wire_statemachine_process(sm);
            if (YETTY_IS_ERR(pr2)) {
                fprintf(stderr, "osc-analyzer: SM process failed: %s\n", pr2.error.msg);
                yetty_ycore_error_destroy(pr2.error);
                return 1;
            }
        }
        /* wait_readable reported ready but the drain read nothing — the fd
         * is at EOF / hung up. (A blocking wait never returns "ready" with
         * no bytes otherwise.) */
        if (!got_bytes) return 0;
    }
}

#ifndef _WIN32
/* Spawn argv under a freshly forked PTY and pump the master through
 * the SM. Returns the child's exit code (or 1 on internal error).
 * forkpty/execvp/waitpid are POSIX — the -e mode is Linux/macOS only. */
static int run_exec(char **argv, int cols, int rows,
                    struct yetty_ywire_wire_statemachine *sm)
{
    struct winsize ws = {0};
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    /* Match yetty's terminal_create: populate ws_xpixel/ws_ypixel so a
     * child app that asks TIOCGWINSZ for the pane pixel area sees a
     * sensible answer (cells × 8/16-px defaults if we have nothing
     * better — the analyzer doesn't know the producer's cell size). */
    ws.ws_xpixel = (unsigned short)(cols * 8);
    ws.ws_ypixel = (unsigned short)(rows * 16);

    int pty_master = -1;
    pid_t pid = forkpty(&pty_master, NULL, NULL, &ws);
    if (pid < 0) {
        fprintf(stderr, "forkpty failed: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0) {
        for (int fd = 3; fd < 1024; fd++) close(fd);
        execvp(argv[0], argv);
        fprintf(stderr, "execvp(%s) failed: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    fprintf(stderr, "osc-analyzer: spawned pid=%d cmd='%s' winsize=%ux%u\n",
            pid, argv[0], ws.ws_col, ws.ws_row);

    int flags = fcntl(pty_master, F_GETFL, 0);
    if (flags >= 0) fcntl(pty_master, F_SETFL, flags | O_NONBLOCK);

    int rc = pump_fd(sm, pty_master, STDOUT_FILENO, /*forward_to_stdout=*/1);

    close(pty_master);
    int status = 0;
    if (waitpid(pid, &status, 0) >= 0) {
        if (WIFEXITED(status)) {
            fprintf(stderr, "osc-analyzer: child exited rc=%d\n",
                    WEXITSTATUS(status));
            if (rc == 0) rc = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "osc-analyzer: child killed by signal %d\n",
                    WTERMSIG(status));
            if (rc == 0) rc = 128 + WTERMSIG(status);
        }
    }
    return rc;
}
#endif /* !_WIN32 */

static int run_interpose(struct yetty_ywire_wire_statemachine *sm)
{
    return pump_fd(sm, STDIN_FILENO, STDOUT_FILENO, /*forward_to_stdout=*/1);
}

static int run_file(const char *path, struct yetty_ywire_wire_statemachine *sm)
{
    int fd = (path && strcmp(path, "-") != 0) ? open(path, O_RDONLY)
                                              : STDIN_FILENO;
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return 1;
    }
    int rc = pump_fd(sm, fd, -1, /*forward_to_stdout=*/0);
    if (fd != STDIN_FILENO) close(fd);
    return rc;
}

/* --records mode: read a YFIGURE_DUMP_RECORDS file (a stream of
 * receiver-side records written by yfigure_container_consume_envelope
 * with the `===FRAME===\n` separator) and decode each frame as if it
 * were one envelope body. This bypasses the OSC framing entirely —
 * useful when the user captured wire bytes via YFIGURE_DUMP_RECORDS
 * (no PTY interpose, no live process needed) and wants to inspect
 * exactly what records the receiver saw. */
static int run_records_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "seek %s: %s\n", path, strerror(errno));
        fclose(f);
        return 1;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fprintf(stderr, "ftell %s: %s\n", path, strerror(errno));
        fclose(f);
        return 1;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) {
        fprintf(stderr, "oom reading %s\n", path);
        fclose(f);
        return 1;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        fprintf(stderr, "short read %s: %zu/%ld\n", path, got, sz);
        free(buf);
        return 1;
    }
    const char *sep = "===FRAME===\n";
    size_t sep_len = strlen(sep);
    /* Each occurrence of the separator starts a new frame. The first
     * separator marks the start of frame 1; bytes before it (if any)
     * are skipped. */
    int frame_no = 0;
    size_t p = 0;
    while (p < got) {
        /* Find next separator at-or-after p. */
        const uint8_t *hit =
            (const uint8_t *)memmem(buf + p, got - p, sep, sep_len);
        if (!hit) {
            /* Trailing bytes after the last separator — decode as the
             * last frame (no following separator means EOF). */
            if (frame_no > 0) {
                size_t frame_len = got - p;
                out("\n=== FRAME %d (%zu B) ===\n", frame_no, frame_len);
                walk_records(buf + p, frame_len, 1);
            }
            break;
        }
        if (frame_no > 0) {
            size_t frame_len = (size_t)(hit - (buf + p));
            out("\n=== FRAME %d (%zu B) ===\n", frame_no, frame_len);
            walk_records(buf + p, frame_len, 1);
        }
        p = (size_t)(hit - buf) + sep_len;
        frame_no++;
    }
    free(buf);
    return 0;
}

/*===========================================================================
 * Wiring
 *=========================================================================*/

static void print_help(const char *prog)
{
    fprintf(stderr,
            "usage: %s -e <cmd> [args...] [-o FILE] [--cols N] [--rows N]\n"
            "       %s --interpose [-o FILE]\n"
            "       %s [FILE | -] [-o FILE]\n"
            "       %s --records FILE [-o FILE]\n"
            "\n"
            "Modes:\n"
            "  -e <cmd> [args...]   fork+exec <cmd> under a PTY (default cols=80\n"
            "                       rows=24); use this for any app that gates on\n"
            "                       isatty() — a shell pipe is not a terminal.\n"
            "  --interpose          read bytes from stdin (already a PTY upstream),\n"
            "                       copy them to stdout, decode in parallel.\n"
            "  FILE | -             replay a captured byte stream from FILE/stdin.\n"
            "  --records FILE       decode a YFIGURE_DUMP_RECORDS dump (the file\n"
            "                       a process emits when run with the env var\n"
            "                       YFIGURE_DUMP_RECORDS=path/to/file.bin set).\n"
            "                       Each `===FRAME===\\n`-separated chunk is\n"
            "                       walked as one envelope body — full YAML.\n"
            "\n"
            "Options:\n"
            "  -o FILE              write decoded log to FILE (default: stderr)\n"
            "  --cols N / --rows N  PTY size for -e (cells)\n"
            "\n"
            "Decoding walks {length, id} figure-tree records inside\n"
            "YETTY_OSC_YCOMPOSITOR_BIN envelopes (and prints headers for every\n"
            "other OSC code listed in the public wire headers). Inside each\n"
            "record the body is walked via the flyweight registry — every SDF\n"
            "prim, TEXT_SPAN, FONT, GLYPH, and CMD_GROUP/CMD_DELETE record is\n"
            "emitted as YAML with named fields. Complex prims (yplot/yimage/\n"
            "yvideo etc.) report their type + payload size but omit the data\n"
            "bytes themselves.\n",
            prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    g_out = stderr;
    const char *out_path = NULL;
    int interpose = 0;
    int exec_at = -1;
    int cols = 80, rows = 24;
    const char *file_path = NULL;
    const char *records_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_help(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "-e")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: -e needs a command\n", argv[0]);
                return 1;
            }
            exec_at = i + 1;
            break; /* everything after -e is child argv */
        }
        if (!strcmp(argv[i], "--interpose") || !strcmp(argv[i], "-t")) {
            interpose = 1;
            continue;
        }
        if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: -o needs FILE\n", argv[0]);
                return 1;
            }
            out_path = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--cols")) {
            if (i + 1 >= argc) return 1;
            cols = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--rows")) {
            if (i + 1 >= argc) return 1;
            rows = atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "--records")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --records needs FILE\n", argv[0]);
                return 1;
            }
            records_path = argv[++i];
            continue;
        }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "unknown flag: %s\n", argv[i]);
            print_help(argv[0]);
            return 1;
        }
        if (file_path) {
            fprintf(stderr, "too many positional arguments\n");
            return 1;
        }
        file_path = argv[i];
    }

    int mode_count = (exec_at >= 0) + interpose + (file_path != NULL) +
                     (records_path != NULL);
    if (mode_count == 0) {
        print_help(argv[0]);
        return 1;
    }
    if (mode_count > 1) {
        fprintf(stderr, "%s: -e, --interpose, FILE and --records are mutually exclusive\n",
                argv[0]);
        return 1;
    }

    if (out_path) {
        FILE *f = fopen(out_path, "w");
        if (!f) {
            fprintf(stderr, "open %s: %s\n", out_path, strerror(errno));
            return 1;
        }
        setvbuf(f, NULL, _IOLBF, 0);
        g_out = f;
    }

    /* Flyweight registry — drives drawable_command_parse so ygrid body
     * walks know each record's exact size. Built once, lives until
     * teardown below. NULL is tolerated (walk_ygrid_body prints a
     * stub) but normal runs always have it. */
    g_registry = make_full_registry();

    /* --records mode: parse a pre-captured YFIGURE_DUMP_RECORDS file
     * directly — no PTY, no OSC framing, just walk each frame's record
     * stream and emit YAML. Bypasses the wire SM entirely. */
    if (records_path) {
        int rrc = run_records_file(records_path);
        if (g_registry) {
            yetty_ydraw_drawable_list_registry_destroy(g_registry);
            g_registry = NULL;
        }
        if (g_out != stderr) fclose(g_out);
        return rrc;
    }

    /* Build the SM with NULL PTY — bytes arrive via the async-feed path
     * (yetty_ywire_wire_statemachine_feed) from pump_fd. The SM keeps
     * its sm_coro alive across feed/process pairs, so framer + decode
     * + dispatch state survive byte-boundary returns. */
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(NULL);
    if (YETTY_IS_ERR(sm_res)) {
        fprintf(stderr, "osc-analyzer: SM create failed: %s\n",
                sm_res.error.msg);
        yetty_ycore_error_destroy(sm_res.error);
        if (g_out != stderr) fclose(g_out);
        return 1;
    }
    struct yetty_ywire_wire_statemachine *sm = sm_res.value;

    /* Default sink for bytes outside any OSC envelope (DEC mode codes,
     * terminal text, child stderr). Without one set, the SM's
     * out_carry — where unrecognised "ESC <x>" pairs are queued —
     * never drains and the scanner spins. */
    struct raw_sink_layer raw_sink = {0};
    {
        struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_set_default(
            sm, raw_sink_process_input, &raw_sink);
        if (YETTY_IS_ERR(r)) {
            fprintf(stderr, "osc-analyzer: SM set_default failed: %s\n",
                    r.error.msg);
            yetty_ycore_error_destroy(r.error);
            yetty_ywire_wire_statemachine_destroy(sm);
            if (g_out != stderr) fclose(g_out);
            return 1;
        }
    }

    /* One envelope_mock_layer instance per OSC code, registered
     * individually. Each gets its own persistent layer coroutine inside
     * the SM, so this also exercises the SM's per-layer dispatch /
     * read / yield path. A shared layer would funnel everything
     * through one coro and hide multi-layer bugs. */
    struct envelope_mock_layer *mocks =
        calloc((size_t)kAnalyzedCodes_n, sizeof(*mocks));
    if (!mocks) {
        fprintf(stderr, "osc-analyzer: oom allocating mocks\n");
        yetty_ywire_wire_statemachine_destroy(sm);
        if (g_out != stderr) fclose(g_out);
        return 1;
    }
    for (int i = 0; i < kAnalyzedCodes_n; i++) {
        mocks[i].expected_code = kAnalyzedCodes[i];
        struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_register(
            sm, YETTY_YWIRE_ENVELOPE_OSC, kAnalyzedCodes[i], /*has_args=*/1,
            envelope_mock_process_input, &mocks[i]);
        if (YETTY_IS_ERR(r)) {
            fprintf(stderr,
                    "osc-analyzer: SM register code=%d failed: %s\n",
                    kAnalyzedCodes[i], r.error.msg);
            yetty_ycore_error_destroy(r.error);
            for (int j = 0; j < kAnalyzedCodes_n; j++) free(mocks[j].buf);
            free(mocks);
            yetty_ywire_wire_statemachine_destroy(sm);
            if (g_out != stderr) fclose(g_out);
            return 1;
        }
    }

    int rc;
    if (exec_at >= 0) {
#ifdef _WIN32
        (void)cols;
        (void)rows;
        fprintf(stderr, "osc-analyzer: -e (forkpty spawn) is not supported on "
                        "Windows; use --interpose, FILE, or --records\n");
        rc = 2;
#else
        rc = run_exec(&argv[exec_at], cols, rows, sm);
#endif
    } else if (interpose) {
        rc = run_interpose(sm);
    } else {
        rc = run_file(file_path, sm);
    }

    int total_env = 0, total_err = 0;
    out("\nper-layer summary:\n");
    out("  default sink: %llu raw byte(s) drained\n",
        (unsigned long long)raw_sink.bytes_drained);
    for (int i = 0; i < kAnalyzedCodes_n; i++) {
        if (mocks[i].env_count > 0 || mocks[i].err_count > 0) {
            out("  code %d (%s): %d envelope(s), %d error(s)\n",
                mocks[i].expected_code, osc_code_name(mocks[i].expected_code),
                mocks[i].env_count, mocks[i].err_count);
        }
        total_env += mocks[i].env_count;
        total_err += mocks[i].err_count;
    }
    out("run closed: %d envelope(s), %d error(s)\n", total_env, total_err);

    yetty_ywire_wire_statemachine_destroy(sm);
    for (int i = 0; i < kAnalyzedCodes_n; i++) free(mocks[i].buf);
    free(mocks);
    if (g_registry) {
        yetty_ydraw_drawable_list_registry_destroy(g_registry);
        g_registry = NULL;
    }
    if (g_out != stderr) fclose(g_out);
    return rc;
}
