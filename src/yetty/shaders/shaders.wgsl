// Uniforms
struct Uniforms {
    projection: mat4x4<f32>,   // 64 bytes
    screenSize: vec2<f32>,     // 8 bytes
    cellSize: vec2<f32>,       // 8 bytes
    gridSize: vec2<f32>,       // 8 bytes (cols, rows)
    pixelRange: f32,           // 4 bytes
    scale: f32,                // 4 bytes
    cursorPos: vec2<f32>,      // 8 bytes (col, row)
    cursorVisible: f32,        // 4 bytes
    _pad: f32,                 // 4 bytes
};

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

// Bindings
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var fontTexture: texture_2d<f32>;
@group(0) @binding(2) var fontSampler: sampler;
@group(0) @binding(3) var<storage, read> glyphMetadata: array<GlyphMetadata>;
@group(0) @binding(4) var cellGlyphTexture: texture_2d<u32>;   // R16Uint
@group(0) @binding(5) var cellFgColorTexture: texture_2d<f32>; // RGBA8Unorm
@group(0) @binding(6) var cellBgColorTexture: texture_2d<f32>; // RGBA8Unorm
@group(0) @binding(7) var cellAttrsTexture: texture_2d<u32>;   // R8Uint - packed attributes

// Emoji bindings
@group(0) @binding(8) var emojiTexture: texture_2d<f32>;       // RGBA8Unorm - color emoji atlas
@group(0) @binding(9) var emojiSampler: sampler;
@group(0) @binding(10) var<storage, read> emojiMetadata: array<EmojiGlyphMetadata>;

// Attribute bit masks (matches CellAttrs in grid.h)
const ATTR_BOLD: u32 = 0x01u;           // Bit 0
const ATTR_ITALIC: u32 = 0x02u;         // Bit 1
const ATTR_UNDERLINE_MASK: u32 = 0x0Cu; // Bits 2-3 (0=none, 1=single, 2=double, 3=curly)
const ATTR_STRIKETHROUGH: u32 = 0x10u;  // Bit 4
const ATTR_EMOJI: u32 = 0x20u;          // Bit 5 - render from emoji atlas

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

// MSDF median function
fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Convert fragment position to pixel coordinates
    let pixelPos = input.position.xy;

    // Calculate grid dimensions in pixels
    let gridPixelWidth = uniforms.gridSize.x * uniforms.cellSize.x;
    let gridPixelHeight = uniforms.gridSize.y * uniforms.cellSize.y;

    // Check if outside the grid area
    if (pixelPos.x >= gridPixelWidth || pixelPos.y >= gridPixelHeight) {
        return vec4<f32>(0.1, 0.1, 0.1, 1.0);  // Background color
    }

    // Calculate which cell we're in
    let cellCol = floor(pixelPos.x / uniforms.cellSize.x);
    let cellRow = floor(pixelPos.y / uniforms.cellSize.y);
    let cellCoord = vec2<i32>(i32(cellCol), i32(cellRow));

    // Position within the cell (pixels from top-left of cell)
    let localPxBase = vec2<f32>(
        pixelPos.x - cellCol * uniforms.cellSize.x,
        pixelPos.y - cellRow * uniforms.cellSize.y
    );

    // Get cell data from textures
    var glyphIndex = textureLoad(cellGlyphTexture, cellCoord, 0).r;
    let fgColor = textureLoad(cellFgColorTexture, cellCoord, 0);
    let bgColor = textureLoad(cellBgColorTexture, cellCoord, 0);
    let cellAttrs = textureLoad(cellAttrsTexture, cellCoord, 0).r;

    // Extract attribute flags
    let underlineType = (cellAttrs & ATTR_UNDERLINE_MASK) >> 2u;
    let hasStrikethrough = (cellAttrs & ATTR_STRIKETHROUGH) != 0u;
    let isEmoji = (cellAttrs & ATTR_EMOJI) != 0u;

    // Check if cursor should be rendered on this cell
    let cursorCol = i32(uniforms.cursorPos.x);
    let cursorRow = i32(uniforms.cursorPos.y);
    let isCursor = uniforms.cursorVisible > 0.5 &&
                   cellCoord.x == cursorCol &&
                   cellCoord.y == cursorRow;

    // Handle wide character continuation (0xFFFE) - look at previous cell
    var cellOffset = 0.0;
    if (glyphIndex == 0xFFFEu) {
        if (cellCoord.x > 0) {
            let prevCellCoord = vec2<i32>(cellCoord.x - 1, cellCoord.y);
            glyphIndex = textureLoad(cellGlyphTexture, prevCellCoord, 0).r;
            cellOffset = uniforms.cellSize.x;  // Offset by one cell width to continue rendering
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
        if (isEmoji) {
            // Emoji rendering: glyphIndex is the emoji index in emojiMetadata array
            let emoji = emojiMetadata[glyphIndex];

            // Emoji fills the cell, centered and scaled to fit
            // emoji.size contains the actual glyph size in atlas pixels
            let emojiAspect = emoji.size.x / emoji.size.y;
            let cellAspect = uniforms.cellSize.x / uniforms.cellSize.y;

            // Scale emoji to fit within cell while maintaining aspect ratio
            var scaledSize: vec2<f32>;
            if (emojiAspect > cellAspect) {
                // Emoji is wider - fit to width
                scaledSize = vec2<f32>(uniforms.cellSize.x, uniforms.cellSize.x / emojiAspect);
            } else {
                // Emoji is taller - fit to height
                scaledSize = vec2<f32>(uniforms.cellSize.y * emojiAspect, uniforms.cellSize.y);
            }

            // Center in cell
            let offset = (uniforms.cellSize - scaledSize) * 0.5;

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
            let scaledGlyphSize = glyph.size * uniforms.scale;
            let scaledBearing = glyph.bearing * uniforms.scale;

            // Baseline at 80% of cell height
            let baseline = uniforms.cellSize.y * 0.8;
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
                let screenPxRange = uniforms.pixelRange * uniforms.scale;
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
            inUnderline = isInCurlyUnderline(localX, localY, uniforms.cellSize.x, uniforms.cellSize.y, uniforms.scale);
        } else {
            inUnderline = isInUnderline(localY, uniforms.cellSize.y, underlineType, uniforms.scale);
        }
        if (inUnderline) {
            finalColor = fgColor.rgb;
        }
    }

    // Check strikethrough
    if (hasStrikethrough) {
        if (isInStrikethrough(localY, uniforms.cellSize.y, uniforms.scale)) {
            finalColor = fgColor.rgb;
        }
    }

    // Cursor rendering: invert colors at cursor position
    if (isCursor) {
        finalColor = vec3<f32>(1.0, 1.0, 1.0) - finalColor;
    }

    return vec4<f32>(finalColor, 1.0);
}
