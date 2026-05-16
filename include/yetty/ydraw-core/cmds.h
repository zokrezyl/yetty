/* ydraw control cmds — non-drawing primitives that have side effects on
 * the receiving canvas at decode time.
 *
 * Type-id space for the ydraw wire format:
 *   [0x00000000, 0x0000FFFF]  cmds (this header)
 *   [0x10000000, 0x1FFFFFFF]  SDF (paint primitives, generated)
 *   [0x40000000, 0x7FFFFFFF]  flyweight (FONT, TEXT_SPAN)
 *   [0x80000000, 0xFFFFFFFF]  complex (yplot, yimage, ...)
 *
 * Cmds use the same FAM wire layout as flyweight prims:
 *     u32 type
 *     u32 payload_size  (bytes of payload, 4-aligned)
 *     u8  payload[payload_size]
 * — so the iterator walks them with the same stride machinery.
 */
#ifndef YETTY_YDRAW_CORE_CMDS_H
#define YETTY_YDRAW_CORE_CMDS_H

#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YDRAW_CMD_BASE 0x00000000u
#define YETTY_YDRAW_CMD_END 0x0000FFFFu

/* CMD_ZERO: clear the receiving ydraw canvas AND reset the cursor to
 * (col=0, row=0). The canvas's cursor-set callback fires on the reset, so
 * sibling layers (text/vterm) see the cursor move too — that's how the
 * "all layers cursor" semantic propagates without this header reaching
 * outside the ydraw module.
 *
 * Empty payload. Use at the start of every full-redraw frame buffer in
 * GUI / fullscreen producers (ygui sends one CMD_ZERO + the new prims per
 * frame, eliminating the separate YDRAW_CLEAR OSC envelope). */
#define YETTY_YDRAW_CMD_ZERO 0x00000000u

struct yetty_ydraw_draw_list;

/* Append a CMD_ZERO at the current write head of the buffer. */
struct yetty_ycore_void_result yetty_ydraw_draw_list_add_cmd_zero(
    struct yetty_ydraw_draw_list *buf);

/* Flyweight handler for the cmd tier — returns the cmd base_ops which
 * stride by the FAM `8 + payload_size` bytes (same as flyweight prims).
 * Register at startup with:
 *   yetty_ydraw_flyweight_registry_add(reg,
 *       YETTY_YDRAW_CMD_BASE, YETTY_YDRAW_CMD_END,
 *       yetty_ydraw_cmd_handler);
 */
struct yetty_ydraw_drawable_base_ops_ptr_result yetty_ydraw_cmd_handler(uint32_t prim_type);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_CMDS_H */
