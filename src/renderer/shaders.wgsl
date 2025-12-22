// Uniform buffer for projection and view
struct Uniforms {
    projection: mat4x4<f32>,
    screenSize: vec2<f32>,
    cellSize: vec2<f32>,
    pixelRange: f32,
    scale: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var fontTexture: texture_2d<f32>;
@group(0) @binding(2) var fontSampler: sampler;

// Per-instance vertex data
struct VertexInput {
    @location(0) position: vec2<f32>,      // Quad corner (0,0), (1,0), (0,1), (1,1)
    @location(1) cellPos: vec2<f32>,       // Grid cell position (col, row)
    @location(2) uvMin: vec2<f32>,         // Glyph UV min in atlas
    @location(3) uvMax: vec2<f32>,         // Glyph UV max in atlas
    @location(4) glyphSize: vec2<f32>,     // Glyph size in pixels
    @location(5) glyphBearing: vec2<f32>,  // Glyph bearing (offset)
    @location(6) fgColor: vec4<f32>,       // Foreground color
    @location(7) bgColor: vec4<f32>,       // Background color
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) fgColor: vec4<f32>,
    @location(2) bgColor: vec4<f32>,
    @location(3) cellUV: vec2<f32>,        // Position within cell (0-1)
    @location(4) glyphBoundsMin: vec2<f32>, // Glyph start in cell (0-1)
    @location(5) glyphBoundsMax: vec2<f32>, // Glyph end in cell (0-1)
    @location(6) uvMin: vec2<f32>,          // Glyph UV min in atlas
    @location(7) uvMax: vec2<f32>,          // Glyph UV max in atlas
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Calculate cell position in pixels - CELL-SIZED quad
    let cellPixelPos = input.cellPos * uniforms.cellSize;
    let localPos = input.position * uniforms.cellSize;
    let pixelPos = cellPixelPos + localPos;

    // Transform to clip space
    output.position = uniforms.projection * vec4<f32>(pixelPos, 0.0, 1.0);

    // Calculate glyph bounds within cell (normalized 0-1)
    let scaledGlyphSize = input.glyphSize * uniforms.scale;
    let scaledBearing = input.glyphBearing * uniforms.scale;
    let baseline = uniforms.cellSize.y * 0.8;
    let glyphTop = baseline - scaledBearing.y;
    let glyphLeft = (uniforms.cellSize.x - scaledGlyphSize.x) * 0.5 + scaledBearing.x;

    // Glyph bounds in cell-normalized coordinates
    output.glyphBoundsMin = vec2<f32>(glyphLeft, glyphTop) / uniforms.cellSize;
    output.glyphBoundsMax = vec2<f32>(glyphLeft + scaledGlyphSize.x, glyphTop + scaledGlyphSize.y) / uniforms.cellSize;

    // Pass atlas UVs to fragment shader
    output.uvMin = input.uvMin;
    output.uvMax = input.uvMax;
    output.uv = vec2<f32>(0.0, 0.0); // Not used directly anymore

    output.fgColor = input.fgColor;
    output.bgColor = input.bgColor;
    output.cellUV = input.position;  // 0-1 position within cell

    return output;
}

// MSDF median function
fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Early out for empty glyphs (spaces) - prevents division by zero
    let hasGlyph = input.glyphBoundsMax.x > input.glyphBoundsMin.x &&
                   input.glyphBoundsMax.y > input.glyphBoundsMin.y;
    if (!hasGlyph) {
        return vec4<f32>(input.bgColor.rgb, 1.0);
    }

    // Check if current pixel is within glyph bounds
    let inGlyph = input.cellUV.x >= input.glyphBoundsMin.x &&
                  input.cellUV.x <= input.glyphBoundsMax.x &&
                  input.cellUV.y >= input.glyphBoundsMin.y &&
                  input.cellUV.y <= input.glyphBoundsMax.y;

    if (!inGlyph) {
        return vec4<f32>(input.bgColor.rgb, 1.0);
    }

    // Map cellUV to atlas UV within glyph bounds
    let glyphLocalUV = (input.cellUV - input.glyphBoundsMin) / (input.glyphBoundsMax - input.glyphBoundsMin);
    let atlasUV = mix(input.uvMin, input.uvMax, glyphLocalUV);

    // Sample MSDF texture
    let msdf = textureSample(fontTexture, fontSampler, atlasUV);

    // Calculate signed distance
    let sd = median(msdf.r, msdf.g, msdf.b);

    // Calculate screen-space distance for anti-aliasing
    let screenTexSize = vec2<f32>(textureDimensions(fontTexture));
    let unitRange = uniforms.pixelRange / screenTexSize;
    // Use actual glyph size on screen, not cell size
    let scaledGlyphSize = (input.glyphBoundsMax - input.glyphBoundsMin) * uniforms.cellSize;
    let screenRange = max(scaledGlyphSize.x * unitRange.x, scaledGlyphSize.y * unitRange.y);

    // Apply anti-aliased edge - this is our alpha
    let alpha = clamp((sd - 0.5) * screenRange + 0.5, 0.0, 1.0);

    // Blend foreground over background - output is fully opaque
    let color = mix(input.bgColor.rgb, input.fgColor.rgb, alpha);
    return vec4<f32>(color, 1.0);
}

// Background quad shader - renders cell backgrounds
struct BgVertexInput {
    @location(0) position: vec2<f32>,    // Quad corner
    @location(1) cellPos: vec2<f32>,     // Grid cell position
    @location(2) bgColor: vec4<f32>,     // Background color
};

struct BgVertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) bgColor: vec4<f32>,
};

@vertex
fn vs_background(input: BgVertexInput) -> BgVertexOutput {
    var output: BgVertexOutput;

    let cellPixelPos = input.cellPos * uniforms.cellSize;
    let pixelPos = cellPixelPos + input.position * uniforms.cellSize;

    output.position = uniforms.projection * vec4<f32>(pixelPos, 0.0, 1.0);
    output.bgColor = input.bgColor;

    return output;
}

@fragment
fn fs_background(input: BgVertexOutput) -> @location(0) vec4<f32> {
    return input.bgColor;
}
