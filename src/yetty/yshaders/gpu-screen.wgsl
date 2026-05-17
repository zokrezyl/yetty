// Shared uniforms (managed by Yetty, group 0)
struct SharedUniforms {
    time: f32,           // 4 bytes - animation time
    deltaTime: f32,      // 4 bytes
    screenWidth: f32,    // 4 bytes
    screenHeight: f32,   // 4 bytes
    mouseX: f32,         // 4 bytes
    mouseY: f32,         // 4 bytes
    lastChar: u32,       // 4 bytes - last typed character glyph index
    lastCharTime: f32,   // 4 bytes - time when last character was typed
};

// Grid uniforms (managed by renderer, group 1)
struct GridUniforms {
    projection: mat4x4<f32>,   // 64 bytes
    screenSize: vec2<f32>,     // 8 bytes
    cellSize: vec2<f32>,       // 8 bytes
    gridSize: vec2<f32>,       // 8 bytes (cols, rows)
    pixelRange: f32,           // 4 bytes
    scale: f32,                // 4 bytes
    cursorPos: vec2<f32>,      // 8 bytes (col, row)
    cursorVisible: f32,        // 4 bytes
    cursorShape: f32,          // 4 bytes (1=block, 2=underline, 3=bar)
    viewportOrigin: vec2<f32>, // 8 bytes - viewport origin in framebuffer
    cursorBlink: f32,          // 4 bytes (0=no blink, 1=blink)
    hasSelection: f32,         // 4 bytes (0=no, 1=yes)
    selStart: vec2<f32>,       // 8 bytes (col, row)
    selEnd: vec2<f32>,         // 8 bytes (col, row)
    // Effects (per gpu-screen instance)
    preEffectIndex: u32,       // 4 bytes (0 = no effect)
    postEffectIndex: u32,      // 4 bytes (0 = no effect)
    preEffectP0: f32,          // 4 bytes - pre-effect params
    preEffectP1: f32,          // 4 bytes
    preEffectP2: f32,          // 4 bytes
    preEffectP3: f32,          // 4 bytes
    preEffectP4: f32,          // 4 bytes
    preEffectP5: f32,          // 4 bytes
    postEffectP0: f32,         // 4 bytes - post-effect params
    postEffectP1: f32,         // 4 bytes
    postEffectP2: f32,         // 4 bytes
    postEffectP3: f32,         // 4 bytes
    postEffectP4: f32,         // 4 bytes
    postEffectP5: f32,         // 4 bytes
    defaultFg: u32,            // 4 bytes - packed RGBA like cell.fg
    defaultBg: u32,            // 4 bytes - packed RGB like cell.bg
    spaceGlyph: u32,           // 4 bytes - glyph index for space character
    // Coordinate distortion effects (no prefix - just "effect")
    effectIndex: u32,          // 4 bytes (0 = no effect)
    effectP0: f32,             // 4 bytes
    effectP1: f32,             // 4 bytes
    effectP2: f32,             // 4 bytes
    effectP3: f32,             // 4 bytes
    effectP4: f32,             // 4 bytes
    effectP5: f32,             // 4 bytes
    // Visual Zoom (shader-based magnification)
    visualZoomScale: f32,      // 4 bytes: 1.0 = off, >1.0 = zoomed
    visualZoomOffsetX: f32,    // 4 bytes: pan X in pixels
    visualZoomOffsetY: f32,    // 4 bytes: pan Y in pixels
    // YDraw overlay painters (full-screen layers)
    ydrawScrollingSlot: u32,  // 4 bytes: slot index (0 = disabled)
    ydrawOverlaySlot: u32,    // 4 bytes: slot index (0 = disabled)
    _pad0: f32,                // 4 bytes: alignment padding
    _pad1: f32,                // 4 bytes: alignment padding
    _pad2: f32,                // 4 bytes: alignment padding for 16-byte
};  // Total: 272 bytes (16-byte aligned)

// Glyph metadata (40 bytes per glyph, matches C++ GlyphMetadataGPU)
struct GlyphMetadata {
    uvMin: vec2<f32>,      // 8 bytes
    uvMax: vec2<f32>,      // 8 bytes
    size: vec2<f32>,       // 8 bytes (glyph size in pixels)
    bearing: vec2<f32>,    // 8 bytes (glyph offset from baseline)
    advance: f32,          // 4 bytes
    _pad: f32,             // 4 bytes
};

// Emoji glyph metadata (32 bytes per emoji, matches C++ EmojiGlyphMetadata)
struct EmojiGlyphMetadata {
    uvMin: vec2<f32>,      // 8 bytes
    uvMax: vec2<f32>,      // 8 bytes
    size: vec2<f32>,       // 8 bytes (glyph size in pixels at atlas resolution)
    _pad: vec2<f32>,       // 8 bytes padding
};

// Raster glyph UV (8 bytes per glyph) - minimal for monospace terminal
// uvMax = uvMin + glyphSizeUV (computed from uniform)
struct RasterGlyphUV {
    uv: vec2<f32>,         // 8 bytes (UV top-left corner)
};

// Cell structure - matches C++ Cell struct (12 bytes)
// Uploaded directly from CPU, zero-copy
struct Cell {
    glyph: u32,    // 4 bytes - glyph index (room for shader-based glyphs)
    fg: u32,       // 4 bytes - packed: fgR | (fgG << 8) | (fgB << 16) | (alpha << 24)
    bg: u32,       // 4 bytes - packed: bgR | (bgG << 8) | (bgB << 16) | (style << 24)
};

// Shared bindings (group 0 - managed by Yetty)
@group(0) @binding(0) var<uniform> globals: SharedUniforms;
@group(0) @binding(1) var<storage, read> cardMetadata: array<u32>;
@group(0) @binding(2) var<storage, read> cardStorage: array<f32>;
// Card image atlas texture
@group(0) @binding(3) var cardImageAtlas: texture_2d<f32>;
@group(0) @binding(4) var cardImageSampler: sampler;

// Grid bindings (group 1 - managed by renderer)
@group(1) @binding(0) var<uniform> grid: GridUniforms;
@group(1) @binding(1) var fontTexture: texture_2d<f32>;
@group(1) @binding(2) var fontSampler: sampler;
@group(1) @binding(3) var<storage, read> glyphMetadata: array<GlyphMetadata>;
@group(1) @binding(4) var<storage, read> cellBuffer: array<Cell>;  // Cell buffer - zero copy from CPU

