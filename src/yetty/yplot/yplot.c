/*
 * yplot.c — high-level convenience wrappers around the auto-generated
 * yplot-gen.c API. See include/yetty/yplot/yplot.h for the contract.
 */

#include <yetty/yplot/yplot.h>

#include <yetty/yexpr/yexpr.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ycore/types.h>
#include <yetty/yterminal/dcs-codes.h>

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

/* Fill `u` with caller config geometry/ranges/flags and palette-default
 * colors. Shared by the expression path and the precompiled-program path. */
static void yplot_init_base_uniforms(const struct yetty_yplot_render_config *config,
                                     struct yetty_yplot_uniforms *u)
{
    memset(u, 0, sizeof(*u));
    u->bounds_x = config ? config->bounds_x : 0.0f;
    u->bounds_y = config ? config->bounds_y : 0.0f;
    u->bounds_w = (config && config->bounds_w > 0.0f) ? config->bounds_w : 400.0f;
    u->bounds_h = (config && config->bounds_h > 0.0f) ? config->bounds_h : 200.0f;
    if (config && (config->x_min != 0.0f || config->x_max != 0.0f)) {
        u->x_min = config->x_min;
        u->x_max = config->x_max;
    } else {
        u->x_min = -3.14159f;
        u->x_max = 3.14159f;
    }
    if (config && (config->y_min != 0.0f || config->y_max != 0.0f)) {
        u->y_min = config->y_min;
        u->y_max = config->y_max;
    } else {
        u->y_min = -1.5f;
        u->y_max = 1.5f;
    }
    u->flags = config && config->flags
                   ? config->flags
                   : (YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS);

    for (int i = 0; i < 8; i++) {
        u->colors[i] = YPLOT_PALETTE[i];
    }
}

/* Fill `u` with caller config values, palette-default colors, and a
 * compiled bytecode block from `source` (may be empty when caller only
 * wants buffer curves). `bc_buf` / `bc_cap` is a caller-owned scratch
 * area for the serialized yfsvm program. The parsed plot expression is
 * also handed back to the caller in `*out_plot` so it can synthesise
 * data buffers from `name=buffer` declarations; `expr_arena` is the
 * caller-owned node arena the parsed AST points into. */
static struct yetty_ycore_void_result yplot_build_uniforms_and_bytecode(
    const char *source, size_t source_len, const struct yetty_yplot_render_config *config,
    uint32_t *bc_buf, uint32_t bc_cap, struct yetty_yplot_uniforms *u, uint32_t *out_bc_len,
    struct yetty_yexpr_arena *expr_arena, struct yetty_yexpr_plot_expr *out_plot)
{
    yplot_init_base_uniforms(config, u);

    *out_bc_len = 0;
    if (out_plot) {
        memset(out_plot, 0, sizeof(*out_plot));
    }
    if (!source || source_len == 0) {
        u->function_count = 0;
        return YETTY_OK_VOID();
    }

    /* Parse expression(s) using yexpr's plot-syntax. */
    struct yetty_yexpr_plot_expr_result pr = yetty_yexpr_parse_plot(source, source_len, expr_arena);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "yplot: expression parse failed");
    if (out_plot) {
        *out_plot = pr.value;
    }

    /* Domain / viewport overrides come from inline `x=A..B` etc. The
     * @view= viewport currently rebinds the static domain — the shader
     * doesn't yet animate zoom from this, but the override still gives
     * a useful initial framing for the first frame. */
    if (pr.value.has_x_range) {
        u->x_min = pr.value.x_min;
        u->x_max = pr.value.x_max;
    }
    if (pr.value.has_y_range) {
        u->y_min = pr.value.y_min;
        u->y_max = pr.value.y_max;
    }
    if (pr.value.has_view) {
        u->x_min = pr.value.view_x_min;
        u->x_max = pr.value.view_x_max;
        u->y_min = pr.value.view_y_min;
        u->y_max = pr.value.view_y_max;
    }

    u->function_count = pr.value.def_count;
    if (u->function_count > 8) {
        u->function_count = 8;
    }

    /* Per-plot @<name>.color overrides. */
    for (uint32_t i = 0; i < pr.value.attr_count; i++) {
        const struct yetty_yexpr_plot_attr *attr = &pr.value.attrs[i];
        if (strcmp(attr->attr_name, "color") != 0) {
            continue;
        }
        for (uint32_t j = 0; j < u->function_count; j++) {
            if (strcmp(pr.value.defs[j].name, attr->plot_name) == 0) {
                uint32_t c;
                if (parse_hex_color(attr->value, &c)) {
                    u->colors[j] = c;
                }
                break;
            }
        }
    }

    /* Compile to bytecode. The compile_multi entry threads the plot
     * expression's buffer table into the codegen so f(x) calls resolve
     * to LOAD_S against the buffer's slot. */
    struct yetty_yfsvm_program_result prog = yetty_yfsvm_compile_multi(&pr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, prog, "yplot: yfsvm compile failed");

    /* Tell the receiver to subscribe to the animation timer when the
     * compiled program references LOAD_T. The hook reads this bit at
     * instance_create — saves scanning the bytecode every time. */
    if (prog.value.uses_time) {
        u->flags |= YETTY_YPLOT_FLAG_USES_TIME;
    }

    /* A program that reads `y` is a 2D field f(x,y) — flag it so the shader
     * renders a colormapped heatmap rather than treating the result as a
     * line height. */
    if (prog.value.uses_y) {
        u->flags |= YETTY_YPLOT_FLAG_FIELD;
    }

    uint32_t bc_len = yetty_yfsvm_program_serialize(&prog.value, bc_buf, bc_cap);
    if (bc_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "yplot: bytecode serialize failed");
    }
    *out_bc_len = bc_len;
    return YETTY_OK_VOID();
}

