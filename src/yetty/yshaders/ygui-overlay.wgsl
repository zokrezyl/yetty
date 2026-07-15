// YGui Overlay Shader
// Renders ydraw SDF primitives as a transparent fullscreen overlay.
// Uses the same SDF evaluation as the card ydraw shader but with RGBA output
// and alpha blending for transparent overlay rendering.
//
// Bind group layout:
//   @group(0) @binding(0): uniform  OverlayUniforms
//   @group(0) @binding(1): storage  cardStorage (prim data, compact format)
//   @group(0) @binding(2): storage  gridData (spatial hash grid, u32)
//   @group(0) @binding(3): storage  glyphBuffer (YDrawGlyph array)
//   @group(0) @binding(4): texture  fontTexture (shared MSDF atlas)
//   @group(0) @binding(5): sampler  fontSampler
//   @group(0) @binding(6): storage  glyphMetadata (font glyph UV metadata)

struct OverlayUniforms {
    screenWidth: f32,
    screenHeight: f32,
    sceneMinX: f32,
    sceneMinY: f32,
    sceneMaxX: f32,
    sceneMaxY: f32,
    gridWidth: u32,
    gridHeight: u32,
    cellSizeX: f32,
    cellSizeY: f32,
    primCount: u32,
    glyphCount: u32,
    pixelRange: f32,
    _pad: f32,  // padding to maintain alignment
};

// Glyph metadata (40 bytes per glyph, matches C++ GlyphMetadataGPU)
struct GlyphMetadata {
    uvMin: vec2<f32>,
    uvMax: vec2<f32>,
    size: vec2<f32>,
    bearing: vec2<f32>,
    advance: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> overlay: OverlayUniforms;
@group(0) @binding(1) var<storage, read> cardStorage: array<f32>;
@group(0) @binding(2) var<storage, read> gridData: array<u32>;
@group(0) @binding(3) var<storage, read> glyphBuffer: array<f32>;
@group(0) @binding(4) var fontTexture: texture_2d<f32>;
@group(0) @binding(5) var fontSampler: sampler;
@group(0) @binding(6) var<storage, read> glyphMetadata: array<GlyphMetadata>;

// === SDF type constants (subset used by ygui widgets) ===
// INJECT_SDF_TYPES

// === SDF distance functions ===
// INJECT_DISTFUNCTIONS

// === Fullscreen triangle vertex shader ===

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var out: VertexOutput;
    // Fullscreen triangle: 3 vertices cover the entire screen
    let x = f32(i32(vertexIndex & 1u)) * 4.0 - 1.0;
    let y = f32(i32(vertexIndex >> 1u)) * 4.0 - 1.0;
    out.position = vec4<f32>(x, y, 0.0, 1.0);
    out.uv = vec2<f32>((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return out;
}

// === MSDF text rendering ===

fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

fn unpackColor(packed: u32) -> vec3<f32> {
    return vec3<f32>(
        f32(packed & 0xFFu) / 255.0,
        f32((packed >> 8u) & 0xFFu) / 255.0,
        f32((packed >> 16u) & 0xFFu) / 255.0
    );
}

fn unpackAlpha(packed: u32) -> f32 {
    return f32((packed >> 24u) & 0xFFu) / 255.0;
}

// Safety cap for entries per cell
const YDRAW_MAX_ENTRIES_PER_CELL: u32 = 4096u;
const GLYPH_BIT: u32 = 0x80000000u;
const INDEX_MASK: u32 = 0x7FFFFFFFu;

// === Color Wheel ===

fn hsv2rgb(hsv: vec3<f32>) -> vec3<f32> {
    let h = hsv.x * 6.0;
    let s = hsv.y;
    let v = hsv.z;
    let i = floor(h);
    let f = h - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));
    let ii = i32(i) % 6;
    if (ii == 0) { return vec3<f32>(v, t, p); }
    if (ii == 1) { return vec3<f32>(q, v, p); }
    if (ii == 2) { return vec3<f32>(p, v, t); }
    if (ii == 3) { return vec3<f32>(p, q, v); }
    if (ii == 4) { return vec3<f32>(t, p, v); }
    return vec3<f32>(v, p, q);
}

