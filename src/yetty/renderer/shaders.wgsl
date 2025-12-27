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

// Bindings
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var fontTexture: texture_2d<f32>;
@group(0) @binding(2) var fontSampler: sampler;
@group(0) @binding(3) var<storage, read> glyphMetadata: array<GlyphMetadata>;
@group(0) @binding(4) var cellGlyphTexture: texture_2d<u32>;   // R16Uint
@group(0) @binding(5) var cellFgColorTexture: texture_2d<f32>; // RGBA8Unorm
@group(0) @binding(6) var cellBgColorTexture: texture_2d<f32>; // RGBA8Unorm

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

    // Position within the cell (0-1)
    let cellLocalPos = vec2<f32>(
        (pixelPos.x - cellCol * uniforms.cellSize.x) / uniforms.cellSize.x,
        (pixelPos.y - cellRow * uniforms.cellSize.y) / uniforms.cellSize.y
    );

    // Get cell data from textures
    var glyphIndex = textureLoad(cellGlyphTexture, cellCoord, 0).r;
    let fgColor = textureLoad(cellFgColorTexture, cellCoord, 0);
    let bgColor = textureLoad(cellBgColorTexture, cellCoord, 0);

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
            if (isCursor) {
                return vec4<f32>(vec3<f32>(1.0, 1.0, 1.0) - bgColor.rgb, 1.0);
            }
            return bgColor;
        }
    }

    // If no glyph (index 0), plugin cell (0xFFFF), or custom glyph range (0xF000-0xFFFD),
    // output background and let the appropriate layer render instead
    if (glyphIndex == 0u || glyphIndex == 0xFFFFu ||
        (glyphIndex >= 0xF000u && glyphIndex <= 0xFFFDu)) {
        if (isCursor) {
            return vec4<f32>(vec3<f32>(1.0, 1.0, 1.0) - bgColor.rgb, 1.0);
        }
        return bgColor;
    }

    // Get glyph metadata
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

    // Current pixel position within cell (offset for wide char continuation)
    let localPx = cellLocalPos * uniforms.cellSize + vec2<f32>(cellOffset, 0.0);

    // Check if inside glyph bounds
    if (localPx.x < glyphMinPx.x || localPx.x >= glyphMaxPx.x ||
        localPx.y < glyphMinPx.y || localPx.y >= glyphMaxPx.y) {
        // Outside glyph bounds - show background (possibly inverted for cursor)
        if (isCursor) {
            return vec4<f32>(vec3<f32>(1.0, 1.0, 1.0) - bgColor.rgb, 1.0);
        }
        return bgColor;
    }

    // Calculate UV for MSDF sampling
    let glyphLocalPos = (localPx - glyphMinPx) / scaledGlyphSize;
    let uv = mix(glyph.uvMin, glyph.uvMax, glyphLocalPos);

    // Sample MSDF texture
    let msdf = textureSample(fontTexture, fontSampler, uv);

    // Calculate signed distance
    let sd = median(msdf.r, msdf.g, msdf.b);

    // Apply anti-aliased edge
    // Scale pixelRange by render scale to maintain consistent AA quality at different zoom levels
    let screenPxRange = uniforms.pixelRange * uniforms.scale;
    let alpha = clamp((sd - 0.5) * screenPxRange + 0.5, 0.0, 1.0);

    // Blend foreground over background
    var finalColor = mix(bgColor.rgb, fgColor.rgb, alpha);

    // Cursor rendering: invert colors at cursor position
    if (isCursor) {
        finalColor = vec3<f32>(1.0, 1.0, 1.0) - finalColor;
    }

    return vec4<f32>(finalColor, 1.0);
}
