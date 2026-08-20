/*
 * ywire - yetty-native consumer for the Chromium paint-bridge wire format.
 *
 * Reads the wire stream produced by the Chromium content_shell paint hook
 * (yetty/paint_bridge/paint_wire_format.h) from a file or stdin, builds a
 * ydraw drawable-list (the same primitive scene ybrowser and ycat produce),
 * and emits it as an OSC 666674 envelope. A running yetty terminal renders it
 * natively via ydraw - no SVG, no image, no rasterizer.
 *
 * Pipe form:  content_shell ... --wire-stdout | ywire
 * File form:  ywire /tmp/page.ypw
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycat/ycat.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

/* Wire opcodes - mirror of yetty/paint_bridge/paint_wire_format.h. */
enum {
    OP_FRAME_BEGIN = 0x01,
    OP_FRAME_END = 0x02,
    OP_IMAGE_RESOURCE = 0x03,
    OP_FONT_RESOURCE = 0x04,
    OP_SAVE = 0x10,
    OP_RESTORE = 0x11,
    OP_SAVE_LAYER = 0x12,
    OP_SAVE_LAYER_ALPHA = 0x13,
    OP_TRANSLATE = 0x20,
    OP_SCALE = 0x21,
    OP_ROTATE = 0x22,
    OP_CONCAT = 0x23,
    OP_SET_MATRIX = 0x24,
    OP_CLIP_RECT = 0x30,
    OP_CLIP_RRECT = 0x31,
    OP_CLIP_PATH = 0x32,
    OP_DRAW_COLOR = 0x40,
    OP_DRAW_RECT = 0x41,
    OP_DRAW_IRECT = 0x42,
    OP_DRAW_OVAL = 0x43,
    OP_DRAW_RRECT = 0x44,
    OP_DRAW_DRRECT = 0x45,
    OP_DRAW_LINE = 0x46,
    OP_DRAW_ARC = 0x47,
    OP_DRAW_PATH = 0x48,
    OP_DRAW_IMAGE = 0x49,
    OP_DRAW_IMAGE_RECT = 0x4A,
    OP_DRAW_TEXT_BLOB = 0x4B,
    OP_DRAW_RECORD = 0x4C,
    OP_UNSUPPORTED = 0x7F,
};

struct cursor {
    const uint8_t *data;
    size_t len;
    size_t off;
};

static uint8_t rd_u8(struct cursor *cursor)
{
    if (cursor->off + 1 > cursor->len) {
        cursor->off = cursor->len + 1;
        return 0;
    }
    return cursor->data[cursor->off++];
}

static uint32_t rd_u32(struct cursor *cursor)
{
    if (cursor->off + 4 > cursor->len) {
        cursor->off = cursor->len + 1;
        return 0;
    }
    uint32_t value;
    memcpy(&value, cursor->data + cursor->off, 4);
    cursor->off += 4;
    return value;
}

static int32_t rd_i32(struct cursor *cursor)
{
    uint32_t value = rd_u32(cursor);
    int32_t out;
    memcpy(&out, &value, 4);
    return out;
}

static float rd_f32(struct cursor *cursor)
{
    uint32_t value = rd_u32(cursor);
    float out;
    memcpy(&out, &value, 4);
    return out;
}

static void skip(struct cursor *cursor, size_t bytes)
{
    cursor->off += bytes;
}

static uint8_t to_byte(float channel)
{
    if (channel <= 0.0f) {
        return 0;
    }
    if (channel >= 1.0f) {
        return 255;
    }
    return (uint8_t)(channel * 255.0f + 0.5f);
}

/* ydraw fill/stroke colors are 0xAARRGGBB (matches ybrowser's pack + the
 * 0xffc0c0c0 literals in ybrowser-paint.c). */
static uint32_t pack_argb(float r, float g, float b, float a)
{
    return ((uint32_t)to_byte(a) << 24) | ((uint32_t)to_byte(r) << 16) |
           ((uint32_t)to_byte(g) << 8) | (uint32_t)to_byte(b);
}

/* A decoded PaintFlags block. */
struct paint_flags {
    float r, g, b, a;
    uint8_t style; /* 0 fill, 1 stroke, 2 stroke+fill */
    float stroke_width;
};

