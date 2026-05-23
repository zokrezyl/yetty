/*
 * yetty_yrdawn_figure — yrdawn rendered as a compositor figure.
 *
 * Subclass of yetty_yfigure_figure. Same WGPU pipeline as the old
 * yrdawn terminal layer (fullscreen-quad shader that samples the
 * figure's frame texture); placement comes from self->rect rather
 * than target->viewport, so a figure can occupy the host surface
 * fully OR sit at any sub-rect inside it. Multiple yrdawn figures
 * may coexist on one compositor.
 *
 * Owns: WGPU pipeline + sampler + bind-group-layout, current frame
 * WGPUTexture/view/bind-group, handle table for codegen dispatch,
 * bulk reassembly state, connected flag.
 *
 * Borrows: Dawn instance/adapter/device/queue/surface_format from
 * context->runtime->gpu.
 *
 * Wire: the host (typically ycompositor::process_input) decodes one
 * complete yrdawn OSC envelope and hands the payload to
 * yetty_yrdawn_figure_handle_osc. Outbound HELLO_ACK / REPLY / EVENT
 * / SC_KEY / SC_RESIZE go out through the emit callback wired by
 * yetty_yrdawn_figure_set_emit_osc.
 */
#ifndef YETTY_YRDAWN_FIGURE_H
#define YETTY_YRDAWN_FIGURE_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrdawn_figure;

YETTY_YRESULT_DECLARE(yetty_yrdawn_figure_ptr, struct yetty_yrdawn_figure *);

/* Outbound OSC emit. Called when the figure needs to send a frame back
 * to the client (HELLO_ACK, REPLY, EVENT, SC_KEY, SC_RESIZE). The host
 * is responsible for the OSC framing (yface) and PTY write. */
typedef struct yetty_ycore_void_result (*yetty_yrdawn_emit_osc_fn)(
    int osc_code, const void *payload, size_t len, void *user);

/* Optional repaint nudge. Called from set_frame so the host kicks a
 * compositor render pass without waiting for the next polling tick.
 * NULL is fine — the figure's dirty bit is set either way. */
typedef struct yetty_ycore_void_result (*yetty_yrdawn_request_render_fn)(void *user);

/* Create a yrdawn figure at `rect` (absolute target pixel space). The
 * rect can equal the host pane (full-screen yrdawn) or be a sub-rect
 * (yrdawn-in-a-window); the figure does not assume one or the other.
 * Builds the WGPU pipeline + sampler on the shared Dawn device pulled
 * from context. */
struct yetty_yrdawn_figure_ptr_result yetty_yrdawn_figure_create(
    struct yetty_ycore_rectangle rect,
    const struct yetty_context *context);

/* Upcast — stable pointer; pass to yetty_yfigure_container_add_child. */
struct yetty_yfigure_figure *yetty_yrdawn_figure_as_figure(
    struct yetty_yrdawn_figure *figure);

void yetty_yrdawn_figure_set_emit_osc(
    struct yetty_yrdawn_figure *figure,
    yetty_yrdawn_emit_osc_fn fn, void *user);

void yetty_yrdawn_figure_set_request_render(
    struct yetty_yrdawn_figure *figure,
    yetty_yrdawn_request_render_fn fn, void *user);

/* Decode one yrdawn OSC envelope. `osc_code` is one of
 * YETTY_YRDAWN_OSC_CS_HELLO / _CMD / _BULK / _BYE; `payload` is the
 * deflated body the wire SM already assembled. */
struct yetty_ycore_void_result yetty_yrdawn_figure_handle_osc(
    struct yetty_yrdawn_figure *figure,
    int osc_code, const uint8_t *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRDAWN_FIGURE_H */