// Emoji bindings (group 1 continued)
@group(1) @binding(5) var emojiTexture: texture_2d<f32>;       // RGBA8Unorm - color emoji atlas
@group(1) @binding(6) var emojiSampler: sampler;
@group(1) @binding(7) var<storage, read> emojiMetadata: array<EmojiGlyphMetadata>;

// Vector SDF font bindings (group 1 continued)
// Glyph buffer: [header:u32][curve0.p0][curve0.p1][curve0.p2][curve1...]
// Header: curveCount[16] | flags[16]
// Each point: x[16] | y[16] packed as u32, normalized to [0,1]
@group(1) @binding(8) var<storage, read> vectorGlyphBuffer: array<u32>;
// Offset table: codepoint -> offset in vectorGlyphBuffer (0xFFFFFFFF = not present)
@group(1) @binding(9) var<storage, read> vectorOffsetTable: array<u32>;

// Vector Coverage font bindings (group 1 continued)
// Same buffer format as SDF, but rendered with coverage-based anti-aliasing
@group(1) @binding(10) var<storage, read> coverageGlyphBuffer: array<u32>;
@group(1) @binding(11) var<storage, read> coverageOffsetTable: array<u32>;

// Raster font bindings (group 1 continued)
// R8Unorm texture atlas with grayscale glyph bitmaps
@group(1) @binding(12) var rasterTexture: texture_2d<f32>;
@group(1) @binding(13) var rasterSampler: sampler;
@group(1) @binding(14) var<storage, read> rasterMetadata: array<RasterGlyphUV>;

// Attribute bit masks (matches CellAttrs in grid.h)
const ATTR_BOLD: u32 = 0x01u;           // Bit 0
const ATTR_ITALIC: u32 = 0x02u;         // Bit 1
const ATTR_UNDERLINE_MASK: u32 = 0x0Cu; // Bits 2-3 (0=none, 1=single, 2=double, 3=curly)
const ATTR_STRIKETHROUGH: u32 = 0x10u;  // Bit 4
const ATTR_FONT_TYPE_MASK: u32 = 0xE0u; // Bits 5-7 - font type

// Font type constants (bits 5-7 of style byte)
const FONT_TYPE_MSDF: u32 = 0u;     // Default text rendering
const FONT_TYPE_BITMAP: u32 = 1u;   // Bitmap fonts (emoji, color fonts)
const FONT_TYPE_SHADER: u32 = 2u;   // Single-cell shader glyphs
const FONT_TYPE_CARD: u32 = 3u;     // Multi-cell card glyphs
const FONT_TYPE_VECTOR: u32 = 4u;   // Vector font (SDF curves)
const FONT_TYPE_COVERAGE: u32 = 5u; // Vector font (coverage-based)
const FONT_TYPE_RASTER: u32 = 6u;   // Raster font (texture atlas)

// Legacy alias for backward compatibility
const ATTR_EMOJI: u32 = 0x20u;      // Bit 5 - same position as FONT_TYPE_BITMAP

// Shader glyph constants (matches grid.h)
// Uses Unicode Plane 16 PUA-B: U+100000 - U+10FFFD (codepoint = glyph index)
const CARD_GLYPH_BASE: u32 = 0x100000u;     // 1048576 decimal - card range start
const CARD_GLYPH_END: u32 = 0x100FFFu;      // 1052671 decimal - card range end
const SHADER_GLYPH_BASE: u32 = 0x101000u;   // 1052672 decimal - shader glyph range start
const SHADER_GLYPH_END: u32 = 0x10FFFDu;    // 1114109 decimal - shader glyph range end
const SHADER_GLYPH_START: u32 = 0x100000u;  // Legacy alias

// Vector SDF font glyph range - Plane 15 PUA-A: U+F0000 - U+F0FFF
const VECTOR_GLYPH_BASE: u32 = 0xF0000u;    // Vector SDF font range start
const VECTOR_GLYPH_END: u32 = 0xF0FFFu;     // Vector SDF font range end

// Vector Coverage font glyph range - Plane 15 PUA-A: U+F1000 - U+F1FFF
const COVERAGE_GLYPH_BASE: u32 = 0xF1000u;  // Vector coverage font range start
const COVERAGE_GLYPH_END: u32 = 0xF1FFFu;   // Vector coverage font range end

// Raster font glyph range - Plane 15 PUA-A: U+F2000 - U+F2FFF
const RASTER_GLYPH_BASE: u32 = 0xF2000u;    // Raster font range start
const RASTER_GLYPH_END: u32 = 0xF2FFFu;     // Raster font range end

// Vertex input/output
struct VertexInput {
    @location(0) position: vec2<f32>,
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    // Pass through clip-space coordinates directly
    output.position = vec4<f32>(input.position, 0.0, 1.0);
    return output;
}

// Note: median() function moved to lib/text.wgsl

// Helper function to check if pixel is in underline region
fn isInUnderline(localY: f32, cellHeight: f32, underlineType: u32, scale: f32) -> bool {
    // Underline position: slightly below baseline (baseline is at 80%)
    let baseline = cellHeight * 0.8;
    let underlineY = baseline + 2.0 * scale;
    let lineThickness = max(1.0, 1.5 * scale);

    if (underlineType == 1u) {
        // Single underline
        return localY >= underlineY && localY < underlineY + lineThickness;
    } else if (underlineType == 2u) {
        // Double underline
        let gap = lineThickness * 1.5;
        let inFirst = localY >= underlineY && localY < underlineY + lineThickness;
        let inSecond = localY >= underlineY + gap && localY < underlineY + gap + lineThickness;
        return inFirst || inSecond;
    } else if (underlineType == 3u) {
        // Curly/wavy underline (simplified sine wave)
        let waveHeight = lineThickness * 1.5;
        let waveFreq = 0.15;  // Wave frequency
        // We can't access localX here easily, so we'll handle curly differently
        // For now, treat curly as a thicker single underline (TODO: proper wavy)
        return localY >= underlineY && localY < underlineY + lineThickness * 2.0;
    }
    return false;
}

