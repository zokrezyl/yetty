// Effects library — the single source of truth for yetty's runtime shader
// effects. Ported from ../yetty-poc. Two entry points:
//
//   fx_post_apply(...)  -> vec3  post-color effect on the final RGB
//   fx_coord_apply(...) -> vec2  coordinate distortion of the sampling pixel
//
// Both take an effect index (0 = none) plus context (pixel/screen/time/
// mouse/cursor/cell-size) and 6 params. Callers pass their shared frame clock
// as `time` so every shader animates in phase. This file is used two ways:
//   - vterm text shader: prepended to the hand-written shader string.
//   - ygrid layer: attached as a binder child resource set.
// Keep both call sites in sync with these signatures.
//
// Indices (match the OSC protocol / demo scripts):
//   post : 1 scanlines 2 crt 3 chromatic 4 broken-tv 5 matrix 6 sepia
//          7 pixelate 8 wave 9 invert 10 night-vision 11 vaporwave 12 thermal
//          13 glitch 14 emboss 15 rain 16 matrix-rain 18 thunderstorm
//   coord: 1 fisheye 2 magnify-cursor 3 magnify-mouse 4 warts 5 wandering-wart
//          6 barrel 7 swirl 8 bulge 9 pinch 10 jello 11 heartbeat 12 drunk
//          13 heat-haze 14 underwater 15 earthquake 16 scanline-offset
//          17 vhs-tear 18 melt 19 wave 20 funhouse 21 twist

fn fx_hash11(x: f32) -> f32 {
    return fract(sin(x * 127.1) * 43758.5453);
}

fn fx_hash2(p: vec2<f32>) -> f32 {
    return fract(sin(dot(p, vec2<f32>(12.9898, 78.233))) * 43758.5453);
}

