// WGSL shader for ShaderToy plugin
// Must define: fn mainImage(fragCoord: vec2<f32>) -> vec4<f32>
// Available: iTime(), iResolution(), iMouse(), iParam(), iZoom(), iGrabbed(), iMouseDown()
// iMouse(): vec4 (x, y normalized 0-1, grabbed, buttonDown)
// iParam(): scroll-controlled value 0-1
// iZoom(): ctrl+scroll zoom level (default 1.0)

fn mainImage(fragCoord: vec2<f32>) -> vec4<f32> {
    let res = iResolution();
    let mouse = iMouse();
    let mouseUV = vec2<f32>(mouse.x, mouse.y);

    // Apply zoom: zoom into center, centered on mouse position
    let center = mouseUV;
    var uv = fragCoord / res;
    uv = center + (uv - center) / iZoom();

    let t = iTime() * 0.5;

    // Use iParam() to control pattern frequency (SCROLL to change!)
    let freq = 3.0 + iParam() * 30.0;

    // Use mouse.x to control animation speed
    let speed = 0.2 + mouseUV.x * 2.0;

    // Use mouse.y to control color palette
    let colorOffset = mouseUV.y * 6.28;

    var v: f32 = 0.0;
    v += sin((uv.x * freq) + t * speed);
    v += sin((uv.y * freq) + t * speed);
    v += sin((uv.x * freq + uv.y * freq) + t * speed);
    v += sin(sqrt(uv.x * uv.x * freq * freq + uv.y * uv.y * freq * freq) + t * speed);

    let col = vec3<f32>(
        sin(v * 3.14159 + colorOffset) * 0.5 + 0.5,
        sin(v * 3.14159 + 2.094 + colorOffset) * 0.5 + 0.5,
        sin(v * 3.14159 + 4.188 + colorOffset) * 0.5 + 0.5
    );

    // Draw bright crosshair at mouse position (in screen space, not zoomed)
    let screenUV = fragCoord / res;
    let dx = abs(screenUV.x - mouseUV.x);
    let dy = abs(screenUV.y - mouseUV.y);
    var crosshair: f32 = 0.0;
    if (dx < 0.005 || dy < 0.005) {
        crosshair = 1.0;
    }
    // Circle around mouse
    let dist = distance(screenUV, mouseUV);
    if (dist > 0.08 && dist < 0.1) {
        crosshair = 1.0;
    }

    // Make crosshair color based on grabbed state
    var crossColor = vec3<f32>(1.0, 1.0, 0.0);  // Yellow normally
    if (iGrabbed()) {
        crossColor = vec3<f32>(0.0, 1.0, 0.0);  // Green when grabbed
    }

    return vec4<f32>(mix(col, crossColor, crosshair), 1.0);
}