// Helper for curly underline with X position
fn isInCurlyUnderline(localX: f32, localY: f32, cellWidth: f32, cellHeight: f32, scale: f32) -> bool {
    let baseline = cellHeight * 0.8;
    let underlineY = baseline + 2.0 * scale;
    let lineThickness = max(1.0, 1.5 * scale);
    let waveHeight = lineThickness * 1.2;
    let waveFreq = 3.14159 * 2.0 / (cellWidth * 0.5);  // One full wave per half cell

    // Calculate wave offset
    let waveOffset = sin(localX * waveFreq) * waveHeight;
    let waveCenterY = underlineY + waveHeight;  // Center the wave

    return localY >= waveCenterY + waveOffset - lineThickness * 0.5 &&
           localY < waveCenterY + waveOffset + lineThickness * 0.5;
}

// Helper function to check if pixel is in strikethrough region
fn isInStrikethrough(localY: f32, cellHeight: f32, scale: f32) -> bool {
    // Strikethrough at middle of cell (approximately 45% from top for x-height)
    let strikeY = cellHeight * 0.45;
    let lineThickness = max(1.0, 1.5 * scale);
    return localY >= strikeY && localY < strikeY + lineThickness;
}

// Check if glyph index is a shader glyph (in Plane 16 PUA-B range)
fn isShaderGlyph(glyphIndex: u32) -> bool {
    return glyphIndex >= SHADER_GLYPH_START && glyphIndex <= SHADER_GLYPH_END;
}

// Check if glyph index is a card glyph
fn isCardGlyph(glyphIndex: u32) -> bool {
    return glyphIndex >= CARD_GLYPH_BASE && glyphIndex <= CARD_GLYPH_END;
}

// Check if glyph is any procedural glyph (card or shader)
fn isProceduralGlyph(glyphIndex: u32) -> bool {
    return glyphIndex >= CARD_GLYPH_BASE && glyphIndex <= SHADER_GLYPH_END;
}

// Check if glyph is a vector SDF font glyph (Plane 15 PUA-A range: U+F0000-U+F0FFF)
fn isVectorGlyph(glyphIndex: u32) -> bool {
    return glyphIndex >= VECTOR_GLYPH_BASE && glyphIndex <= VECTOR_GLYPH_END;
}

// Check if glyph is a vector coverage font glyph (Plane 15 PUA-A range: U+F1000-U+F1FFF)
fn isCoverageGlyph(glyphIndex: u32) -> bool {
    return glyphIndex >= COVERAGE_GLYPH_BASE && glyphIndex <= COVERAGE_GLYPH_END;
}

// Check if glyph is a raster font glyph (Plane 15 PUA-A range: U+F2000-U+F2FFF)
fn isRasterGlyph(glyphIndex: u32) -> bool {
    return glyphIndex >= RASTER_GLYPH_BASE && glyphIndex <= RASTER_GLYPH_END;
}

// ==== VECTOR FONT SDF RENDERING ====

// Unpack a normalized coordinate from u32 (x in high 16 bits, y in low 16 bits)
fn unpackPoint(packed: u32) -> vec2<f32> {
    let x = f32((packed >> 16u) & 0xFFFFu) / 65535.0;
    let y = f32(packed & 0xFFFFu) / 65535.0;
    return vec2<f32>(x, y);
}

// ==== SDF BEZIER RENDERING ====

// Unsigned distance to a quadratic Bezier curve (Inigo Quilez)
fn sdQuadraticBezier(pos: vec2<f32>, p0: vec2<f32>, p1: vec2<f32>, p2: vec2<f32>) -> f32 {
    let a = p1 - p0;
    let b = p0 - 2.0 * p1 + p2;
    let c = a * 2.0;
    let d = p0 - pos;

    let kk = 1.0 / dot(b, b);
    let kx = kk * dot(a, b);
    let ky = kk * (2.0 * dot(a, a) + dot(d, b)) / 3.0;
    let kz = kk * dot(d, a);

    let p = ky - kx * kx;
    let q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
    let p3 = p * p * p;
    let h = q * q + 4.0 * p3;

    var res: f32;
    if (h >= 0.0) {
        let sqrtH = sqrt(h);
        let x = (vec2<f32>(sqrtH, -sqrtH) - q) / 2.0;
        let uv = sign(x) * pow(abs(x), vec2<f32>(1.0 / 3.0));
        let t = clamp(uv.x + uv.y - kx, 0.0, 1.0);
        let qpos = d + (c + b * t) * t;
        res = dot(qpos, qpos);
    } else {
        let z = sqrt(-p);
        let v = acos(q / (p * z * 2.0)) / 3.0;
        let m = cos(v);
        let n = sin(v) * 1.732050808;
        let t0 = clamp((m + m) * z - kx, 0.0, 1.0);
        let t1 = clamp((-n - m) * z - kx, 0.0, 1.0);
        let t2 = clamp((n - m) * z - kx, 0.0, 1.0);
        let qpos0 = d + (c + b * t0) * t0;
        let qpos1 = d + (c + b * t1) * t1;
        let qpos2 = d + (c + b * t2) * t2;
        res = min(min(dot(qpos0, qpos0), dot(qpos1, qpos1)), dot(qpos2, qpos2));
    }
    return sqrt(res);
}

