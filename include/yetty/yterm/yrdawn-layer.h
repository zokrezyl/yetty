/*
 * yrdawn-layer — server side of the WebGPU-over-OSC bridge.
 *
 * Receives CMD/HELLO/BULK/BYE frames over ywire (OSC codes 6200xx),
 * decodes each CMD's method body, dispatches into Dawn, and renders
 * the resulting Dawn texture into the layer's own surface. Reverse
 * direction (REPLY/EVENT/HELLO_ACK/BULK/ERROR; OSC codes 7200xx) goes
 * out via the layer's emit_osc_fn back to the PTY child.
 *
 * Skeleton: HELLO/HELLO_ACK works, BYE works, CMD frames produce
 * UNKNOWN_METHOD replies until src/yetty/yrdawn/generate.py lands the
 * per-method dispatch. The handle table is here but empty.
 */
#ifndef YETTY_YTERM_YRDAWN_LAYER_H
#define YETTY_YTERM_YRDAWN_LAYER_H

#include <stdint.h>

#include <yetty/yetty/yetty.h>
#include <yetty/yterm/terminal.h>
#include <yetty/ywire/wire-statemachine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create a yrdawn-layer. Caller still has to:
 *   - wire base.emit_osc_fn (for outbound HELLO_ACK / REPLY / EVENT)
 *   - register the layer with the ywire SM for OSC codes
 *     YETTY_YRDAWN_OSC_CS_HELLO / _CMD / _BULK / _BYE
 *   - add it to the terminal's layer list.
 * The context's gpu_context (device/queue/surface_format) is stashed
 * for the layer's render path; until the codegen-emitted dispatcher
 * starts driving Dawn, those fields stay unused. */
struct yetty_yterm_terminal_layer_result yetty_yterm_yrdawn_layer_create(
    uint32_t cols, uint32_t rows, float cell_width, float cell_height,
    const struct yetty_context *context);

/* Wire-SM dispatch entry. Pass the layer's base pointer as userdata. */
struct yetty_ycore_void_result yetty_yterm_yrdawn_layer_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_YRDAWN_LAYER_H */
