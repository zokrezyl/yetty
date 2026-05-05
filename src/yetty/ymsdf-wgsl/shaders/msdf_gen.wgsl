// MSDF Generation Compute Shader
// Based on msdfgen algorithm by Viktor Chlumsky

const PI: f32 = 3.14159265358979323846;
const INFINITY_F: f32 = 1e20;

// Color constants for edge coloring
const BLACK: u32 = 0u;
const RED: u32 = 1u;
const GREEN: u32 = 2u;
const BLUE: u32 = 4u;
const YELLOW: u32 = 3u;  // RED | GREEN
const MAGENTA: u32 = 5u; // BLUE | RED
const CYAN: u32 = 6u;    // BLUE | GREEN
const WHITE: u32 = 7u;   // RED | GREEN | BLUE

// Uniforms for current glyph
struct GlyphUniforms {
    atlas_offset: vec2<u32>,  // Where to write in atlas
    glyph_size: vec2<u32>,    // Size of glyph in pixels
    translate: vec2<f32>,     // Translation (padding offset)
    scale: f32,               // Scale factor
    range: f32,               // Distance field range
    meta_offset: u32,         // Offset into metadata buffer
    point_offset: u32,        // Offset into points buffer
    glyph_height: f32,        // Glyph height for Y flip
    _padding: u32,
};

@group(0) @binding(0) var<uniform> uniforms: GlyphUniforms;
@group(0) @binding(1) var<storage, read> metadata: array<u32>;
@group(0) @binding(2) var<storage, read> points: array<f32>;
@group(0) @binding(3) var output_texture: texture_storage_2d<rgba32float, write>;

// Get point from buffer (each point is 2 floats)
fn get_point(idx: u32) -> vec2<f32> {
    let base = idx * 2u;
    return vec2<f32>(points[base], points[base + 1u]);
}

// Get metadata value
fn get_meta(idx: u32) -> u32 {
    return metadata[idx];
}

// 2D cross product
fn cross2d(a: vec2<f32>, b: vec2<f32>) -> f32 {
    return a.x * b.y - a.y * b.x;
}

// Like sign() but never returns 0 — matches msdfgen's `nonZeroSign`.
// Used to determine the SDF sign from cross(tangent, origin-closest):
// when the cross product is *exactly* 0 (origin lies on the tangent line
// through a segment endpoint, common at axis-aligned wave/cap geometry),
// WGSL's sign() returns 0, making signed_dist evaluate to 0 even though
// the actual distance is non-zero. That hit min_abs_X to 0 *once* per
// channel and froze the channel at "right on the edge" (= rgba8 value
// 127) for every pixel in that row, producing the visible horizontal
// stripe across glyphs like ~ / ⌒ / U+25EE.
fn nz_sign(x: f32) -> f32 {
    if x >= 0.0 { return 1.0; }
    return -1.0;
}

// Signed distance to a line segment.
// Convention: negative = inside (left of CCW tangent), positive = outside.
//
// We *don't* apply pseudo-distance here. msdfgen does — it gives smoother
// AA at corners — but it relies on msdfgen's separate per-pixel error
// correction pass to catch the near-edge values pseudo otherwise leaks
// into far-outside pixels along tangent extensions. The sign-only
// winding correction we have in main() catches sign flips, not magnitude
// leaks, so adding pseudo-distance without proper error correction
// makes the MSDF *worse* (verified empirically — bad_pct went 0.65→5%).
fn distance_to_line(p0: vec2<f32>, p1: vec2<f32>, origin: vec2<f32>) -> vec3<f32> {
    let aq = origin - p0;
    let ab = p1 - p0;
    let t = clamp(dot(aq, ab) / dot(ab, ab), 0.0, 1.0);
    let closest = p0 + t * ab;
    let to_origin = origin - closest;
    let sign_val = -nz_sign(cross2d(ab, to_origin));
    return vec3<f32>(sign_val * length(to_origin), 0.0, t);
}