// Ray-cast winding number for inside/outside test
fn windingNumber(glyphIndex: u32, pos: vec2<f32>) -> i32 {
    let codepoint = glyphIndex - VECTOR_GLYPH_BASE;
    let offset = vectorOffsetTable[codepoint];
    if (offset == 0xFFFFFFFFu) { return 0; }

    let header = vectorGlyphBuffer[offset];
    let curveCount = (header >> 16u) & 0xFFFFu;
    if (curveCount == 0u) { return 0; }

    var crossings = 0;
    let curveBase = offset + 1u;

    for (var i = 0u; i < curveCount; i++) {
        let curveOffset = curveBase + i * 3u;
        let p0 = unpackPoint(vectorGlyphBuffer[curveOffset]);
        let p1 = unpackPoint(vectorGlyphBuffer[curveOffset + 1u]);
        let p2 = unpackPoint(vectorGlyphBuffer[curveOffset + 2u]);

        // Solve for t where curve.y = pos.y
        let y0 = p0.y - pos.y;
        let y1 = p1.y - pos.y;
        let y2 = p2.y - pos.y;

        let aa = y0 - 2.0 * y1 + y2;
        let bb = 2.0 * (y1 - y0);
        let cc = y0;

        if (abs(aa) < 1e-6) {
            // Linear case
            if (abs(bb) > 1e-6) {
                let t = -cc / bb;
                if (t >= 0.0 && t < 1.0) {
                    let mt = 1.0 - t;
                    let x = mt * mt * p0.x + 2.0 * mt * t * p1.x + t * t * p2.x;
                    if (x > pos.x) {
                        let dy = 2.0 * mt * (p1.y - p0.y) + 2.0 * t * (p2.y - p1.y);
                        crossings += select(-1, 1, dy > 0.0);
                    }
                }
            }
        } else {
            let disc = bb * bb - 4.0 * aa * cc;
            if (disc >= 0.0) {
                let sqrtDisc = sqrt(disc);
                let t1 = (-bb - sqrtDisc) / (2.0 * aa);
                let t2 = (-bb + sqrtDisc) / (2.0 * aa);

                if (t1 >= 0.0 && t1 < 1.0) {
                    let mt = 1.0 - t1;
                    let x = mt * mt * p0.x + 2.0 * mt * t1 * p1.x + t1 * t1 * p2.x;
                    if (x > pos.x) {
                        let dy = 2.0 * mt * (p1.y - p0.y) + 2.0 * t1 * (p2.y - p1.y);
                        crossings += select(-1, 1, dy > 0.0);
                    }
                }
                if (t2 >= 0.0 && t2 < 1.0 && abs(t2 - t1) > 1e-6) {
                    let mt = 1.0 - t2;
                    let x = mt * mt * p0.x + 2.0 * mt * t2 * p1.x + t2 * t2 * p2.x;
                    if (x > pos.x) {
                        let dy = 2.0 * mt * (p1.y - p0.y) + 2.0 * t2 * (p2.y - p1.y);
                        crossings += select(-1, 1, dy > 0.0);
                    }
                }
            }
        }
    }
    return crossings;
}

// Render a vector glyph using SDF curves
// glyphIndex: codepoint in PUA-A range (U+F0000+)
// localUV: position within cell, normalized [0,1]
// Returns: signed distance (negative = inside, positive = outside)
fn sampleVectorGlyph(glyphIndex: u32, localUV: vec2<f32>) -> f32 {
    // Map from PUA-A to actual codepoint (for testing: maps to ASCII)
    let codepoint = glyphIndex - VECTOR_GLYPH_BASE;

    // Look up offset in the offset table
    let offset = vectorOffsetTable[codepoint];
    if (offset == 0xFFFFFFFFu) {
        return 1.0;  // Glyph not present, return "outside"
    }

    // Read header: curveCount (upper 16 bits) | flags (lower 16 bits)
    let header = vectorGlyphBuffer[offset];
    let curveCount = (header >> 16u) & 0xFFFFu;

    if (curveCount == 0u) {
        return 1.0;  // No curves, empty glyph
    }

    // Find minimum distance to all curves
    var minDist = 1000.0;
    let curveBase = offset + 1u;

    for (var i = 0u; i < curveCount; i = i + 1u) {
        let curveOffset = curveBase + i * 3u;
        let p0 = unpackPoint(vectorGlyphBuffer[curveOffset]);
        let p1 = unpackPoint(vectorGlyphBuffer[curveOffset + 1u]);
        let p2 = unpackPoint(vectorGlyphBuffer[curveOffset + 2u]);

        // Early-out: compute distance to curve bounding box
        // If box is farther than current minDist, skip expensive SDF calculation
        let boxMin = min(min(p0, p1), p2);
        let boxMax = max(max(p0, p1), p2);
        let dBox = max(boxMin - localUV, localUV - boxMax);
        let distToBox = length(max(dBox, vec2<f32>(0.0)));

        if (distToBox < minDist) {
            let dist = sdQuadraticBezier(localUV, p0, p1, p2);
            minDist = min(minDist, dist);
        }
    }

    // Use winding number for inside/outside (non-zero = inside)
    let winding = windingNumber(glyphIndex, localUV);
    if (winding != 0) {
        return -minDist;
    }
    return minDist;
}

// ==== VECTOR COVERAGE FONT RENDERING ====

// Winding number for coverage font (uses coverageGlyphBuffer)
fn coverageWindingNumber(glyphIndex: u32, pos: vec2<f32>) -> i32 {
    let codepoint = glyphIndex - COVERAGE_GLYPH_BASE;
    let offset = coverageOffsetTable[codepoint];
    if (offset == 0xFFFFFFFFu) { return 0; }

    let header = coverageGlyphBuffer[offset];
    let curveCount = (header >> 16u) & 0xFFFFu;
    if (curveCount == 0u) { return 0; }

    var crossings = 0;
    let curveBase = offset + 1u;

    for (var i = 0u; i < curveCount; i++) {
        let curveOffset = curveBase + i * 3u;
        let p0 = unpackPoint(coverageGlyphBuffer[curveOffset]);
        let p1 = unpackPoint(coverageGlyphBuffer[curveOffset + 1u]);
        let p2 = unpackPoint(coverageGlyphBuffer[curveOffset + 2u]);

        // Solve for t where curve.y = pos.y
        let y0 = p0.y - pos.y;
        let y1 = p1.y - pos.y;
        let y2 = p2.y - pos.y;

        let aa = y0 - 2.0 * y1 + y2;
        let bb = 2.0 * (y1 - y0);
        let cc = y0;

        if (abs(aa) < 1e-6) {
            // Linear case
            if (abs(bb) > 1e-6) {
                let t = -cc / bb;
                if (t >= 0.0 && t < 1.0) {
                    let mt = 1.0 - t;
                    let x = mt * mt * p0.x + 2.0 * mt * t * p1.x + t * t * p2.x;
                    if (x > pos.x) {
                        let dy = 2.0 * mt * (p1.y - p0.y) + 2.0 * t * (p2.y - p1.y);
                        crossings += select(-1, 1, dy > 0.0);
                    }
                }
            }
        } else {
            let disc = bb * bb - 4.0 * aa * cc;
            if (disc >= 0.0) {
                let sqrtDisc = sqrt(disc);
                let t1 = (-bb - sqrtDisc) / (2.0 * aa);
                let t2 = (-bb + sqrtDisc) / (2.0 * aa);

                if (t1 >= 0.0 && t1 < 1.0) {
                    let mt = 1.0 - t1;
                    let x = mt * mt * p0.x + 2.0 * mt * t1 * p1.x + t1 * t1 * p2.x;
                    if (x > pos.x) {
                        let dy = 2.0 * mt * (p1.y - p0.y) + 2.0 * t1 * (p2.y - p1.y);
                        crossings += select(-1, 1, dy > 0.0);
                    }
                }
                if (t2 >= 0.0 && t2 < 1.0 && abs(t2 - t1) > 1e-6) {
                    let mt = 1.0 - t2;
                    let x = mt * mt * p0.x + 2.0 * mt * t2 * p1.x + t2 * t2 * p2.x;
                    if (x > pos.x) {
                        let dy = 2.0 * mt * (p1.y - p0.y) + 2.0 * t2 * (p2.y - p1.y);
                        crossings += select(-1, 1, dy > 0.0);
                    }
                }
            }
        }
    }
    return crossings;
}

