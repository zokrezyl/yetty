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

// Signed distance to a line segment.
// Convention: negative = inside (left of CCW tangent), positive = outside.
//
// Returns vec3<f32>(signed_dist, _unused, t):
//   signed_dist  signed Euclidean distance to the segment (closest point in [0,1])
//   _unused      reserved (was orthogonality, now unused by main)
//   t            clamped projection along ab in [0,1]
//
// Pseudo-distance correction (msdfgen's
// EdgeSegment::distanceToPseudoDistance) is intentionally NOT applied
// here. Without msdfgen's per-pixel error-correction pass it leaks
// near-edge pseudo values into far-outside pixels along tangent
// extensions and produces a *worse* MSDF than plain true-distance.
fn distance_to_line(p0: vec2<f32>, p1: vec2<f32>, origin: vec2<f32>) -> vec3<f32> {
    let aq = origin - p0;
    let ab = p1 - p0;
    let t = clamp(dot(aq, ab) / dot(ab, ab), 0.0, 1.0);
    let closest = p0 + t * ab;
    let to_origin = origin - closest;
    let sign_val = -sign(cross2d(ab, to_origin));
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

    // Find min distance with sign convention -sign(cross(tangent, origin-closest))
    // — same convention as distance_to_line and the interior case below.
    // The original p0 sign expression `-sign(cross(ab, qa))` was inverted
    // relative to that convention; that flipped sign at any pixel whose
    // closest point on a quad segment was its t=0 endpoint, producing the
    // visible MSDF artefacts at corners where one segment's t=0 met the
    // previous segment's t=1 on a closed contour.
    let aq = -qa;                 // origin - p0
    var min_dist = -sign(cross2d(ab, aq)) * length(qa);
    var param = 0.0;

    let qc = p2 - origin;
    let dist_end = -sign(cross2d(p2 - p1, -qc)) * length(qc);
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
            let dist = -sign(cross2d(tangent, to_origin)) * length(to_origin);
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

    // Initialize distances for R, G, B channels
    var min_dist_r = INFINITY_F;
    var min_dist_g = INFINITY_F;
    var min_dist_b = INFINITY_F;

    // Track minimum absolute distance for each channel
    var min_abs_r = INFINITY_F;
    var min_abs_g = INFINITY_F;
    var min_abs_b = INFINITY_F;

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

            point_idx = point_idx + npoints - 1u;
        }
        point_idx = point_idx + 1u;
    }

    // Handle case where no edges were found for a channel (use overall minimum)
    let min_overall = min(min(min_abs_r, min_abs_g), min_abs_b);
    if min_abs_r >= INFINITY_F { min_dist_r = select(-min_overall, min_overall, min_dist_g > 0.0 || min_dist_b > 0.0); }
    if min_abs_g >= INFINITY_F { min_dist_g = select(-min_overall, min_overall, min_dist_r > 0.0 || min_dist_b > 0.0); }
    if min_abs_b >= INFINITY_F { min_dist_b = select(-min_overall, min_overall, min_dist_r > 0.0 || min_dist_g > 0.0); }

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