/* Pack uniforms + buffers into a fresh ydraw buffer carrying one yplot prim. */
static struct yetty_ydraw_drawable_list_result yplot_emit_prim(
    const struct yetty_yplot_uniforms *u, const struct yetty_yplot_buffers *bufs)
{
    size_t required = yetty_yplot_uniforms_serialized_size(u, bufs);
    uint8_t *drawable_buf = malloc(required);
    if (!drawable_buf) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: prim alloc failed");
    }
    struct yetty_ycore_size_result ser =
        yetty_yplot_uniforms_serialize(u, bufs, drawable_buf, required);
    if (YETTY_IS_ERR(ser)) {
        free(drawable_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: serialize failed", ser);
    }

    struct yetty_ydraw_drawable_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = u->bounds_x + u->bounds_w,
        .scene_max_y = u->bounds_y + u->bounds_h,
    };
    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        free(drawable_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: ydraw buffer create failed", br);
    }

    struct yetty_ydraw_id_result idr =
        yetty_ydraw_drawable_list_add_prim(br.value, drawable_buf, required);
    free(drawable_buf);
    if (YETTY_IS_ERR(idr)) {
        yetty_ydraw_drawable_list_destroy(br.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: ydraw add_prim failed", idr);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, br.value);
}

struct yetty_ydraw_drawable_list_result yetty_yplot_render(
    const char *source, size_t len, const struct yetty_yplot_render_config *config)
{
    return yetty_yplot_render_with_buffers(source, len, NULL, 0, config);
}

struct yetty_ydraw_drawable_list_result yetty_yplot_render_with_buffers(
    const char *source, size_t len, const struct yetty_yplot_buffer_input *buffers,
    size_t buffer_count, const struct yetty_yplot_render_config *config)
{
    if (!source && len > 0) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "source is NULL");
    }
    if (!buffers && buffer_count > 0) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "buffers is NULL but buffer_count > 0");
    }

    struct yetty_yplot_uniforms u;
    uint32_t bc_buf[1024];
    uint32_t bc_len = 0;
    struct yetty_yexpr_arena expr_arena;
    struct yetty_yexpr_plot_expr parsed = {0};

    struct yetty_ycore_void_result ub = yplot_build_uniforms_and_bytecode(
        source, len, config, bc_buf, (uint32_t)(sizeof bc_buf / sizeof bc_buf[0]), &u, &bc_len,
        &expr_arena, &parsed);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, ub, "yplot: uniforms/bytecode build failed");

    /* Buffer slots come from TWO sources, layered in this order so that
     * sampler-slot indices match what the compiler emitted:
     *   1) declarations from source (`f=buffer; @f.size=N; @f.values=…`)
     *      — slot index == declaration order, matching compiler's LOAD_S idx
     *   2) caller-supplied data buffers via the API — appended after */
    uint32_t decl_count = parsed.buffer_count;
    if (decl_count > 8) {
        decl_count = 8;
    }
    size_t total_bufs = (size_t)decl_count + buffer_count;

    struct yetty_yplot_data_buffer wire_bufs_stack[8];
    struct yetty_yplot_data_buffer *wire_bufs = NULL;
    /* Zero-fill scratch — declarations without inline values render as a
     * flat baseline until their owner streams in real data. Allocated as
     * one slab and aliased to per-decl spans, freed at the end. */
    float *zero_fill = NULL;
    size_t zero_fill_total = 0;

    if (total_bufs > 0) {
        wire_bufs = (total_bufs <= 8) ? wire_bufs_stack : malloc(total_bufs * sizeof(*wire_bufs));
        if (!wire_bufs) {
            return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: buffer view alloc failed");
        }

        /* First pass: how much zero-fill do we need? */
        for (uint32_t i = 0; i < decl_count; i++) {
            const struct yetty_yexpr_plot_buffer *d = &parsed.buffers[i];
            if (d->inline_count == 0 && d->size > 0) {
                zero_fill_total += d->size;
            }
        }
        if (zero_fill_total > 0) {
            zero_fill = calloc(zero_fill_total, sizeof(float));
            if (!zero_fill) {
                if (wire_bufs != wire_bufs_stack) {
                    free(wire_bufs);
                }
                return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: zero-fill alloc failed");
            }
        }

        /* Second pass: populate wire entries for declarations. */
        size_t zf_off = 0;
        for (uint32_t i = 0; i < decl_count; i++) {
            const struct yetty_yexpr_plot_buffer *d = &parsed.buffers[i];
            if (d->inline_count > 0) {
                wire_bufs[i].samples = d->inline_values;
                wire_bufs[i].count = d->inline_count;
            } else if (d->size > 0) {
                wire_bufs[i].samples = zero_fill + zf_off;
                wire_bufs[i].count = d->size;
                zf_off += d->size;
            } else {
                /* No values, no size — render a degenerate two-zero buffer
                 * so the shader's >=2 check is satisfied and the curve is
                 * a flat baseline until the owner sets it up. */
                wire_bufs[i].samples = NULL;
                wire_bufs[i].count = 0;
            }
        }

        /* Caller-supplied buffers append after declarations. */
        for (size_t i = 0; i < buffer_count; i++) {
            wire_bufs[decl_count + i].samples = buffers[i].samples;
            wire_bufs[decl_count + i].count = buffers[i].count;
        }
    }

    /* Per-buffer color slots: expressions occupy 0..function_count-1, then
     * buffers (declarations first, then API) fill the next slots (mod 8).
     * Caller-supplied colors override the palette defaults already filled. */
    for (size_t i = 0; i < buffer_count; i++) {
        uint32_t slot = (u.function_count + (uint32_t)decl_count + (uint32_t)i) % 8u;
        if (buffers[i].color != 0u) {
            u.colors[slot] = buffers[i].color;
        }
    }

    struct yetty_yplot_buffers bufs = {
        .bytecode = bc_len > 0 ? bc_buf : NULL,
        .bytecode_len = bc_len,
        .data = wire_bufs,
        .data_count = total_bufs,
    };

    struct yetty_ydraw_drawable_list_result out = yplot_emit_prim(&u, &bufs);
    if (wire_bufs && wire_bufs != wire_bufs_stack) {
        free(wire_bufs);
    }
    free(zero_fill);
    return out;
}

