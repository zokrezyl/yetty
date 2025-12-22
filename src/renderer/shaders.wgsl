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

    // Calculate glyph position within cell
    let scaledGlyphSize = input.glyphSize * uniforms.scale;
    let scaledBearing = input.glyphBearing * uniforms.scale;

    let cellPixelPos = input.cellPos * uniforms.cellSize;
    let baseline = uniforms.cellSize.y * 0.8;
    let glyphTop = baseline - scaledBearing.y;
    let glyphLeft = scaledBearing.x;

    // GLYPH-SIZED quad positioned within the cell
    let glyphOffset = vec2<f32>(glyphLeft, glyphTop);
    let localPos = input.position * scaledGlyphSize;
    let pixelPos = cellPixelPos + glyphOffset + localPos;

    output.position = uniforms.projection * vec4<f32>(pixelPos, 0.0, 1.0);

    // UV interpolation across the glyph quad
    output.uv = mix(input.uvMin, input.uvMax, input.position);
    output.uvMin = input.uvMin;
    output.uvMax = input.uvMax;

    output.fgColor = input.fgColor;
    output.bgColor = input.bgColor;
    output.cellUV = input.position;
    output.glyphBoundsMin = vec2<f32>(0.0);
    output.glyphBoundsMax = vec2<f32>(1.0);

    return output;
}

// MSDF median function
fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    // Sample MSDF texture using interpolated UV
    let msdf = textureSample(fontTexture, fontSampler, input.uv);

    // Calculate signed distance
    let sd = median(msdf.r, msdf.g, msdf.b);

    // screenPxRange = pixelRange * (glyphScreenSize / glyphAtlasSize)
    // For sharp edges, we need adequate range. Using pixelRange directly works well.
    let screenPxRange = uniforms.pixelRange;

    // Apply anti-aliased edge - this is the alpha
    let alpha = clamp((sd - 0.5) * screenPxRange + 0.5, 0.0, 1.0);

    // Output foreground color with alpha - will blend over background
    return vec4<f32>(input.fgColor.rgb, alpha);
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