// Sample coverage glyph using multi-sample anti-aliasing (4x4 grid)
// Returns coverage value [0,1] where 1 = fully inside glyph
fn sampleCoverageGlyph(glyphIndex: u32, localUV: vec2<f32>, pixelSize: vec2<f32>) -> f32 {
    let codepoint = glyphIndex - COVERAGE_GLYPH_BASE;
    let offset = coverageOffsetTable[codepoint];
    if (offset == 0xFFFFFFFFu) {
        return 0.0;  // Glyph not present
    }

    let header = coverageGlyphBuffer[offset];
    let curveCount = (header >> 16u) & 0xFFFFu;
    if (curveCount == 0u) {
        return 0.0;  // Empty glyph
    }

    // 4x4 supersampling for coverage calculation
    var coverage = 0.0;
    let samples = 4;
    let sampleWeight = 1.0 / f32(samples * samples);

    for (var sy = 0; sy < samples; sy++) {
        for (var sx = 0; sx < samples; sx++) {
            // Jittered sample position within pixel
            let sampleOffset = vec2<f32>(
                (f32(sx) + 0.5) / f32(samples) - 0.5,
                (f32(sy) + 0.5) / f32(samples) - 0.5
            );
            let samplePos = localUV + sampleOffset * pixelSize;

            // Check if sample is inside glyph using winding number
            let winding = coverageWindingNumber(glyphIndex, samplePos);
            if (winding != 0) {
                coverage += sampleWeight;
            }
        }
    }

    return coverage;
}

// Extract font type from cell attrs (bits 5-7)
fn getFontType(attrs: u32) -> u32 {
    return (attrs >> 5u) & 0x7u;
}

// ==== PRE-EFFECT FUNCTIONS (injected by loader) ====
// PRE_EFFECT_FUNCTIONS_PLACEHOLDER

// ==== COORD EFFECT FUNCTIONS (injected by loader) ====
// EFFECT_FUNCTIONS_PLACEHOLDER

// ==== POST-EFFECT FUNCTIONS (injected by loader) ====
// POST_EFFECT_FUNCTIONS_PLACEHOLDER

// ==== SHADER GLYPH FUNCTIONS (injected by loader) ====
// SHADER_GLYPH_FUNCTIONS_PLACEHOLDER

// Helper to unpack u32 color to vec3<f32>
fn unpackColor(packed: u32) -> vec3<f32> {
    return vec3<f32>(
        f32(packed & 0xFFu) / 255.0,
        f32((packed >> 8u) & 0xFFu) / 255.0,
        f32((packed >> 16u) & 0xFFu) / 255.0
    );
}

// Helper to unpack u32 color to vec4<f32> (with alpha)
fn unpackColorAlpha(packed: u32) -> vec4<f32> {
    return vec4<f32>(
        f32(packed & 0xFFu) / 255.0,
        f32((packed >> 8u) & 0xFFu) / 255.0,
        f32((packed >> 16u) & 0xFFu) / 255.0,
        f32((packed >> 24u) & 0xFFu) / 255.0
    );
}

// Dispatch to shader glyph by codepoint
// Returns rendered color, or bgColor if glyph not found
// pixelPos: absolute pixel coordinates on screen (for tiled effects like fractals)
// mousePos: current mouse position in pixels
// fg/bg: raw packed u32 from cell - cards use as metadata index, regular glyphs unpack to color
fn renderShaderGlyph(glyphIndex: u32, localUV: vec2<f32>, time: f32, fg: u32, bg: u32, pixelPos: vec2<f32>, mousePos: vec2<f32>, lastChar: u32, lastCharTime: f32) -> vec3<f32> {
    // shaderGlyphDispatch
    // (loader generates: if glyphIndex == 1052672u { return shaderGlyph_1048577(...); } else if ...)
    return unpackColor(bg);  // Fallback if no shader glyph matches
}