struct yetty_ydraw_drawable_list_result yetty_yplot_render_program(
    const uint32_t *program, uint32_t program_words, const struct yetty_yplot_render_config *config)
{
    if (!program) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: program is NULL");
    }
    /* Fully validate the serialized word layout before trusting any field —
     * this blob may have arrived from an external frontend and is headed for
     * the GPU interpreter, where malformed bytecode is undefined behaviour. */
    struct yetty_ycore_void_result valid = yetty_yfsvm_validate_serialized(program, program_words);
    if (YETTY_IS_ERR(valid)) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: invalid program bytecode", valid);
    }

    struct yetty_yplot_uniforms u;
    yplot_init_base_uniforms(config, &u);

    uint32_t function_count = program[2];
    uint32_t const_count = program[3];
    u.function_count = function_count > 8u ? 8u : function_count;

    /* Derive the animation/field flags by scanning the code segment for the
     * input opcodes — the same signals the expression path reads from the
     * compiler's uses_time / uses_y. The code segment follows the padded
     * function table and the constant pool. */
    uint32_t code_offset = 4u + YFSVM_MAX_FUNCTIONS + const_count;
    if (code_offset <= program_words) {
        for (uint32_t i = code_offset; i < program_words; i++) {
            uint32_t op = yfsvm_decode_opcode(program[i]);
            if (op == YETTY_YFSVM_OP_LOAD_T) {
                u.flags |= YETTY_YPLOT_FLAG_USES_TIME;
            } else if (op == YETTY_YFSVM_OP_LOAD_Y) {
                u.flags |= YETTY_YPLOT_FLAG_FIELD;
            }
        }
    }

    struct yetty_yplot_buffers bufs = {
        .bytecode = program,
        .bytecode_len = program_words,
        .data = NULL,
        .data_count = 0,
    };
    return yplot_emit_prim(&u, &bufs);
}

struct yetty_ycore_size_result yetty_yplot_osc_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "yplot_osc_bin_emit: NULL buffer or out");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)buffer, &raw);
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
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
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