// ---------------------------------------------------------------------------
// Post-color effects
// ---------------------------------------------------------------------------
fn fx_post_apply(index: u32, color: vec3<f32>, pixel: vec2<f32>, screen: vec2<f32>,
                 time: f32, mouse: vec2<f32>, cursor: vec2<f32>, cell: vec2<f32>,
                 p0: f32, p1: f32, p2: f32, p3: f32, p4: f32, p5: f32) -> vec3<f32> {
    let scr = max(screen, vec2<f32>(1.0, 1.0));
    let uv = pixel / scr;
    let lum = dot(color, vec3<f32>(0.299, 0.587, 0.114));

    if (index == 1u) { // scanlines
        let intensity = select(0.3, p0, p0 > 0.0);
        let lw = select(2.0, p1, p1 > 0.0);
        let sl = fract(floor(pixel.y / lw) * 0.5) * 2.0;
        return color * (1.0 - intensity * sl) * (1.0 - 0.02 * sin(time * 5.0));
    } else if (index == 2u) { // crt
        let vg = select(0.4, p0, p0 > 0.0);
        let ss = select(0.15, p1, p1 > 0.0);
        var r = color * (1.0 - vg * pow(length(uv - vec2<f32>(0.5)) * 1.4, 2.0));
        r *= 1.0 - ss * (1.0 - (sin(pixel.y * 3.14159) * 0.5 + 0.5));
        r *= 1.0 - 0.01 * sin(time * 8.0) - 0.005 * sin(time * 17.3);
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 3u) { // chromatic
        let intensity = select(0.5, p0, p0 > 0.0);
        let center = vec2<f32>(select(0.5, p1, p1 > 0.0), select(0.5, p2, p2 > 0.0));
        let off = uv - center;
        let ang = atan2(off.y, off.x);
        let shift = length(off) * intensity * 0.3;
        var r = vec3<f32>(color.r * (1.0 + shift * cos(ang)), color.g * (1.0 - shift * 0.5),
                          color.b * (1.0 + shift * cos(ang + 3.14159)));
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 4u) { // broken-tv
        let gl = select(0.5, p0, p0 > 0.0);
        let ns = select(0.3, p1, p1 > 0.0);
        let roll = select(1.0, p2, p2 > 0.0);
        var r = color;
        let rp = fract(time * roll * 0.1);
        let bd = abs(uv.y - rp);
        if (bd < 0.08) { r = mix(r, vec3<f32>(1.0) - r, (1.0 - bd / 0.08) * gl * 0.7); }
        let nz = fx_hash2(pixel + vec2<f32>(time * 100.0, 0.0));
        r = mix(r, vec3<f32>(nz), ns * 0.15);
        let gh = fx_hash11(floor(pixel.y / 3.0) * 43.758 + floor(time * 15.0) * 137.5);
        if (gh > (1.0 - gl * 0.1)) {
            let s = (gh - 0.5) * 2.0;
            r.r = mix(r.r, r.g, abs(s)); r.b = mix(r.b, r.r, abs(s) * 0.5);
        }
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 5u) { // matrix
        let gs = select(0.8, p0, p0 > 0.0);
        let sp = select(2.0, p1, p1 > 0.0);
        var r = vec3<f32>(lum * 0.2, lum * gs, lum * 0.1);
        let ch = fx_hash11(floor(pixel.x / 8.0));
        r *= 0.7 + 0.3 * (sin(pixel.y * 0.05 - time * sp * (0.5 + ch)) * 0.5 + 0.5);
        r += vec3<f32>(0.0, 0.02, 0.0);
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 6u) { // sepia
        let it = select(0.8, p0, p0 > 0.0);
        let sep = vec3<f32>(dot(color, vec3<f32>(0.393, 0.769, 0.189)),
                            dot(color, vec3<f32>(0.349, 0.686, 0.168)),
                            dot(color, vec3<f32>(0.272, 0.534, 0.131)));
        return clamp(mix(color, sep, it), vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 7u) { // pixelate (posterize + block shading)
        let bs = select(4.0, p0, p0 > 0.0);
        let levels = select(8.0, p1, p1 > 0.0);
        let bc = floor(pixel / bs) * bs + bs * 0.5;
        var r = color * (1.0 - clamp(length(pixel - bc) / (bs * 0.707), 0.0, 1.0) * 0.3);
        r = floor(r * levels + 0.5) / levels;
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 8u) { // wave (brightness)
        let amp = select(0.3, p0, p0 > 0.0);
        let freq = select(0.05, p1, p1 > 0.0);
        let sp = select(2.0, p2, p2 > 0.0);
        let b = 1.0 + sin(pixel.y * freq + time * sp) * amp
                    + sin(pixel.x * freq * 0.7 + time * sp * 0.8) * amp * 0.5;
        return clamp(color * b, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 9u) { // invert
        return mix(color, vec3<f32>(1.0) - color, select(1.0, p0, p0 > 0.0));
    } else if (index == 10u) { // night vision
        let gs = select(0.9, p0, p0 > 0.0);
        let ns = select(0.15, p1, p1 > 0.0);
        let vr = select(0.6, p2, p2 > 0.0);
        var r = vec3<f32>(lum * 0.1, lum * gs * 1.5, lum * 0.05);
        r += vec3<f32>(0.0, (fx_hash2(pixel + vec2<f32>(time * 120.0, time * 90.0)) - 0.5) * ns, 0.0);
        r *= smoothstep(vr + 0.3, vr, length(uv - vec2<f32>(0.5)));
        r *= 0.95 + 0.05 * sin(pixel.y * 2.0);
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 11u) { // vaporwave
        let it = select(0.4, p0, p0 > 0.0);
        let sp = select(0.5, p1, p1 > 0.0);
        let grad = mix(uv.x, uv.y, sin(time * sp) * 0.5 + 0.5);
        let tint = mix(vec3<f32>(1.0, 0.443, 0.808), vec3<f32>(0.004, 0.804, 0.996), grad);
        var r = mix(color, color * tint * 1.5, it);
        r *= 0.97 + 0.03 * sin(pixel.y * 1.5 + time * 2.0);
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 12u) { // thermal
        var t: vec3<f32>;
        if (lum < 0.15) { t = mix(vec3<f32>(0.0, 0.0, 0.1), vec3<f32>(0.0, 0.0, 0.8), lum / 0.15); }
        else if (lum < 0.35) { t = mix(vec3<f32>(0.0, 0.0, 0.8), vec3<f32>(0.0, 0.8, 0.2), (lum - 0.15) / 0.2); }
        else if (lum < 0.55) { t = mix(vec3<f32>(0.0, 0.8, 0.2), vec3<f32>(1.0, 1.0, 0.0), (lum - 0.35) / 0.2); }
        else if (lum < 0.8) { t = mix(vec3<f32>(1.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), (lum - 0.55) / 0.25); }
        else { t = mix(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(1.0, 1.0, 1.0), (lum - 0.8) / 0.2); }
        return clamp(mix(color, t, select(1.0, p0, p0 > 0.0)), vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 13u) { // glitch
        let it = select(0.5, p0, p0 > 0.0);
        let slices = select(20.0, p1, p1 > 0.0);
        let sp = select(5.0, p2, p2 > 0.0);
        var r = color;
        let tt = floor(time * sp);
        if (fx_hash11(tt * 43.758) > (1.0 - it * 0.5)) {
            let sh = fx_hash11(floor(uv.y * slices) * 127.1 + tt * 311.7);
            if (sh > 0.6) {
                let s = (sh - 0.6) * 2.5 * it;
                r.r = mix(r.r, r.g, s); r.b = mix(r.b, r.r, s * 0.7);
                r *= 1.0 + (sh - 0.8) * it * 3.0;
            }
            if (sh > 0.95) { r = mix(r, vec3<f32>(1.0), it * 0.5); }
        }
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 14u) { // emboss (position-hash approximation)
        let strength = select(1.0, p0, p0 > 0.0);
        let ang = select(0.785, p1, p1 > 0.0);
        let g = sin(lum * 50.0 + pixel.x * cos(ang) * 0.5 + pixel.y * sin(ang) * 0.5);
        return clamp(vec3<f32>(0.5 + g * strength * 0.5) * color * 1.5, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 15u) { // rain (additive streaks)
        let density = select(0.3, p0, p0 > 0.0);
        let sp = select(3.0, p1, p1 > 0.0);
        let streak = select(0.15, p2, p2 > 0.0);
        var r = color;
        for (var layer = 0u; layer < 3u; layer++) {
            let lf = f32(layer);
            let col = floor(uv.x * scr.x / (3.0 * (1.0 + lf * 0.5)));
            let ch = fx_hash11(col * 127.1 + lf * 311.7);
            if (ch < density) {
                let dy = fract(-uv.y + time * sp * (1.0 + lf * 0.3) * 0.1 * (0.7 + ch * 0.6) + ch * 10.0);
                if (dy < streak) {
                    r += vec3<f32>(0.4, 0.5, 0.7) * (1.0 - dy / streak) * (0.3 - lf * 0.08) * density;
                }
            }
        }
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 16u) { // matrix-rain (green rain over content)
        let density = select(0.5, p0, p0 > 0.0);
        let sp = select(1.5, p1, p1 > 0.0);
        let trail = select(15.0, p2, p2 > 0.0);
        let cs = max(cell, vec2<f32>(1.0, 1.0));
        let ccol = floor(pixel.x / cs.x);
        let crow = floor(pixel.y / cs.y);
        var r = color * 0.5;
        let rows = scr.y / cs.y;
        for (var stream = 0u; stream < 3u; stream++) {
            let sf = f32(stream);
            let ch = fx_hash11(ccol * 12.9 + sf * 71.3);
            if (ch < density) {
                let head = fract(time * sp * (0.6 + ch) * 0.1 + ch * 50.0 + sf * 17.0) * (rows + trail);
                let d = head - crow;
                if (d >= 0.0 && d < trail) {
                    let b = 1.0 - d / trail;
                    let head_glow = select(0.0, 0.6, d < 1.0);
                    r += vec3<f32>(0.1, 0.9, 0.3) * b * b + vec3<f32>(head_glow);
                }
            }
        }
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    } else if (index == 18u) { // thunderstorm (lightning flashes + darken)
        let dark = select(0.4, p3, p3 > 0.0);
        let freq = select(0.3, p2, p2 > 0.0);
        var r = color * (1.0 - dark);
        let lt = time * (0.5 + freq);
        let phase = floor(lt);
        if (fx_hash11(phase * 127.1) < freq) {
            let t = fract(lt);
            var li = 0.0;
            if (t < 0.05) { li = 1.0; }
            else if (t < 0.1) { li = 1.0 - (t - 0.05) * 15.0; }
            else if (t < 0.4) { li = 0.1 * (sin(t * 50.0) * 0.5 + 0.5) * (1.0 - (t - 0.1) / 0.3); }
            r += vec3<f32>(0.8, 0.85, 1.0) * li;
        }
        return clamp(r, vec3<f32>(0.0), vec3<f32>(1.0));
    }
    return color;
}

// ---------------------------------------------------------------------------
// Coordinate distortion
// ---------------------------------------------------------------------------
fn fx_coord_apply(index: u32, pixel: vec2<f32>, screen: vec2<f32>, time: f32,
                  mouse: vec2<f32>, cursor: vec2<f32>,
                  p0: f32, p1: f32, p2: f32, p3: f32, p4: f32, p5: f32) -> vec2<f32> {
    if (index == 0u) { return pixel; }
    let scr = max(screen, vec2<f32>(1.0, 1.0));
    let center = scr * 0.5;
    let uv = (pixel - center) / center;
    let r2 = dot(uv, uv);
    let t = time;

    if (index == 1u) { // fisheye lens — follows the mouse
        let s = select(0.5, p0, p0 > 0.0);
        let radius = select(min(scr.x, scr.y) * 0.5, p1, p1 > 0.0);
        let d = pixel - mouse;
        let dist = length(d);
        if (dist >= radius || dist < 0.001) { return pixel; }
        let rn = dist / radius;
        return mouse + d * (1.0 - s * (1.0 - rn * rn));
    } else if (index == 2u) { // magnify around the terminal cursor
        let s = select(0.5, p0, p0 > 0.0);
        let radius = select(200.0, p1, p1 > 0.0);
        let delta = pixel - cursor;
        let dist = length(delta);
        if (dist >= radius || dist < 0.001) { return pixel; }
        let tt = dist / radius;
        return cursor + delta * (1.0 - s * (1.0 - tt * tt));
    } else if (index == 3u) { // magnify around the mouse pointer
        let s = select(0.5, p0, p0 > 0.0);
        let radius = select(150.0, p1, p1 > 0.0);
        let delta = pixel - mouse;
        let dist = length(delta);
        if (dist >= radius || dist < 0.001) { return pixel; }
        let tt = dist / radius;
        return mouse + delta * (1.0 - s * (1.0 - tt * tt));
    } else if (index == 4u) { // warts (pulsing blisters, animated positions)
        let s = select(0.4, p0, p0 > 0.0);
        let count = i32(select(5.0, p1, p1 > 0.0));
        let br = select(80.0, p2, p2 > 0.0);
        var pos = pixel;
        for (var i = 0; i < count; i++) {
            let seed = f32(i) * 17.31;
            let slot = floor(t * 0.3 + seed);
            let c = vec2<f32>(fx_hash11(slot + seed) * scr.x, fx_hash11(slot + seed + 100.0) * scr.y);
            let radius = br * (0.7 + 0.3 * sin(t * 2.0 + seed * 3.0));
            let delta = pos - c;
            let dist = length(delta);
            if (dist < radius && dist > 0.001) {
                let ps = s * (0.5 + 0.5 * sin(t * 3.0 + seed * 5.0));
                pos = c + delta * (1.0 - ps * (1.0 - pow(dist / radius, 2.0)));
            }
        }
        return pos;
    } else if (index == 5u) { // wandering wart
        let s = select(0.5, p0, p0 > 0.0);
        let br = select(120.0, p1, p1 > 0.0);
        let sp = select(1.0, p2, p2 > 0.0);
        let tt = t * sp;
        let c = vec2<f32>(
            (0.5 + 0.3 * sin(tt * 0.7) + 0.15 * sin(tt * 1.3 + 2.0)) * scr.x,
            (0.5 + 0.3 * sin(tt * 0.5 + 1.0) + 0.15 * sin(tt * 1.1 + 3.0)) * scr.y);
        let radius = br * (0.8 + 0.2 * sin(tt * 2.5));
        let delta = pixel - c;
        let dist = length(delta);
        if (dist >= radius || dist < 0.001) { return pixel; }
        return c + delta * (1.0 - s * (1.0 - pow(dist / radius, 2.0)));
    } else if (index == 6u) { // barrel
        let s = select(0.3, p0, p0 != 0.0);
        return center + uv * (1.0 + s * r2) * center;
    } else if (index == 7u) { // swirl (animated)
        let s = select(3.0, p0, p0 != 0.0);
        let rad = select(1.0, p1, p1 > 0.0);
        let a = (s + 0.5 * sin(t)) * exp(-r2 / (rad * rad));
        let ca = cos(a); let sa = sin(a);
        return center + vec2<f32>(uv.x * ca - uv.y * sa, uv.x * sa + uv.y * ca) * center;
    } else if (index == 8u) { // bulge lens — follows the mouse
        let s = select(0.5, p0, p0 > 0.0);
        let radius = select(min(scr.x, scr.y) * 0.5, p1, p1 > 0.0);
        let d = pixel - mouse;
        let dist = length(d);
        if (dist >= radius || dist < 0.001) { return pixel; }
        return mouse + d * (1.0 - s * (1.0 - dist / radius));
    } else if (index == 9u) { // pinch lens — follows the mouse
        let s = select(0.5, p0, p0 > 0.0);
        let radius = select(min(scr.x, scr.y) * 0.5, p1, p1 > 0.0);
        let d = pixel - mouse;
        let dist = length(d);
        if (dist >= radius || dist < 0.001) { return pixel; }
        return mouse + d * (1.0 + s * (1.0 - dist / radius));
    } else if (index == 10u) { // jello
        let s = select(0.3, p0, p0 != 0.0);
        let freq = select(3.0, p1, p1 > 0.0);
        let sp = select(2.0, p2, p2 > 0.0);
        let tt = t * sp;
        let wx = sin(pixel.y / scr.y * freq * 6.28 + tt) * cos(pixel.x / scr.x * freq * 3.14 + tt * 0.7) * s * 30.0;
        let wy = cos(pixel.x / scr.x * freq * 6.28 + tt * 1.3) * sin(pixel.y / scr.y * freq * 3.14 + tt * 0.9) * s * 30.0;
        return pixel + vec2<f32>(wx, wy);
    } else if (index == 11u) { // heartbeat
        let s = select(0.3, p0, p0 != 0.0);
        let bpm = select(72.0, p1, p1 > 0.0);
        let period = 60.0 / bpm;
        let tt = (t % period) / period;
        let pulse = (exp(-pow(tt * 10.0, 2.0)) + exp(-pow((tt - 0.2) * 12.0, 2.0)) * 0.7) * s;
        let delta = pixel - center;
        return center + delta * (1.0 - pulse * (1.0 - length(delta) / length(center)));
    } else if (index == 12u) { // drunk
        let s = select(0.4, p0, p0 != 0.0);
        let sp = select(0.5, p1, p1 > 0.0);
        let tt = t * sp;
        let sx = sin(tt * 0.7) * 20.0 + sin(tt * 1.1 + 1.0) * 15.0 + sin(tt * 0.3 + 2.0) * 10.0;
        let sy = cos(tt * 0.5) * 15.0 + cos(tt * 0.9 + 1.5) * 10.0 + cos(tt * 0.2 + 3.0) * 8.0;
        let ef = length(pixel / scr - 0.5) * 2.0;
        return pixel + vec2<f32>(sx, sy) * s * (0.5 + ef * 0.5);
    } else if (index == 13u) { // heat haze
        let s = select(0.2, p0, p0 != 0.0);
        let freq = select(20.0, p1, p1 > 0.0);
        let sp = select(3.0, p2, p2 > 0.0);
        let tt = t * sp;
        let hf = 1.0 - pixel.y / scr.y;
        let sx = sin(pixel.y * freq * 0.1 + tt) * cos(pixel.y * freq * 0.07 + tt * 1.3) * s * 8.0 * hf;
        let sy = sin(pixel.x * freq * 0.05 + tt * 0.8) * s * 3.0 * hf;
        return pixel + vec2<f32>(sx, sy);
    } else if (index == 14u) { // underwater
        let s = select(0.3, p0, p0 != 0.0);
        let freq = select(4.0, p1, p1 > 0.0);
        let sp = select(1.5, p2, p2 > 0.0);
        let tt = t * sp;
        let u = pixel / scr;
        let w1 = sin(u.x * freq * 6.28 + tt) * cos(u.y * freq * 4.0 + tt * 0.7);
        let w2 = sin(u.y * freq * 5.0 - tt * 1.1) * cos(u.x * freq * 3.0 + tt * 0.5);
        let w3 = sin((u.x + u.y) * freq * 4.0 + tt * 0.8);
        return pixel + vec2<f32>((w1 + w2 * 0.5) * s * 20.0, (w2 + w3 * 0.5) * s * 20.0);
    } else if (index == 15u) { // earthquake
        let s = select(0.5, p0, p0 != 0.0);
        let freq = select(30.0, p1, p1 > 0.0);
        let tt = t * freq;
        let fl = floor(tt);
        let sm = smoothstep(0.0, 1.0, fract(tt));
        let sx = mix(fx_hash11(fl) * 2.0 - 1.0, fx_hash11(fl + 1.0) * 2.0 - 1.0, sm) * s * 15.0;
        let sy = mix(fx_hash11(fl + 100.0) * 2.0 - 1.0, fx_hash11(fl + 101.0) * 2.0 - 1.0, sm) * s * 15.0;
        return pixel + vec2<f32>(sx, sy);
    } else if (index == 16u) { // scanline offset
        let s = select(0.5, p0, p0 != 0.0);
        let sp = select(5.0, p1, p1 > 0.0);
        let row = floor(pixel.y / 4.0);
        let off = (fx_hash11(row * 17.31 + floor(t * sp)) * 2.0 - 1.0) * s * 30.0 * fx_hash11(row * 7.13);
        return vec2<f32>(pixel.x + off, pixel.y);
    } else if (index == 17u) { // vhs tear
        let s = select(0.6, p0, p0 != 0.0);
        let th = select(50.0, p1, p1 > 0.0);
        let sp = select(2.0, p2, p2 > 0.0);
        let tt = t * sp;
        let ty = (sin(tt * 0.7) * 0.5 + 0.5) * scr.y;
        let d = abs(pixel.y - ty);
        if (d > th) { return pixel; }
        let tf = 1.0 - d / th;
        let off = sin(tt * 10.0 + pixel.y * 0.1) * s * 50.0 * tf
                + (fx_hash11(pixel.y + tt) * 2.0 - 1.0) * 10.0 * tf * s;
        return vec2<f32>(pixel.x + off, pixel.y);
    } else if (index == 18u) { // melt
        let s = select(0.5, p0, p0 != 0.0);
        let df = select(8.0, p1, p1 > 0.0);
        let sp = select(1.0, p2, p2 > 0.0);
        let tt = t * sp;
        let cs = floor(pixel.x / scr.x * df);
        let hf = pixel.y / scr.y;
        let drip = sin(tt * (0.5 + fx_hash11(cs + 50.0)) + fx_hash11(cs) * 6.28) * 0.5 + 0.5;
        let wob = sin(pixel.y * 0.1 + tt * 2.0) * s * 5.0 * hf;
        return vec2<f32>(pixel.x + wob, pixel.y - drip * s * 40.0 * hf);
    } else if (index == 19u) { // wave
        let s = select(0.4, p0, p0 != 0.0);
        let freq = select(3.0, p1, p1 > 0.0);
        let sp = select(2.0, p2, p2 > 0.0);
        let tt = t * sp;
        if (p3 > 0.5) {
            return vec2<f32>(pixel.x + sin(pixel.y / scr.y * freq * 6.28 + tt) * s * 30.0, pixel.y);
        }
        return vec2<f32>(pixel.x, pixel.y + sin(pixel.x / scr.x * freq * 6.28 + tt) * s * 30.0);
    } else if (index == 20u) { // funhouse
        let s = select(0.4, p0, p0 != 0.0);
        let cx = select(3.0, p1, p1 > 0.0);
        let cy = select(2.0, p2, p2 > 0.0);
        let u = pixel / scr;
        let bx = sin(u.x * cx * 6.28) * sin(u.y * cy * 6.28);
        let by = cos(u.x * cx * 6.28) * cos(u.y * cy * 6.28);
        let tt = t * 0.5;
        return pixel + vec2<f32>(bx * cos(tt) + by * sin(tt) * 0.3, by * cos(tt) - bx * sin(tt) * 0.3) * s * 30.0;
    } else if (index == 21u) { // twist
        let s = select(0.5, p0, p0 != 0.0);
        let delta = pixel - center;
        let nd = length(delta) / length(center);
        let a = nd * nd * s * 3.14159 + p1 * t;
        let ca = cos(a); let sa = sin(a);
        return center + vec2<f32>(delta.x * ca - delta.y * sa, delta.x * sa + delta.y * ca);
    }
    return pixel;
}