// =============================================================================
// YDraw Overlay Rendering (full-screen painters)
// Renders ydraw content from a metadata slot as a full-screen overlay.
// Returns vec4: rgb = color, a = coverage (0 = transparent, 1 = opaque)
// =============================================================================
fn renderYdrawOverlay(slotIndex: u32, pixelPos: vec2<f32>) -> vec4<f32> {
    if (slotIndex == 0u) {
        return vec4<f32>(0.0);  // Disabled
    }

    let metaOffset = slotIndex * 16u;
    let primitiveCount = cardMetadata[metaOffset + 1u];
    if (primitiveCount == 0u) {
        return vec4<f32>(0.0);  // No content
    }

    // Read metadata
    let primitiveOffset = cardMetadata[metaOffset + 0u];
    let gridOffset = cardMetadata[metaOffset + 2u];
    let gridWidth = cardMetadata[metaOffset + 3u];
    let gridHeight = cardMetadata[metaOffset + 4u];
    let cellSizeXY = unpack2x16float(cardMetadata[metaOffset + 5u]);
    let cellSizeX = cellSizeXY.x;
    let cellSizeY = cellSizeXY.y;
    let glyphOffset = cardMetadata[metaOffset + 6u];
    let glyphCount = cardMetadata[metaOffset + 7u];

    // Content bounds
    let contentMinX = bitcast<f32>(cardMetadata[metaOffset + 8u]);
    let contentMinY = bitcast<f32>(cardMetadata[metaOffset + 9u]);
    let contentMaxX = bitcast<f32>(cardMetadata[metaOffset + 10u]);
    let contentMaxY = bitcast<f32>(cardMetadata[metaOffset + 11u]);

    // Early exit if no grid
    if (gridWidth == 0u || gridHeight == 0u || cellSizeX <= 0.0 || cellSizeY <= 0.0) {
        return vec4<f32>(0.0);
    }

    // For full-screen overlay, map pixel position directly to scene coordinates
    // The scene bounds define where content is placed
    let contentW = contentMaxX - contentMinX;
    let contentH = contentMaxY - contentMinY;

    // Scale pixel position to scene - overlay covers full grid area
    let gridPixelW = grid.gridSize.x * grid.cellSize.x;
    let gridPixelH = grid.gridSize.y * grid.cellSize.y;
    let scenePos = vec2<f32>(
        contentMinX + (pixelPos.x / gridPixelW) * contentW,
        contentMinY + (pixelPos.y / gridPixelH) * contentH
    );

    // Grid lookup
    let invCellSizeX = 1.0 / cellSizeX;
    let invCellSizeY = 1.0 / cellSizeY;
    let cellX = u32(clamp((scenePos.x - contentMinX) * invCellSizeX, 0.0, f32(gridWidth - 1u)));
    let cellY = u32(clamp((scenePos.y - contentMinY) * invCellSizeY, 0.0, f32(gridHeight - 1u)));
    let cellIndex = cellY * gridWidth + cellX;

    // Variable-length grid lookup
    let packedStart = bitcast<u32>(cardStorage[gridOffset + cellIndex]);
    let cellEntryCount = bitcast<u32>(cardStorage[gridOffset + packedStart]);
    let loopCount = min(cellEntryCount, 64u);  // Safety cap

    let primDataBase = primitiveOffset + primitiveCount;
    var resultColor = vec3<f32>(0.0);
    var resultAlpha = 0.0;

    for (var i = 0u; i < loopCount; i++) {
        let rawIdx = bitcast<u32>(cardStorage[gridOffset + packedStart + 1u + i]);

        // Skip glyphs for now (bit 31 set = glyph)
        if ((rawIdx & 0x80000000u) != 0u) {
            continue;
        }

        // SDF primitive
        let primOff = primDataBase + rawIdx;
        let d = evaluateYdrawSDF(primOff, scenePos);

        let colors = ydrawPrimColors(primOff);
        let fillColorPacked = colors.x;
        if (d < 0.0 && fillColorPacked != 0u) {
            let fillColorAlpha = unpackColorAlpha(fillColorPacked);
            let edgeAlpha = clamp(-d * 2.0, 0.0, 1.0);
            let alpha = edgeAlpha * fillColorAlpha.a;
            resultColor = mix(resultColor, fillColorAlpha.rgb, alpha);
            resultAlpha = max(resultAlpha, alpha);
        }

        let strokeColorPacked = colors.y;
        let strokeWidth = ydrawPrimStrokeWidth(primOff);
        if (strokeWidth > 0.0 && strokeColorPacked != 0u) {
            let strokeDist = abs(d) - strokeWidth * 0.5;
            if (strokeDist < 0.0) {
                let strokeColorAlpha = unpackColorAlpha(strokeColorPacked);
                let edgeAlpha = clamp(-strokeDist * 2.0, 0.0, 1.0);
                let alpha = edgeAlpha * strokeColorAlpha.a;
                resultColor = mix(resultColor, strokeColorAlpha.rgb, alpha);
                resultAlpha = max(resultAlpha, alpha);
            }
        }
    }

    return vec4<f32>(resultColor, resultAlpha);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Convert fragment position to viewport-local pixel coordinates
    // input.position.xy is in framebuffer space; subtract viewport origin
    // so grid calculations start at (0,0) within the pane's client area
    let fbPixelPos = input.position.xy;
    var pixelPos = fbPixelPos - grid.viewportOrigin;

    // ==== VISUAL ZOOM: transform coordinates before all other processing ====
    // This is shader-based magnification that doesn't change terminal structure
    if (grid.visualZoomScale > 1.0) {
        // Zoom centered on viewport center, then apply pan offset
        let viewportCenter = grid.screenSize * 0.5;
        // Inverse transform: divide by scale (2x zoom = sample half the area)
        pixelPos = (pixelPos - viewportCenter) / grid.visualZoomScale
                 + viewportCenter
                 + vec2<f32>(grid.visualZoomOffsetX, grid.visualZoomOffsetY);
    }

    // Calculate grid dimensions in pixels
    let gridPixelWidth = grid.gridSize.x * grid.cellSize.x;
    let gridPixelHeight = grid.gridSize.y * grid.cellSize.y;

    // Check if outside the grid area
    if (pixelPos.x < 0.0 || pixelPos.y < 0.0 || pixelPos.x >= gridPixelWidth || pixelPos.y >= gridPixelHeight) {
        return vec4<f32>(0.1, 0.1, 0.1, 1.0);  // Background color
    }

    // ==== COORD EFFECT: distort pixel position before cell lookup ====
    // effectIndex 0 = no effect, 1+ = apply effect
    var distortedPos = pixelPos;
    // EFFECT_APPLY_PLACEHOLDER

    // Calculate which cell we're in (clamped to valid range after effects)
    let cellCol = clamp(floor(distortedPos.x / grid.cellSize.x), 0.0, grid.gridSize.x - 1.0);
    let cellRow = clamp(floor(distortedPos.y / grid.cellSize.y), 0.0, grid.gridSize.y - 1.0);
    let cellCoord = vec2<i32>(i32(cellCol), i32(cellRow));

    // Position within the cell (pixels from top-left of cell)
    let localPxBase = vec2<f32>(
        distortedPos.x - cellCol * grid.cellSize.x,
        distortedPos.y - cellRow * grid.cellSize.y
    );

    // Get cell data from storage buffer (zero-copy from CPU)
    let cellIndex = u32(cellCoord.y) * u32(grid.gridSize.x) + u32(cellCoord.x);
    let cell = cellBuffer[cellIndex];

    // Unpack cell data
    var glyphIndex = cell.glyph;
    let fgColor = vec4<f32>(
        f32(cell.fg & 0xFFu) / 255.0,
        f32((cell.fg >> 8u) & 0xFFu) / 255.0,
        f32((cell.fg >> 16u) & 0xFFu) / 255.0,
        f32((cell.fg >> 24u) & 0xFFu) / 255.0
    );
    let bgColor = vec4<f32>(
        f32(cell.bg & 0xFFu) / 255.0,
        f32((cell.bg >> 8u) & 0xFFu) / 255.0,
        f32((cell.bg >> 16u) & 0xFFu) / 255.0,
        1.0  // bg alpha always 1.0
    );
    let cellAttrs = (cell.bg >> 24u) & 0xFFu;  // style is in high byte of bg

    // Extract attribute flags
    let underlineType = (cellAttrs & ATTR_UNDERLINE_MASK) >> 2u;
    let hasStrikethrough = (cellAttrs & ATTR_STRIKETHROUGH) != 0u;
    let fontType = getFontType(cellAttrs);
    // Backward compatible: if fontType is BITMAP or legacy emoji bit is set
    let isEmoji = fontType == FONT_TYPE_BITMAP;

    // ==== PRE-EFFECT: modify cell data before rendering ====
    // PRE_EFFECT_APPLY_PLACEHOLDER

    // Check if cursor should be rendered on this cell
    let cursorCol = i32(grid.cursorPos.x);
    let cursorRow = i32(grid.cursorPos.y);
    let isCursor = grid.cursorVisible > 0.5 &&
                   cellCoord.x == cursorCol &&
                   cellCoord.y == cursorRow;

    // Handle wide character continuation (0xFFFE) - look at previous cell
    var cellOffset = 0.0;
    if (glyphIndex == 0xFFFEu) {
        if (cellCoord.x > 0) {
            let prevCellIndex = u32(cellCoord.y) * u32(grid.gridSize.x) + u32(cellCoord.x - 1);
            glyphIndex = cellBuffer[prevCellIndex].glyph;
            cellOffset = grid.cellSize.x;  // Offset by one cell width to continue rendering
        } else {
            // Edge case: continuation at column 0 (shouldn't happen)
            var resultColor = bgColor.rgb;
            if (isCursor) {
                resultColor = vec3<f32>(1.0, 1.0, 1.0) - resultColor;
            }
            return vec4<f32>(resultColor, 1.0);
        }
    }

    // Current pixel position within cell (offset for wide char continuation)
    let localPx = localPxBase + vec2<f32>(cellOffset, 0.0);

    // Start with background color
    var finalColor = bgColor.rgb;
    var hasGlyph = false;

    // Check if we should render a glyph
    // Note: for emojis, index 0 is valid (first emoji in atlas), so don't skip it
    let skipGlyph = (!isEmoji && glyphIndex == 0u) || glyphIndex == 0xFFFFu ||
                    (glyphIndex >= 0xF000u && glyphIndex <= 0xFFFDu);

    if (!skipGlyph) {
        // Check for vector glyph first (Plane 15 PUA-A range: U+F0000+)
        if (isVectorGlyph(glyphIndex)) {
            // Vector font: render using SDF curves
            // Curves are normalized to font metrics (ascender/descender/advance)
            // Just flip Y for screen coordinates
            let localUV = vec2<f32>(
                localPxBase.x / grid.cellSize.x,
                1.0 - localPxBase.y / grid.cellSize.y
            );

            let sd = sampleVectorGlyph(glyphIndex, localUV);

            // Anti-aliasing: compute pixel width in UV space from cell size
            // One pixel in UV space = 1.0 / cellSize
            let pixelWidth = 1.0 / min(grid.cellSize.x, grid.cellSize.y);

            // Smooth anti-aliasing with ~1.5 pixel transition zone
            let alpha = clamp(0.5 - sd / (pixelWidth * 1.5), 0.0, 1.0);

            finalColor = mix(bgColor.rgb, fgColor.rgb, alpha);
            hasGlyph = alpha > 0.01;
        } else if (isCoverageGlyph(glyphIndex)) {
            // Coverage font: render using multi-sample coverage calculation
            let localUV = vec2<f32>(
                localPxBase.x / grid.cellSize.x,
                1.0 - localPxBase.y / grid.cellSize.y
            );

            // Pixel size in UV space
            let pixelSize = vec2<f32>(1.0 / grid.cellSize.x, 1.0 / grid.cellSize.y);

            // Get coverage value (0-1)
            let coverage = sampleCoverageGlyph(glyphIndex, localUV, pixelSize);

            finalColor = mix(bgColor.rgb, fgColor.rgb, coverage);
            hasGlyph = coverage > 0.01;
        } else if (isRasterGlyph(glyphIndex)) {
            // Raster font: sample from texture atlas
            let rasterIdx = glyphIndex - RASTER_GLYPH_BASE;
            let glyphUV = rasterMetadata[rasterIdx].uv;

            // Skip empty glyphs (marked with negative UV)
            if (glyphUV.x >= 0.0) {
                // Compute UV within the glyph cell
                // glyphSizeUV should be passed as uniform, for now assume square cells
                let glyphSizeUV = vec2<f32>(grid.cellSize.x / 1024.0, grid.cellSize.y / 1024.0);
                let localUV = localPxBase / grid.cellSize;
                let sampleUV = glyphUV + localUV * glyphSizeUV;

                // Sample grayscale texture (R channel) - use Level to avoid non-uniform control flow issue
                let alpha = textureSampleLevel(rasterTexture, rasterSampler, sampleUV, 0.0).r;

                finalColor = mix(bgColor.rgb, fgColor.rgb, alpha);
                hasGlyph = alpha > 0.01;
            }
        } else if (isCardGlyph(glyphIndex) || isShaderGlyph(glyphIndex)) {
            // Card or shader glyph: render via shader function
            let localUV = localPxBase / grid.cellSize;  // Normalize to 0-1
            let mousePos = vec2<f32>(globals.mouseX, globals.mouseY);
            finalColor = renderShaderGlyph(glyphIndex, localUV, globals.time, cell.fg, cell.bg, fbPixelPos, mousePos, globals.lastChar, globals.lastCharTime);
            hasGlyph = true;
        } else if (isEmoji) {
            // Emoji rendering: glyphIndex is the emoji index in emojiMetadata array
            let emoji = emojiMetadata[glyphIndex];

            // Emoji fills the cell, centered and scaled to fit
            // emoji.size contains the actual glyph size in atlas pixels
            let emojiAspect = emoji.size.x / emoji.size.y;
            let cellAspect = grid.cellSize.x / grid.cellSize.y;

            // Scale emoji to fit within cell while maintaining aspect ratio
            var scaledSize: vec2<f32>;
            if (emojiAspect > cellAspect) {
                // Emoji is wider - fit to width
                scaledSize = vec2<f32>(grid.cellSize.x, grid.cellSize.x / emojiAspect);
            } else {
                // Emoji is taller - fit to height
                scaledSize = vec2<f32>(grid.cellSize.y * emojiAspect, grid.cellSize.y);
            }

            // Center in cell
            let offset = (grid.cellSize - scaledSize) * 0.5;

            // Check if inside emoji bounds
            if (localPx.x >= offset.x && localPx.x < offset.x + scaledSize.x &&
                localPx.y >= offset.y && localPx.y < offset.y + scaledSize.y) {
                // Calculate UV for sampling
                let emojiLocalPos = (localPx - offset) / scaledSize;
                let uv = mix(emoji.uvMin, emoji.uvMax, emojiLocalPos);

                // Sample emoji texture (pre-rendered RGBA)
                let emojiColor = textureSampleLevel(emojiTexture, emojiSampler, uv, 0.0);

                // Alpha blend emoji over background
                finalColor = mix(bgColor.rgb, emojiColor.rgb, emojiColor.a);
                hasGlyph = emojiColor.a > 0.01;
            }
        } else {
            // MSDF text rendering
            let glyph = glyphMetadata[glyphIndex];

            // Calculate glyph position within cell
            let scaledGlyphSize = glyph.size * grid.scale;
            let scaledBearing = glyph.bearing * grid.scale;

            // Baseline at 80% of cell height
            let baseline = grid.cellSize.y * 0.8;
            let glyphTop = baseline - scaledBearing.y;
            let glyphLeft = scaledBearing.x;

            // Glyph bounds in cell pixel space
            let glyphMinPx = vec2<f32>(glyphLeft, glyphTop);
            let glyphMaxPx = vec2<f32>(glyphLeft + scaledGlyphSize.x, glyphTop + scaledGlyphSize.y);

            // Check if inside glyph bounds
            if (localPx.x >= glyphMinPx.x && localPx.x < glyphMaxPx.x &&
                localPx.y >= glyphMinPx.y && localPx.y < glyphMaxPx.y) {
                // Calculate UV for MSDF sampling
                let glyphLocalPos = (localPx - glyphMinPx) / scaledGlyphSize;
                let uv = mix(glyph.uvMin, glyph.uvMax, glyphLocalPos);

                // Sample MSDF texture
                let msdf = textureSampleLevel(fontTexture, fontSampler, uv, 0.0);

                // Calculate signed distance
                let sd = median(msdf.r, msdf.g, msdf.b);

                // Apply anti-aliased edge
                let screenPxRange = grid.pixelRange * grid.scale;
                let alpha = clamp((sd - 0.5) * screenPxRange + 0.5, 0.0, 1.0);

                // Blend foreground over background
                finalColor = mix(bgColor.rgb, fgColor.rgb, alpha);
                hasGlyph = alpha > 0.01;
            }
        }
    }

    // Apply text decorations (underline and strikethrough)
    let localY = localPxBase.y;  // Use base position, not offset
    let localX = localPxBase.x;

    // Check underline
    if (underlineType > 0u) {
        var inUnderline = false;
        if (underlineType == 3u) {
            // Curly underline needs X position
            inUnderline = isInCurlyUnderline(localX, localY, grid.cellSize.x, grid.cellSize.y, grid.scale);
        } else {
            inUnderline = isInUnderline(localY, grid.cellSize.y, underlineType, grid.scale);
        }
        if (inUnderline) {
            finalColor = fgColor.rgb;
        }
    }

    // Check strikethrough
    if (hasStrikethrough) {
        if (isInStrikethrough(localY, grid.cellSize.y, grid.scale)) {
            finalColor = fgColor.rgb;
        }
    }

    // Cursor rendering: shape + blink
    if (isCursor) {
        // Blink: skip drawing cursor during off-phase (1Hz, 50% duty)
        var cursorOn = true;
        if (grid.cursorBlink > 0.5) {
            cursorOn = fract(globals.time) < 0.5;
        }

        if (cursorOn) {
            let localUV = localPxBase / grid.cellSize;
            let shape = i32(grid.cursorShape);
            var drawCursor = false;

            if (shape == 2) {
                // Underline: bottom 15% of cell
                drawCursor = localUV.y > 0.85;
            } else if (shape == 3) {
                // Bar: left 10% of cell
                drawCursor = localUV.x < 0.1;
            } else {
                // Block (1 or default): full cell
                drawCursor = true;
            }

            if (drawCursor) {
                finalColor = vec3<f32>(1.0, 1.0, 1.0) - finalColor;
            }
        }
    }

    // Selection highlighting
    if (grid.hasSelection > 0.5) {
        let col = i32(cellCol);
        let row = i32(cellRow);
        let sStartCol = i32(grid.selStart.x);
        let sStartRow = i32(grid.selStart.y);
        let sEndCol = i32(grid.selEnd.x);
        let sEndRow = i32(grid.selEnd.y);

        var inSelection = false;
        if (row > sStartRow && row < sEndRow) {
            // Entire row is selected
            inSelection = true;
        } else if (row == sStartRow && row == sEndRow) {
            // Single-row selection
            inSelection = col >= sStartCol && col <= sEndCol;
        } else if (row == sStartRow) {
            // First row of multi-row selection
            inSelection = col >= sStartCol;
        } else if (row == sEndRow) {
            // Last row of multi-row selection
            inSelection = col <= sEndCol;
        }

        if (inSelection) {
            // Highlight: blend with selection color (blue tint)
            finalColor = mix(finalColor, vec3<f32>(0.3, 0.5, 0.8), 0.4);
        }
    }

    // ==== POST-EFFECT: modify final pixel color ====
    // POST_EFFECT_APPLY_PLACEHOLDER

    // ==== YDRAW OVERLAYS: render full-screen painter layers ====
    // Layer 1: Scrolling painter (scrolls with terminal content)
    if (grid.ydrawScrollingSlot > 0u) {
        let scrollingColor = renderYdrawOverlay(grid.ydrawScrollingSlot, pixelPos);
        if (scrollingColor.a > 0.01) {
            finalColor = mix(finalColor, scrollingColor.rgb, scrollingColor.a);
        }
    }
    // Layer 2: Overlay painter (fixed position, on top)
    if (grid.ydrawOverlaySlot > 0u) {
        let overlayColor = renderYdrawOverlay(grid.ydrawOverlaySlot, pixelPos);
        if (overlayColor.a > 0.01) {
            finalColor = mix(finalColor, overlayColor.rgb, overlayColor.a);
        }
    }

    return vec4<f32>(finalColor, 1.0);
}