fn renderColorWheel(p: vec2<f32>, center: vec2<f32>, outerR: f32, innerR: f32,
                    hue: f32, sat: f32, val: f32, indicatorSize: f32) -> vec4<f32> {
    let d = p - center;
    let dist = length(d);
    let angle = atan2(d.y, d.x);
    let normAngle = (angle + 3.14159265) / (2.0 * 3.14159265);
    let aa = 1.5;
    let ringWidth = outerR - innerR;
    if (dist >= innerR && dist <= outerR) {
        let color = hsv2rgb(vec3<f32>(normAngle, 1.0, 1.0));
        let ringDist = min(dist - innerR, outerR - dist);
        let alpha = clamp(ringDist / aa + 0.5, 0.0, 1.0);
        let hueAngle = hue * 2.0 * 3.14159265 - 3.14159265;
        let indPos = center + vec2<f32>(cos(hueAngle), sin(hueAngle)) * (innerR + ringWidth * 0.5);
        let indDist = length(p - indPos);
        if (indDist < indicatorSize) {
            if (indDist < indicatorSize - 2.0) { return vec4<f32>(1.0, 1.0, 1.0, 1.0); }
            return vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }
        return vec4<f32>(color, alpha);
    }
    let triR = innerR * 0.85;
    let hueAng = hue * 2.0 * 3.14159265;
    let v0 = center + vec2<f32>(cos(hueAng), sin(hueAng)) * triR;
    let v1 = center + vec2<f32>(cos(hueAng + 2.094395), sin(hueAng + 2.094395)) * triR;
    let v2 = center + vec2<f32>(cos(hueAng + 4.18879), sin(hueAng + 4.18879)) * triR;
    let d0 = v1 - v0; let d1 = v2 - v0; let d2 = p - v0;
    let dot00 = dot(d0, d0); let dot01 = dot(d0, d1);
    let dot02 = dot(d0, d2); let dot11 = dot(d1, d1); let dot12 = dot(d1, d2);
    let invD = 1.0 / (dot00 * dot11 - dot01 * dot01);
    let u = (dot11 * dot02 - dot01 * dot12) * invD;
    let bv = (dot00 * dot12 - dot01 * dot02) * invD;
    if (u >= 0.0 && bv >= 0.0 && u + bv <= 1.0) {
        let svS = clamp(1.0 - bv - u * 0.5, 0.0, 1.0);
        let svV = clamp(1.0 - bv, 0.0, 1.0);
        let color = hsv2rgb(vec3<f32>(hue, svS, svV));
        let edge = min(u, min(bv, 1.0 - u - bv)) * triR * 2.0;
        let alpha = clamp(edge / aa + 0.5, 0.0, 1.0);
        let selU = (1.0 - val) * 2.0 / 3.0 + val * (1.0 - sat) / 3.0;
        let selV = 1.0 - val;
        let selPos = v0 + d0 * selU + d1 * selV;
        let selDist = length(p - selPos);
        if (selDist < indicatorSize) {
            if (selDist < indicatorSize - 2.0) { return vec4<f32>(1.0, 1.0, 1.0, 1.0); }
            return vec4<f32>(0.0, 0.0, 0.0, 1.0);
        }
        return vec4<f32>(color, alpha);
    }
    return vec4<f32>(0.0, 0.0, 0.0, 0.0);
}