static struct paint_flags rd_flags(struct cursor *cursor)
{
    struct paint_flags flags;
    flags.r = rd_f32(cursor);
    flags.g = rd_f32(cursor);
    flags.b = rd_f32(cursor);
    flags.a = rd_f32(cursor);
    flags.style = rd_u8(cursor);
    (void)rd_u8(cursor); /* antialias */
    (void)rd_u8(cursor); /* blend mode */
    (void)rd_u8(cursor); /* shader tag */
    flags.stroke_width = rd_f32(cursor);
    return flags;
}

static void skip_path(struct cursor *cursor)
{
    (void)rd_u8(cursor); /* fill type */
    uint32_t verbs = rd_u32(cursor);
    for (uint32_t i = 0; i < verbs; i++) {
        uint8_t verb = rd_u8(cursor);
        switch (verb) {
        case 0:
        case 1:
            skip(cursor, 8);
            break; /* move, line */
        case 2:
            skip(cursor, 16);
            break; /* quad */
        case 3:
            skip(cursor, 20);
            break; /* conic */
        case 4:
            skip(cursor, 24);
            break; /* cubic */
        default:
            break; /* close */
        }
    }
}

/* Emit one axis-aligned (rounded) box into the drawable list. */
static void emit_box(struct yetty_ydraw_drawable_list *list, uint32_t *z, float left, float top,
                     float right, float bottom, float corner_radius,
                     const struct paint_flags *flags)
{
    struct yetty_ysdf_box box = {
        .center_x = (left + right) * 0.5f,
        .center_y = (top + bottom) * 0.5f,
        .half_width = (right - left) * 0.5f,
        .half_height = (bottom - top) * 0.5f,
        .corner_radius = corner_radius,
    };
    uint32_t color = pack_argb(flags->r, flags->g, flags->b, flags->a);
    if (flags->style == 1) {
        (void)yetty_ydraw_drawable_list_add_cmd_add_box(
            list, 0, (*z)++, 0u, color, flags->stroke_width > 0 ? flags->stroke_width : 1.0f, &box);
    } else {
        (void)yetty_ydraw_drawable_list_add_cmd_add_box(list, 0, (*z)++, color, 0u, 0.0f, &box);
    }
}

