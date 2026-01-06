// Cursor shader - GPU-driven blinking cursor
// Uses ShaderManager's QuadUniforms: iTime, iResolution, iParam0 (cursor style)
//
// Cursor styles (iParam0):
//   0 = Block (full cell)
//   1 = Underline (bottom 2px)
//   2 = Bar (left 2px)

fn fs_main(uv: vec2<f32>) -> vec4<f32> {
    // Blink: on for 500ms, off for 500ms (1Hz total, 50% duty cycle)
    let blinkPeriod = 1.0;
    let blinkPhase = fract(u.iTime / blinkPeriod);
    let visible = blinkPhase < 0.5;

    if (!visible) {
        discard;
    }

    let cursorStyle = u.iParam0;

    // Cursor color (bright white/green)
    let cursorColor = vec4<f32>(0.8, 0.9, 0.8, 0.9);

    // Check if this pixel should be drawn based on cursor style
    var draw = false;

    if (cursorStyle == 0u) {
        // Block cursor - full cell
        draw = true;
    } else if (cursorStyle == 1u) {
        // Underline cursor - bottom 15% of cell
        draw = uv.y > 0.85;
    } else if (cursorStyle == 2u) {
        // Bar cursor - left 10% of cell
        draw = uv.x < 0.1;
    } else {
        // Default to block
        draw = true;
    }

    if (!draw) {
        discard;
    }

    return cursorColor;
}
