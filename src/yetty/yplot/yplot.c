/*
 * yplot.c — high-level convenience wrappers around the auto-generated
 * yplot-gen.c API. See include/yetty/yplot/yplot.h for the contract.
 */

#include <yetty/yplot/yplot.h>

#include <yetty/yexpr/yexpr.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ycore/types.h>
#include <yetty/yterm/osc-codes.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default 8-color palette — matches the yaml factory and the demo plots. */
static const uint32_t YPLOT_PALETTE[8] = {
    0xFFFF6B6B, 0xFF4ECDC4, 0xFFFFE66D, 0xFF95E1D3, 0xFFF38181, 0xFFAA96DA, 0xFF72D6C9, 0xFFFCBF49,
};

static int parse_hex_color(const char *s, uint32_t *out)
{
    if (!s || s[0] != '#') {
        return 0;
    }
    const char *h = s + 1;
    size_t hl = strlen(h);
    char buf[7];
    if (hl == 3) {
        buf[0] = h[0];
        buf[1] = h[0];
        buf[2] = h[1];
        buf[3] = h[1];
        buf[4] = h[2];
        buf[5] = h[2];
        buf[6] = '\0';
    } else if (hl == 6) {
        memcpy(buf, h, 6);
        buf[6] = '\0';
    } else {
        return 0;
    }
    char *endp = NULL;
    unsigned long v = strtoul(buf, &endp, 16);
    if (!endp || *endp != '\0') {
        return 0;
    }
    uint32_t r = (uint32_t)((v >> 16) & 0xFF);
    uint32_t g = (uint32_t)((v >> 8) & 0xFF);
    uint32_t b = (uint32_t)(v & 0xFF);
    *out = 0xFF000000u | (b << 16) | (g << 8) | r;
    return 1;
}

struct yetty_ydraw_draw_list_result yetty_yplot_render(
    const char *source, size_t len, const struct yetty_yplot_render_config *config)
{
    if (!source && len > 0) {
        return YETTY_ERR(yetty_ydraw_draw_list, "source is NULL");
    }

    /* Resolve config defaults. */
    struct yetty_yplot_uniforms u = {0};
    u.bounds_x = config ? config->bounds_x : 0.0f;
    u.bounds_y = config ? config->bounds_y : 0.0f;
    u.bounds_w = (config && config->bounds_w > 0.0f) ? config->bounds_w : 400.0f;
    u.bounds_h = (config && config->bounds_h > 0.0f) ? config->bounds_h : 200.0f;
    if (config && (config->x_min != 0.0f || config->x_max != 0.0f)) {
        u.x_min = config->x_min;
        u.x_max = config->x_max;
    } else {
        u.x_min = -3.14159f;
        u.x_max = 3.14159f;
    }
    if (config && (config->y_min != 0.0f || config->y_max != 0.0f)) {
        u.y_min = config->y_min;
        u.y_max = config->y_max;
    } else {
        u.y_min = -1.5f;
        u.y_max = 1.5f;
    }
    u.flags = config && config->flags
                  ? config->flags
                  : (YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS);

    for (int i = 0; i < 8; i++) {
        u.colors[i] = YPLOT_PALETTE[i];
    }

    /* Parse expression(s) using yexpr's plot-syntax (multi-function +
     * @<name>.color attrs). */
    struct yetty_yexpr_plot_parse_result pr = yetty_yexpr_parse_plot(source, len);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ydraw_draw_list, "yplot: expression parse failed", pr);
    }
    u.function_count = pr.value.plot.def_count;
    if (u.function_count > 8) {
        u.function_count = 8;
    }

    /* Override colors from @<name>.color attrs. */
    for (uint32_t i = 0; i < pr.value.plot.attr_count; i++) {
        const struct yetty_yexpr_plot_attr *attr = &pr.value.plot.attrs[i];
        if (strcmp(attr->attr_name, "color") != 0) {
            continue;
        }
        for (uint32_t j = 0; j < u.function_count; j++) {
            if (strcmp(pr.value.plot.defs[j].name, attr->plot_name) == 0) {
                uint32_t c;
                if (parse_hex_color(attr->value, &c)) {
                    u.colors[j] = c;
                }
                break;
            }
        }
    }

    /* Compile to bytecode. */
    struct yetty_yfsvm_program_result prog = yetty_yfsvm_compile_multi(&pr.value.plot);
    if (YETTY_IS_ERR(prog)) {
        return YETTY_ERR(yetty_ydraw_draw_list, "yplot: yfsvm compile failed", prog);
    }
    uint32_t bc_buf[1024];
    uint32_t bc_len = yetty_yfsvm_program_serialize(&prog.value, bc_buf, 1024);
    if (bc_len == 0) {
        return YETTY_ERR(yetty_ydraw_draw_list, "yplot: bytecode serialize failed");
    }
    struct yetty_yplot_buffers bufs = {.bytecode = bc_buf, .bytecode_len = bc_len};

    /* Wire bytes for the prim. */
    size_t required = yetty_yplot_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf) {
        return YETTY_ERR(yetty_ydraw_draw_list, "yplot: prim alloc failed");
    }
    struct yetty_ycore_size_result ser =
        yetty_yplot_uniforms_serialize(&u, &bufs, prim_buf, required);
    if (YETTY_IS_ERR(ser)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_draw_list, "yplot: serialize failed", ser);
    }

    /* Build a fresh ydraw buffer + attach the prim. Scene bounds = the
     * yplot's own w x h (so the receiving canvas knows how much vertical
     * space to reserve). */
    struct yetty_ydraw_draw_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = u.bounds_x + u.bounds_w,
        .scene_max_y = u.bounds_y + u.bounds_h,
    };
    struct yetty_ydraw_draw_list_result br =
        yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ydraw_draw_list, "yplot: ydraw buffer create failed", br);
    }

    struct yetty_ydraw_id_result idr =
        yetty_ydraw_draw_list_add_prim(br.value, prim_buf, required);
    free(prim_buf);
    if (YETTY_IS_ERR(idr)) {
        yetty_ydraw_draw_list_destroy(br.value);
        return YETTY_ERR(yetty_ydraw_draw_list, "yplot: ydraw add_prim failed", idr);
    }

    return YETTY_OK(yetty_ydraw_draw_list, br.value);
}

struct yetty_ycore_size_result yetty_yplot_osc_bin_emit(
    const struct yetty_ydraw_draw_list *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "yplot_osc_bin_emit: NULL buffer or out");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_draw_list_serialize((struct yetty_ydraw_draw_list *)buffer, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_size, "yplot_osc_bin_emit: empty serialize");
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
    struct yetty_ycore_void_result r = yetty_yface_emit(
        YETTY_OSC_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "yplot_osc_bin_emit: yface_emit failed", r);
    }

    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
