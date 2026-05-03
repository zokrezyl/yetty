// Compute shader: Scale image to target dimensions
//
// Reads from source buffer, scales with bilinear interpolation,
// writes to destination buffer.
//
// Uniforms:
//   srcWidth, srcHeight: source image dimensions
//   dstWidth, dstHeight: destination (scaled) dimensions
//   srcOffset: byte offset in source buffer
//   dstOffset: byte offset in destination buffer

struct ScaleParams {
    srcWidth: u32,
    srcHeight: u32,
    dstWidth: u32,
    dstHeight: u32,
    srcOffset: u32,  // byte offset in source buffer
    dstOffset: u32,  // byte offset in destination buffer
}

@group(0) @binding(0) var<uniform> params: ScaleParams;
@group(0) @binding(1) var<storage, read> srcBuffer: array<u32>;
@group(0) @binding(2) var<storage, read_write> dstBuffer: array<u32>;

// Bilinear sample from source buffer
fn sampleBilinear(uv: vec2<f32>) -> vec4<f32> {
    let srcW = params.srcWidth;
    let srcH = params.srcHeight;
    let baseOffset = params.srcOffset / 4u;

    // Clamp UV
    let clampedUV = clamp(uv, vec2<f32>(0.0), vec2<f32>(1.0));

    // Convert to pixel coordinates
    let pixelCoord = clampedUV * vec2<f32>(f32(srcW) - 1.0, f32(srcH) - 1.0);

    // Integer and fractional parts
    let x0 = u32(floor(pixelCoord.x));
    let y0 = u32(floor(pixelCoord.y));
    let x1 = min(x0 + 1u, srcW - 1u);
    let y1 = min(y0 + 1u, srcH - 1u);
    let fx = fract(pixelCoord.x);
    let fy = fract(pixelCoord.y);

    // Sample 4 pixels
    let c00 = srcBuffer[baseOffset + y0 * srcW + x0];
    let c10 = srcBuffer[baseOffset + y0 * srcW + x1];
    let c01 = srcBuffer[baseOffset + y1 * srcW + x0];
    let c11 = srcBuffer[baseOffset + y1 * srcW + x1];

    // Unpack RGBA8
    let color00 = vec4<f32>(
        f32(c00 & 0xFFu) / 255.0,
        f32((c00 >> 8u) & 0xFFu) / 255.0,
        f32((c00 >> 16u) & 0xFFu) / 255.0,
        f32((c00 >> 24u) & 0xFFu) / 255.0
    );
    let color10 = vec4<f32>(
        f32(c10 & 0xFFu) / 255.0,
        f32((c10 >> 8u) & 0xFFu) / 255.0,
        f32((c10 >> 16u) & 0xFFu) / 255.0,
        f32((c10 >> 24u) & 0xFFu) / 255.0
    );
    let color01 = vec4<f32>(
        f32(c01 & 0xFFu) / 255.0,
        f32((c01 >> 8u) & 0xFFu) / 255.0,
        f32((c01 >> 16u) & 0xFFu) / 255.0,
        f32((c01 >> 24u) & 0xFFu) / 255.0
    );
    let color11 = vec4<f32>(
        f32(c11 & 0xFFu) / 255.0,
        f32((c11 >> 8u) & 0xFFu) / 255.0,
        f32((c11 >> 16u) & 0xFFu) / 255.0,
        f32((c11 >> 24u) & 0xFFu) / 255.0
    );

    // Bilinear interpolation
    let top = mix(color00, color10, fx);
    let bottom = mix(color01, color11, fx);
    return mix(top, bottom, fy);
}

// Pack vec4 color to u32 RGBA8
fn packRGBA8(color: vec4<f32>) -> u32 {
    let r = u32(clamp(color.r * 255.0, 0.0, 255.0));
    let g = u32(clamp(color.g * 255.0, 0.0, 255.0));
    let b = u32(clamp(color.b * 255.0, 0.0, 255.0));
    let a = u32(clamp(color.a * 255.0, 0.0, 255.0));
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let x = gid.x;
    let y = gid.y;

    // Bounds check
    if (x >= params.dstWidth || y >= params.dstHeight) {
        return;
    }

    // Calculate UV in source image
    let uv = vec2<f32>(
        (f32(x) + 0.5) / f32(params.dstWidth),
        (f32(y) + 0.5) / f32(params.dstHeight)
    );

    // Sample with bilinear interpolation
    let color = sampleBilinear(uv);

    // Write to destination
    let dstBaseOffset = params.dstOffset / 4u;
    let dstIndex = dstBaseOffset + y * params.dstWidth + x;
    dstBuffer[dstIndex] = packRGBA8(color);
}
