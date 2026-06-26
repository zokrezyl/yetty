/*
 * handler-circuit.c — circuit schematic DSL → ydraw buffer via ycircuit.
 *
 * Draws a schematic into one drawable list: wires and component bodies as
 * SDF segments / circles / triangles, designators / values / labels as MSDF
 * text spans using the receiver's default font. The grid pitch tracks the
 * cell height so the schematic reads at the terminal's text scale.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ycircuit/circuit.h>

/* Error-path teardown is best-effort — absorb a failed destroy so the
 * original error is the one that surfaces. */
static struct yetty_ycore_void_result circuit_destroy_best_effort(
    struct yetty_yclass_object *circuit)
{
    struct yetty_ycore_void_result destroy_res = yetty_ycircuit_destroy(circuit);
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
    return YETTY_OK_VOID();
}

struct yetty_ydraw_drawable_list_result yetty_ycat_handler_circuit(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config)
{
    (void)path_hint;

    struct yetty_ycore_void_result register_res = yetty_ycircuit_register();
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, register_res, "ycircuit register failed");

    struct yetty_yclass_object_ptr_result object_res = yetty_ycircuit_circuit_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_res, "ycircuit create failed");
    struct yetty_yclass_object *circuit = object_res.value;

    /* The grid pitch tracks the cell height (clamped so tiny cells don't
     * produce an unreadable schematic). The DSL's own `grid` directive wins —
     * pass 0 here only when the cell height is unknown. */
    float cell_height = (config && config->cell_height) ? (float)config->cell_height : 16.0f;
    float grid_px = cell_height * 0.9f;
    if (grid_px < 10.0f) {
        grid_px = 10.0f;
    }

    struct yetty_ycore_void_result configure_res =
        yetty_ycircuit_configure(circuit, grid_px, YETTY_YCIRCUIT_FLAG_NONE);
    if (YETTY_IS_ERR(configure_res)) {
        circuit_destroy_best_effort(circuit);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ycircuit configure failed", configure_res);
    }

    struct yetty_ycore_void_result parse_res =
        yetty_ycircuit_parse(circuit, (const char *)bytes, len);
    if (YETTY_IS_ERR(parse_res)) {
        circuit_destroy_best_effort(circuit);
        return YETTY_ERR(yetty_ydraw_drawable_list, "circuit parse failed", parse_res);
    }

    struct yetty_ydraw_drawable_list_result render_res = yetty_ycircuit_render(circuit);
    /* The rendered list owns its own bytes — the circuit model can go
     * regardless of the render outcome. */
    circuit_destroy_best_effort(circuit);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, render_res, "circuit render failed");
    return render_res;
}