// Signed distance to a quadratic bezier curve
fn distance_to_quad(p0: vec2<f32>, p1: vec2<f32>, p2: vec2<f32>, origin: vec2<f32>) -> vec3<f32> {
    let qa = p0 - origin;
    let ab = p1 - p0;
    let br = p2 - p1 - ab;

    let a = dot(br, br);
    let b = 3.0 * dot(ab, br);
    let c = 2.0 * dot(ab, ab) + dot(qa, br);
    let d = dot(qa, ab);

    // Solve cubic equation for closest point parameter
    var solutions: array<f32, 3>;
    var num_solutions = 0;

    if abs(a) > 1e-10 {
        let _a = b / a;
        let a2 = _a * _a;
        let q = (a2 - 3.0 * (c / a)) / 9.0;
        let r = (_a * (2.0 * a2 - 9.0 * (c / a)) + 27.0 * (d / a)) / 54.0;
        let r2 = r * r;
        let q3 = q * q * q;
        let a_div_3 = _a / 3.0;

        if r2 < q3 {
            let t = acos(clamp(r / sqrt(q3), -1.0, 1.0));
            let q_neg = -2.0 * sqrt(q);
            solutions[0] = q_neg * cos(t / 3.0) - a_div_3;
            solutions[1] = q_neg * cos((t + 2.0 * PI) / 3.0) - a_div_3;
            solutions[2] = q_neg * cos((t - 2.0 * PI) / 3.0) - a_div_3;
            num_solutions = 3;
        } else {
            var A = -pow(abs(r) + sqrt(max(r2 - q3, 0.0)), 1.0 / 3.0);
            if r < 0.0 { A = -A; }
            var B = 0.0;
            if abs(A) > 1e-10 { B = q / A; }
            solutions[0] = (A + B) - a_div_3;
            solutions[1] = -0.5 * (A + B) - a_div_3;
            let imag = 0.5 * sqrt(3.0) * (A - B);
            if abs(imag) < 1e-10 {
                num_solutions = 2;
            } else {
                num_solutions = 1;
            }
        }
    } else if abs(b) > 1e-10 {
        // Quadratic case
        let disc = c * c - 4.0 * b * d;
        if disc >= 0.0 {
            let sq = sqrt(disc);
            solutions[0] = (-c + sq) / (2.0 * b);
            solutions[1] = (-c - sq) / (2.0 * b);
            num_solutions = 2;
        }
    } else if abs(c) > 1e-10 {
        solutions[0] = -d / c;
        num_solutions = 1;
    }

    // Find min |true distance| with the sign convention
    //   sign = -nz_sign(cross(tangent, origin-closest))
    // — same as distance_to_line. Pseudo-distance intentionally omitted
    // (see distance_to_line) — would need msdfgen's full per-pixel
    // error-correction pass to compose safely.
    let aq = -qa;                 // origin - p0
    var min_dist = -nz_sign(cross2d(ab, aq)) * length(qa);
    var param = 0.0;

    let qc = p2 - origin;
    let dist_end = -nz_sign(cross2d(p2 - p1, -qc)) * length(qc);
    if abs(dist_end) < abs(min_dist) {
        min_dist = dist_end;
        param = 1.0;
    }

    for (var i = 0; i < num_solutions; i = i + 1) {
        let t = solutions[i];
        if t > 0.0 && t < 1.0 {
            // B(t) = p0 + 2*t*ab + t^2*br ;  B'(t)/2 = ab + t*br
            let point_on_curve = p0 + ab * 2.0 * t + br * t * t;
            let to_origin = origin - point_on_curve;
            let tangent = ab + br * t;
            let dist = -nz_sign(cross2d(tangent, to_origin)) * length(to_origin);
            if abs(dist) < abs(min_dist) {
                min_dist = dist;
                param = t;
            }
        }
    }

    return vec3<f32>(min_dist, 0.0, param);
}

// Get direction of segment at parameter t
fn segment_direction(point_idx: u32, npoints: u32, t: f32) -> vec2<f32> {
    if npoints == 2u {
        let p0 = get_point(point_idx);
        let p1 = get_point(point_idx + 1u);
        return p1 - p0;
    } else {
        let p0 = get_point(point_idx);
        let p1 = get_point(point_idx + 1u);
        let p2 = get_point(point_idx + 2u);
        let tangent = mix(p1 - p0, p2 - p1, t);
        if dot(tangent, tangent) < 1e-10 {
            return p2 - p0;
        }
        return tangent;
    }
}