// === Fragment shader ===

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let screenPos = vec2<f32>(in.uv.x * overlay.screenWidth, in.uv.y * overlay.screenHeight);

    // Map screen position to scene coordinates (1:1 mapping for overlay)
    let scenePos = screenPos;

    // Early exit if outside scene bounds or no grid
    if (overlay.gridWidth == 0u || overlay.gridHeight == 0u) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }

    let invCellSizeX = select(0.0, 1.0 / overlay.cellSizeX, overlay.cellSizeX > 0.0);
    let invCellSizeY = select(0.0, 1.0 / overlay.cellSizeY, overlay.cellSizeY > 0.0);
    let contentMinX = overlay.sceneMinX;
    let contentMinY = overlay.sceneMinY;

    // O(1) grid lookup
    let cellX = u32(clamp((scenePos.x - contentMinX) * invCellSizeX, 0.0, f32(overlay.gridWidth - 1u)));
    let cellY = u32(clamp((scenePos.y - contentMinY) * invCellSizeY, 0.0, f32(overlay.gridHeight - 1u)));
    let cellIndex = cellY * overlay.gridWidth + cellX;

    let packedStart = gridData[cellIndex];
    let cellEntryCount = gridData[packedStart];
    let loopCount = min(cellEntryCount, YDRAW_MAX_ENTRIES_PER_CELL);

    var resultColor = vec3<f32>(0.0, 0.0, 0.0);
    var resultAlpha = 0.0;
    let screenScale = 1.0;  // 1:1 pixel mapping for overlay
    let primDataBase = overlay.primCount;  // prim data starts after offset table

    for (var i = 0u; i < loopCount; i++) {
        let rawIdx = gridData[packedStart + 1u + i];

        if ((rawIdx & GLYPH_BIT) != 0u) {
            // TEXT GLYPH (MSDF)
            let gi = rawIdx & INDEX_MASK;
            let gOffset = gi * 5u;

            let gx = glyphBuffer[gOffset + 0u];
            let gy = glyphBuffer[gOffset + 1u];
            let whPacked = bitcast<u32>(glyphBuffer[gOffset + 2u]);
            let gw = unpack2x16float(whPacked).x;
            let gh = unpack2x16float(whPacked).y;
            let glfPacked = bitcast<u32>(glyphBuffer[gOffset + 3u]);
            let gIdx = glfPacked & 0xFFFFu;
            let gColorPacked = bitcast<u32>(glyphBuffer[gOffset + 4u]);

            if (scenePos.x >= gx && scenePos.x < gx + gw &&
                scenePos.y >= gy && scenePos.y < gy + gh) {

                let glyphUV = vec2<f32>(
                    (scenePos.x - gx) / gw,
                    (scenePos.y - gy) / gh
                );
                let gColor = unpackColor(gColorPacked);

                // Shared atlas glyph - use MSDF rendering
                let glyph = glyphMetadata[gIdx];
                let uv = mix(glyph.uvMin, glyph.uvMax, glyphUV);
                let msdf = textureSampleLevel(fontTexture, fontSampler, uv, 0.0);
                let sd = median(msdf.r, msdf.g, msdf.b);
                let pxDist = overlay.pixelRange * (sd - 0.5);
                let alpha = clamp(pxDist * screenScale + 0.5, 0.0, 1.0);
                if (alpha > 0.01) {
                    resultColor = mix(resultColor, gColor, alpha);
                    resultAlpha = max(resultAlpha, alpha);
                }
            }
        } else {
            // SDF PRIMITIVE
            let primOff = primDataBase + rawIdx;
            let primType = bitcast<u32>(cardStorage[primOff + 0u]);

            // Color wheel
            if (primType == SDF_COLOR_WHEEL) {
                let cwCenter = vec2<f32>(cardStorage[primOff + 2u], cardStorage[primOff + 3u]);
                let cwOuterR = cardStorage[primOff + 4u];
                let cwInnerR = cardStorage[primOff + 5u];
                let cwHue = cardStorage[primOff + 6u];
                let cwSat = cardStorage[primOff + 7u];
                let cwVal = cardStorage[primOff + 8u];
                let cwIndicator = cardStorage[primOff + 9u];
                let cwColor = renderColorWheel(scenePos, cwCenter, cwOuterR, cwInnerR,
                                               cwHue, cwSat, cwVal, cwIndicator);
                if (cwColor.a > 0.01) {
                    resultColor = mix(resultColor, cwColor.rgb, cwColor.a);
                    resultAlpha = max(resultAlpha, cwColor.a);
                }
                continue;
            }

            // Regular SDF primitive
            let d = evaluateYdrawSDF(primOff, scenePos);

            let colors = primColors(primOff);
            let fillColorPacked = colors.x;
            // Analytic AA: scene == pixel space here, so a fixed one-pixel
            // coverage ramp (half a pixel to each side of the boundary)
            // equals screen-space fwidth AA — which WGSL forbids inside
            // this non-uniform loop.
            // Coverage is gamma-corrected (^(1/2.2)) because compositing
            // happens on sRGB-encoded values; keeps thin light-on-dark
            // fringes at their linear-light brightness.
            if (d < 0.5 && fillColorPacked != 0u) {
                let fillColor = unpackColor(fillColorPacked);
                let fillAlpha = unpackAlpha(fillColorPacked);
                let coverage = clamp(0.5 - d, 0.0, 1.0);
                let alpha = pow(coverage, 1.0 / 2.2) * fillAlpha;
                resultColor = mix(resultColor, fillColor, alpha);
                resultAlpha = max(resultAlpha, alpha);
            }

            // Strokes thinner than 1px draw as a 1px band dimmed by the
            // requested width, so hairlines stay visible (and uniform) at
            // any subpixel position.
            let strokeColorPacked = colors.y;
            let strokeWidth = primStrokeWidth(primOff);
            if (strokeWidth > 0.0 && strokeColorPacked != 0u) {
                let effectiveStrokeWidth = max(strokeWidth, 1.0);
                let strokeDist = abs(d) - effectiveStrokeWidth * 0.5;
                if (strokeDist < 0.5) {
                    let strokeColor = unpackColor(strokeColorPacked);
                    let strokeAlpha = unpackAlpha(strokeColorPacked);
                    let coverage = clamp(0.5 - strokeDist, 0.0, 1.0) * min(strokeWidth, 1.0);
                    let alpha = pow(coverage, 1.0 / 2.2) * strokeAlpha;
                    resultColor = mix(resultColor, strokeColor, alpha);
                    resultAlpha = max(resultAlpha, alpha);
                }
            }
        }
    }

    return vec4<f32>(resultColor, resultAlpha);
}
