// Background layer — solid opaque RGBA fill across the pane viewport.
//
// The pane wipe. With the shared big-target / no-LoadOp_Clear architecture,
// every pane needs an opaque scissored draw at the bottom of its layer
// stack so its previous-frame upper-layer pixels (which would otherwise
// ghost through alpha gaps) get cleanly overwritten without touching any
// other pane's pixels. Confined to the pane by render-target's viewport +
// scissor; the vertex shader emits the full-NDC quad as usual.
//
// Generated bindings (prepended by binder):
//   uniforms.background_color  — vec4<f32> RGBA, components 0..1

// RENDER_LAYER_BINDINGS_PLACEHOLDER

struct VertexInput  { @location(0) position: vec2<f32>, };
struct VertexOutput { @builtin(position) position: vec4<f32>, };

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4<f32>(input.position, 0.0, 1.0);
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    return uniforms.background_color;
}
