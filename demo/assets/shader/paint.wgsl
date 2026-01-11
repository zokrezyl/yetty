// Motion blur / paint effect
// BufferA stores accumulated paint strokes
// Ported from Shadertoy-style multipass pattern

//--- BufferA
// Accumulate paint strokes with slow fade

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let uv = fragCoord / iResolution();
    let res = iResolution();
    
    // Fade previous frame slowly
    var prev = texture(iChannel0, uv) * 0.995;
    
    // Only paint when mouse is pressed/grabbed
    let mouse = iMouse();
    if (mouse.w > 0.5 || mouse.z > 0.5) {
        let mousePos = vec2<f32>(mouse.x, 1.0 - mouse.y);
        let dist = distance(uv, mousePos);
        
        // Brush size controlled by param (scroll wheel)
        let brushSize = 0.02 + iParam() * 0.08;
        let brush = smoothstep(brushSize, brushSize * 0.5, dist);
        
        // Color varies with position and time
        let angle = atan2(uv.y - 0.5, uv.x - 0.5);
        let hue = angle / 6.283 + iTime() * 0.1;
        let r = sin(hue * 6.283) * 0.5 + 0.5;
        let g = sin(hue * 6.283 + 2.094) * 0.5 + 0.5;
        let b = sin(hue * 6.283 + 4.189) * 0.5 + 0.5;
        
        let paint = vec3<f32>(r, g, b) * brush;
        prev = vec4<f32>(max(prev.rgb, paint), 1.0);
    }
    
    return prev;
}

//--- Image
// Display buffer with subtle vignette

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let uv = fragCoord / iResolution();
    
    let buf = texture(iChannel0, uv);
    
    // Vignette effect
    let center = vec2<f32>(0.5, 0.5);
    let vignette = 1.0 - smoothstep(0.3, 0.8, distance(uv, center));
    
    // Subtle background gradient when empty
    let bg = vec3<f32>(0.05, 0.05, 0.1) * (1.0 - uv.y * 0.5);
    
    var col = mix(bg, buf.rgb, step(0.01, length(buf.rgb)));
    col = col * (0.7 + vignette * 0.3);
    
    return vec4<f32>(col, 1.0);
}