int main(int argc, char **argv)
{
    /* Slurp the wire from a file arg or stdin. */
    FILE *in = stdin;
    if (argc > 1 && strcmp(argv[1], "-") != 0) {
        in = fopen(argv[1], "rb");
        if (!in) {
            fprintf(stderr, "ywire: cannot open %s\n", argv[1]);
            return 1;
        }
    }
    size_t cap = 1 << 16, len = 0;
    uint8_t *data = malloc(cap);
    for (;;) {
        if (len == cap) {
            cap *= 2;
            data = realloc(data, cap);
        }
        size_t got = fread(data + len, 1, cap - len, in);
        len += got;
        if (got == 0) {
            break;
        }
    }
    if (in != stdin) {
        fclose(in);
    }

    struct cursor cursor = {.data = data, .len = len, .off = 0};
    if (rd_u8(&cursor) != OP_FRAME_BEGIN) {
        fprintf(stderr, "ywire: not a wire stream (missing FRAME_BEGIN)\n");
        return 1;
    }
    (void)rd_u32(&cursor); /* frame_id */
    uint32_t width = rd_u32(&cursor);
    uint32_t height = rd_u32(&cursor);
    (void)rd_f32(&cursor); /* device_scale */

    struct yetty_ydraw_drawable_list_config config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = (float)width,
        .scene_max_y = (float)height,
    };
    struct yetty_ydraw_drawable_list_result created =
        yetty_ydraw_drawable_list_config_buffer_create(&config);
    if (YETTY_IS_ERR(created)) {
        fprintf(stderr, "ywire: drawable-list create failed\n");
        return 1;
    }
    struct yetty_ydraw_drawable_list *list = created.value;
    uint32_t z = 0;
    uint32_t boxes = 0;

    while (cursor.off < cursor.len) {
        uint8_t op = rd_u8(&cursor);
        if (op == OP_FRAME_END) {
            break;
        }
        switch (op) {
        case OP_SAVE:
        case OP_RESTORE:
        case OP_DRAW_RECORD:
            break;
        case OP_TRANSLATE:
            skip(&cursor, 8);
            break;
        case OP_SCALE:
            skip(&cursor, 8);
            break;
        case OP_ROTATE:
            skip(&cursor, 4);
            break;
        case OP_CONCAT:
        case OP_SET_MATRIX:
            skip(&cursor, 64);
            break;
        case OP_CLIP_RECT:
            skip(&cursor, 16 + 2);
            break;
        case OP_CLIP_RRECT:
            skip(&cursor, 48 + 2);
            break;
        case OP_CLIP_PATH:
            skip_path(&cursor);
            skip(&cursor, 2);
            break;
        case OP_DRAW_COLOR: {
            struct paint_flags flags = {
                rd_f32(&cursor), rd_f32(&cursor), rd_f32(&cursor), rd_f32(&cursor), 0, 0.0f};
            (void)rd_u8(&cursor); /* blend */
            emit_box(list, &z, 0, 0, (float)width, (float)height, 0.0f, &flags);
            boxes++;
            break;
        }
        case OP_DRAW_RECT:
        case OP_DRAW_OVAL: {
            struct paint_flags flags = rd_flags(&cursor);
            float l = rd_f32(&cursor), t = rd_f32(&cursor), r = rd_f32(&cursor),
                  b = rd_f32(&cursor);
            emit_box(list, &z, l, t, r, b, 0.0f, &flags);
            boxes++;
            break;
        }
        case OP_DRAW_IRECT: {
            struct paint_flags flags = rd_flags(&cursor);
            float l = (float)rd_i32(&cursor), t = (float)rd_i32(&cursor);
            float r = (float)rd_i32(&cursor), b = (float)rd_i32(&cursor);
            emit_box(list, &z, l, t, r, b, 0.0f, &flags);
            boxes++;
            break;
        }
        case OP_DRAW_RRECT: {
            struct paint_flags flags = rd_flags(&cursor);
            float l = rd_f32(&cursor), t = rd_f32(&cursor), r = rd_f32(&cursor),
                  b = rd_f32(&cursor);
            float rx = rd_f32(&cursor);
            skip(&cursor, 28); /* remaining 7 radii floats */
            emit_box(list, &z, l, t, r, b, rx, &flags);
            boxes++;
            break;
        }
        case OP_DRAW_DRRECT: {
            struct paint_flags flags = rd_flags(&cursor);
            float l = rd_f32(&cursor), t = rd_f32(&cursor), r = rd_f32(&cursor),
                  b = rd_f32(&cursor);
            float rx = rd_f32(&cursor);
            skip(&cursor, 28 + 48); /* outer remaining radii + inner rrect */
            emit_box(list, &z, l, t, r, b, rx, &flags);
            boxes++;
            break;
        }
        case OP_DRAW_LINE: {
            struct paint_flags flags = rd_flags(&cursor);
            float x0 = rd_f32(&cursor), y0 = rd_f32(&cursor);
            float x1 = rd_f32(&cursor), y1 = rd_f32(&cursor);
            /* Render a thin line as a stroked box spanning the two points. */
            flags.style = 1;
            emit_box(list, &z, x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 < x1 ? x1 : x0,
                     y0 < y1 ? y1 : y0, 0.0f, &flags);
            boxes++;
            break;
        }
        case OP_DRAW_ARC: {
            (void)rd_flags(&cursor);
            skip(&cursor, 16 + 8);
            break;
        }
        case OP_DRAW_PATH:
            (void)rd_flags(&cursor);
            skip_path(&cursor);
            break;
        case OP_DRAW_IMAGE:
            (void)rd_flags(&cursor);
            skip(&cursor, 4 + 8);
            break;
        case OP_DRAW_IMAGE_RECT:
            (void)rd_flags(&cursor);
            skip(&cursor, 4 + 16 + 16);
            break;
        case OP_DRAW_TEXT_BLOB: {
            (void)rd_flags(&cursor);
            (void)rd_u32(&cursor); /* font_id */
            (void)rd_f32(&cursor);
            (void)rd_f32(&cursor); /* x, y */
            uint32_t glyphs = rd_u32(&cursor);
            skip(&cursor, (size_t)glyphs * 10); /* text: TODO real glyphs */
            break;
        }
        case OP_UNSUPPORTED:
            (void)rd_u8(&cursor);
            break;
        default:
            fprintf(stderr, "ywire: unknown op 0x%02x at %zu\n", op, cursor.off - 1);
            goto done;
        }
    }
done:
    fprintf(stderr, "ywire: %u boxes from %ux%u page\n", boxes, width, height);

    struct yetty_ycore_size_result emitted = yetty_ycat_dcs_bin_emit(list, stdout);
    yetty_ydraw_drawable_list_destroy(list);
    free(data);
    if (YETTY_IS_ERR(emitted)) {
        fprintf(stderr, "ywire: OSC emit failed\n");
        return 1;
    }
    return 0;
}