// Main MSDF calculation
@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) global_id: vec3<u32>) {
    let pixel_x = global_id.x;
    let pixel_y = global_id.y;

    // Check bounds
    if pixel_x >= uniforms.glyph_size.x || pixel_y >= uniforms.glyph_size.y {
        return;
    }

    // Calculate position in glyph space
    let pixel_pos = vec2<f32>(f32(pixel_x) + 0.5, f32(pixel_y) + 0.5);
    var p = (pixel_pos / uniforms.scale) - uniforms.translate;
    p.y = (uniforms.glyph_height / uniforms.scale) - p.y;

    // Per-channel signed distance + abs-distance for nearest-segment selection.
    var min_dist_r = INFINITY_F;
    var min_dist_g = INFINITY_F;
    var min_dist_b = INFINITY_F;
    var min_abs_r = INFINITY_F;
    var min_abs_g = INFINITY_F;
    var min_abs_b = INFINITY_F;

    // Plain SDF magnitude (every segment counts, ignoring the per-channel
    // colour filter). This is used together with the winding number below
    // to produce a geometrically-correct *reference* signed distance —
    // independent of the per-segment cross-product sign convention, which
    // gets confused for pixels past a segment endpoint along its tangent
    // extension and produces the wedge artefacts you see at C/G/J/S
    // openings. The winding-based reference then drives error-correction:
    // pixels where the MSDF median3 disagrees with the reference sign get
    // their RGB overwritten with the reference, killing the wedges
    // without losing the per-channel anti-aliasing on correctly-signed
    // pixels.
    var min_abs_sdf = INFINITY_F;
    // Signed crossings of the +x ray from `p` with each contour segment.
    // Non-zero ⇒ pixel is geometrically inside the shape.
    var winding_count = 0;

    // Read glyph structure from metadata
    var meta_idx = uniforms.meta_offset;
    var point_idx = uniforms.point_offset;

    let ncontours = get_meta(meta_idx);
    meta_idx = meta_idx + 1u;

    // Process each contour
    for (var contour_i = 0u; contour_i < ncontours; contour_i = contour_i + 1u) {
        let winding = i32(get_meta(meta_idx)) - 1;
        meta_idx = meta_idx + 1u;
        let nsegments = get_meta(meta_idx);
        meta_idx = meta_idx + 1u;

        if nsegments == 0u {
            continue;
        }

        // Skip degenerate contours
        let first_npoints = get_meta(meta_idx + 1u);
        if nsegments == 1u && first_npoints == 2u {
            point_idx = point_idx + 2u;
            meta_idx = meta_idx + 2u;
            continue;
        }

        // Process each segment
        let segment_meta_start = meta_idx;
        let segment_point_start = point_idx;

        for (var seg_i = 0u; seg_i < nsegments; seg_i = seg_i + 1u) {
            let color = get_meta(meta_idx);
            meta_idx = meta_idx + 1u;
            let npoints = get_meta(meta_idx);
            meta_idx = meta_idx + 1u;

            // Calculate distance to this segment
            var d: vec3<f32>;
            if npoints == 2u {
                let p0 = get_point(point_idx);
                let p1 = get_point(point_idx + 1u);
                d = distance_to_line(p0, p1, p);
            } else {
                let p0 = get_point(point_idx);
                let p1 = get_point(point_idx + 1u);
                let p2 = get_point(point_idx + 2u);
                d = distance_to_quad(p0, p1, p2, p);
            }

            let signed_dist = d.x;
            let abs_dist = abs(signed_dist);

            if (color & RED) != 0u {
                if abs_dist < min_abs_r {
                    min_abs_r = abs_dist;
                    min_dist_r = signed_dist;
                }
            }
            if (color & GREEN) != 0u {
                if abs_dist < min_abs_g {
                    min_abs_g = abs_dist;
                    min_dist_g = signed_dist;
                }
            }
            if (color & BLUE) != 0u {
                if abs_dist < min_abs_b {
                    min_abs_b = abs_dist;
                    min_dist_b = signed_dist;
                }
            }
            if abs_dist < min_abs_sdf {
                min_abs_sdf = abs_dist;
            }

            // Winding contribution: signed crossings of the +x ray from
            // (p.x, ray_y) with this segment. We perturb the ray's y by
            // a sub-pixel irrational amount so the ray never coincides
            // exactly with a contour vertex — without that, a vertex
            // sitting on the integer pixel-grid produces ambiguous
            // counts (one segment "starts at the ray", the next "ends
            // at the ray", and the strict `>` gate flips state in a
            // way that depends on which root the cubic finds first),
            // visible as horizontal stripes one row deep on glyphs with
            // axis-aligned vertices (waves like ≈, U+25EE, …). The
            // perturbation only shifts the ray's notional y; the pixel
            // we're shading is unchanged, so distance & MSDF outputs
            // are bit-identical.
            let ray_y = p.y + 0.0007111111;
            //
            // Why endpoint-side gate, not "find roots in [0,1)":
            //   • A quadratic kissing the ray at its y-extremum has two
            //     roots that collapse together; with f32 they slip just
            //     above or just below 0 unpredictably and the [0,1) loop
            //     either double-counts or misses, producing horizontal
            //     stripes at exactly the y-row of the extremum (visible
            //     on glyphs with sinusoidal motifs: ≈, ⊁, ▷, U+25EE…).
            //   • Curves where both endpoints are on the same side but
            //     the curve dips through the ray contribute 2 crossings
            //     of *opposite* y-direction that cancel — net 0, same as
            //     skipping. So gating on endpoint sides is mathematically
            //     equivalent for those cases too.
            //   • Shared-endpoint vertices that happen to land exactly on
            //     the ray collapse to false on `> ray` (strict greater),
            //     so a single vertex hit is counted once across the two
            //     sharing segments — never zero, never double.
            if npoints == 2u {
                let p0 = get_point(point_idx);
                let p1 = get_point(point_idx + 1u);
                let above0 = p0.y > ray_y;
                let above1 = p1.y > ray_y;
                if above0 != above1 {
                    let dy = p1.y - p0.y;
                    let t = (ray_y - p0.y) / dy;
                    let xi = p0.x + t * (p1.x - p0.x);
                    if xi > p.x {
                        if above1 { winding_count = winding_count + 1; }
                        else      { winding_count = winding_count - 1; }
                    }
                }
            } else {
                let p0 = get_point(point_idx);
                let p1 = get_point(point_idx + 1u);
                let p2 = get_point(point_idx + 2u);
                let above0 = p0.y > ray_y;
                let above2 = p2.y > ray_y;
                if above0 != above2 {
                    // Endpoints differ ⇒ exactly one crossing in [0,1].
                    let A = p0.y - 2.0 * p1.y + p2.y;
                    let B = 2.0 * (p1.y - p0.y);
                    let C = p0.y - ray_y;
                    var t_cross = -1.0;
                    if abs(A) < 1e-10 {
                        if abs(B) > 1e-10 { t_cross = -C / B; }
                    } else {
                        let disc = B * B - 4.0 * A * C;
                        if disc >= 0.0 {
                            let sq = sqrt(disc);
                            let t0 = (-B - sq) / (2.0 * A);
                            let t1 = (-B + sq) / (2.0 * A);
                            if t0 >= 0.0 && t0 <= 1.0 { t_cross = t0; }
                            if t1 >= 0.0 && t1 <= 1.0 { t_cross = t1; }
                        }
                    }
                    if t_cross >= 0.0 && t_cross <= 1.0 {
                        let omt = 1.0 - t_cross;
                        let xi = omt*omt*p0.x + 2.0*t_cross*omt*p1.x +
                                 t_cross*t_cross*p2.x;
                        if xi > p.x {
                            if above2 { winding_count = winding_count + 1; }
                            else      { winding_count = winding_count - 1; }
                        }
                    }
                }
            }

            point_idx = point_idx + npoints - 1u;
        }
        point_idx = point_idx + 1u;
    }

    // Handle case where no edges were found for a channel (use overall minimum)
    let min_overall = min(min(min_abs_r, min_abs_g), min_abs_b);
    if min_abs_r >= INFINITY_F { min_dist_r = select(-min_overall, min_overall, min_dist_g > 0.0 || min_dist_b > 0.0); }
    if min_abs_g >= INFINITY_F { min_dist_g = select(-min_overall, min_overall, min_dist_r > 0.0 || min_dist_b > 0.0); }
    if min_abs_b >= INFINITY_F { min_dist_b = select(-min_overall, min_overall, min_dist_r > 0.0 || min_dist_g > 0.0); }

    // ── MSDF error correction (winding-based) ─────────────────────────
    // Use the winding number for the geometrically-correct sign:
    //   winding != 0  ⇒ pixel is inside the shape (sign convention here:
    //                   negative)
    //   winding == 0  ⇒ outside (positive).
    // Then build a reference signed distance from |min_abs_sdf| and that
    // sign. When median3(R,G,B) disagrees with the reference sign, force
    // RGB := reference so the rendered pixel matches the geometry. This
    // catches the wedge artefacts where every per-segment cross-product
    // sign agrees on the *wrong* side because the closest point is at a
    // segment endpoint and the pixel is past the tangent extension —
    // exactly the failure mode pseudo-distance is meant to handle.
    let median_signed = max(min(min_dist_r, min_dist_g),
                            min(max(min_dist_r, min_dist_g), min_dist_b));
    // FreeType returns TTF outlines in CW orientation, and the shader's
    // distance functions end up with: signed_dist > 0 ⇒ inside (renders
    // bright after `signed/range + 0.5`). So inside pixels (winding_count
    // non-zero) want a *positive* reference, outside (winding 0) negative.
    // WGSL `select(a, b, cond)` returns b when cond is true.
    let inside = winding_count != 0;
    let ref_sign = select(-1.0, 1.0, inside);
    let reference_sdf = ref_sign * min_abs_sdf;
    if min_abs_sdf < INFINITY_F && median_signed * reference_sdf < 0.0 {
        min_dist_r = reference_sdf;
        min_dist_g = reference_sdf;
        min_dist_b = reference_sdf;
    }

    // Normalize to 0-1 range centered at 0.5
    let normalized = vec3<f32>(
        min_dist_r / uniforms.range + 0.5,
        min_dist_g / uniforms.range + 0.5,
        min_dist_b / uniforms.range + 0.5
    );

    // Write to atlas
    let atlas_pos = vec2<i32>(
        i32(uniforms.atlas_offset.x + pixel_x),
        i32(uniforms.atlas_offset.y + pixel_y)
    );

    textureStore(output_texture, atlas_pos, vec4<f32>(normalized, 1.0));
}
