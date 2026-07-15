// YPlot Complex Primitive Shader
// Renders plot from uniform values and a self-describing storage buffer.
//
// Architecture:
//   Uniforms (binding 0):       bounds, ranges, flags, function_count, colors
//   Storage  (binding 1):       bytecode + variable-count data buffers
//
// The shader walks the storage layout at runtime — no per-instance constants
// are baked into the WGSL, so adding/removing/resizing data buffers never
// triggers a pipeline recompile.

const YPLOT_FLAG_GRID:   u32 = 1u;
const YPLOT_FLAG_AXES:   u32 = 2u;
const YPLOT_FLAG_LABELS: u32 = 4u;
const YPLOT_FLAG_FIELD:  u32 = 16u;  // f(x,y) heatmap instead of a line curve
const YPLOT_FLAG_XLOG:   u32 = 32u;  // base-10 log x axis (range is positive)
const YPLOT_FLAG_YLOG:   u32 = 64u;  // base-10 log y axis (range is positive)

fn yplot_log10(value: f32) -> f32 {
    return log2(value) * 0.3010299957;
}

// Perceptually-uniform colormaps — Matt Zucker's 6th-order polynomial
// fits of the matplotlib maps. Input clamped to [0,1]. Selected per
// instance by the colormap_id uniform (0 viridis, 1 plasma, 2 magma,
// 3 inferno). The C twin lives in yplot.c (yplot_colormap_sample) and
// paints the client-side colorbar — keep the coefficients in sync.
fn yplot_colormap_poly(t: f32, c0: vec3<f32>, c1: vec3<f32>, c2: vec3<f32>,
                       c3: vec3<f32>, c4: vec3<f32>, c5: vec3<f32>,
                       c6: vec3<f32>) -> vec3<f32> {
    let rgb = c0 + t * (c1 + t * (c2 + t * (c3 + t * (c4 + t * (c5 + t * c6)))));
    return clamp(rgb, vec3<f32>(0.0), vec3<f32>(1.0));
}

fn yplot_colormap(t_in: f32, colormap_id: u32) -> vec3<f32> {
    let t = clamp(t_in, 0.0, 1.0);
    switch colormap_id {
        case 1u: { // plasma
            return yplot_colormap_poly(t,
                vec3<f32>(0.05873234392399702,  0.02333670892565664,  0.5433401826748754),
                vec3<f32>(2.176514634195958,    0.2383834171260182,   0.7539604599784036),
                vec3<f32>(-2.689460476458034,  -7.455851135738909,    3.110799939717086),
                vec3<f32>(6.130348345893603,   42.3461881477227,    -28.51885465332158),
                vec3<f32>(-11.10743619062271, -82.66631109428045,    60.13984767418263),
                vec3<f32>(10.02306557647065,   71.41361770095349,   -54.07218655560067),
                vec3<f32>(-3.658713842777788, -22.93153465461149,    18.19190778539828));
        }
        case 2u: { // magma
            return yplot_colormap_poly(t,
                vec3<f32>(-0.002136485053939582, -0.000749655052795221, -0.005386127855323933),
                vec3<f32>(0.2516605407371642,     0.6775232436837668,    2.494026599312351),
                vec3<f32>(8.353717279216625,     -3.577719514958484,     0.3144679030132573),
                vec3<f32>(-27.66873308576866,    14.26473078096533,    -13.64921318813922),
                vec3<f32>(52.17613981234068,    -27.94360607168351,     12.94416944238394),
                vec3<f32>(-50.76852536473588,    29.04658282127291,      4.23415299384598),
                vec3<f32>(18.65570506591883,    -11.48977351997711,     -5.601961508734096));
        }
        case 3u: { // inferno
            return yplot_colormap_poly(t,
                vec3<f32>(0.0002189403691192265, 0.001651004631001012, -0.01948089843709184),
                vec3<f32>(0.1065134194856116,    0.5639564367884091,    3.932712388889277),
                vec3<f32>(11.60249308247187,    -3.972853965665698,   -15.9423941062914),
                vec3<f32>(-41.70399613139459,   17.43639888205313,     44.35414519872813),
                vec3<f32>(77.162935699427,     -33.40235894210092,    -81.80730925738993),
                vec3<f32>(-71.31942824499214,   32.62606426397723,     73.20951985803202),
                vec3<f32>(25.13112622477341,   -12.24266895238567,    -23.07032500287172));
        }
        default: { // viridis
            return yplot_colormap_poly(t,
                vec3<f32>(0.2777273272234177,   0.005407344544966578,  0.3340998053353061),
                vec3<f32>(0.1050930431085774,   1.404613529898575,     1.384590162594685),
                vec3<f32>(-0.3308618287255563,  0.214847559468213,     0.09509516302823659),
                vec3<f32>(-4.634230498983486,  -5.799100973351585,   -19.33244095627987),
                vec3<f32>(6.228269936347081,   14.17993336680509,     56.69055260068105),
                vec3<f32>(4.776384997670288,  -13.74514537774601,    -65.35303263337234),
                vec3<f32>(-5.435455855934631,   4.645852612178535,    26.3124352495832));
        }
    }
}

// Plot chrome rides the brand palette: near-black canvas, teal-tinted
// separators, mint axes. Curve colours are supplied per-function and are
// left untouched.
const YPLOT_BG_COLOR:   vec3<f32> = vec3<f32>(0.043, 0.063, 0.078); // BRAND_BG       #0B1014
const YPLOT_GRID_COLOR: vec3<f32> = vec3<f32>(0.212, 0.290, 0.278); // BRAND_BORDER   #364A47
const YPLOT_AXIS_COLOR: vec3<f32> = vec3<f32>(0.420, 0.659, 0.573); // BRAND_ACCENT   #6BA892 (mint)

// Max curves the shader will iterate. Generous static bound for WGSL —
// the actual count is read from data; this just stops the loop from being
// unbounded. 16 ≫ any realistic mix of expressions + buffers.
const YPLOT_MAX_CURVES: u32 = 16u;

fn yplot_unpack_color(packed: u32) -> vec3<f32> {
    return vec3<f32>(
        f32((packed >> 16u) & 0xFFu) / 255.0,
        f32((packed >>  8u) & 0xFFu) / 255.0,
        f32( packed         & 0xFFu) / 255.0
    );
}

// Distance in PIXELS from this fragment to the nearest grid line along one
// axis. Three regimes:
//   log axis        — lines at decade boundaries of the data value
//   step > 0        — lines at multiples of `step` in data units (matches
//                     the client-side tick labels exactly)
//   step == 0       — legacy fixed 10-division fallback (no tick info)
// `uv` is the fragment's fraction along the axis, `range_lo`/`range_hi` the
// axis range, `extent_px` the plot's pixel extent along the axis.
fn yplot_grid_distance_px(uv: f32, range_lo: f32, range_hi: f32, step: f32,
                          is_log: bool, extent_px: f32) -> f32 {
    // The y axis passes an INVERTED range (lo = yMax at uv 0), so the
    // span must be taken as an absolute value.
    if (is_log) {
        let log_lo = yplot_log10(range_lo);
        let log_hi = yplot_log10(range_hi);
        let log_pos = mix(log_lo, log_hi, uv);
        let dist_decades = abs(log_pos - round(log_pos));
        return dist_decades / max(abs(log_hi - log_lo), 1.0e-6) * extent_px;
    }
    if (step > 0.0) {
        let value = mix(range_lo, range_hi, uv);
        let steps = value / step;
        let dist_units = abs(steps - round(steps)) * step;
        return dist_units / max(abs(range_hi - range_lo), 1.0e-12) * extent_px;
    }
    let cell = fract(uv * 10.0);
    return min(cell, 1.0 - cell) * 0.1 * extent_px;
}

fn yplot_draw_grid(bg: vec3<f32>, plotUV: vec2<f32>,
                   xMin: f32, xMax: f32, yMin: f32, yMax: f32,
                   x_step: f32, y_step: f32, x_log: bool, y_log: bool,
                   bounds_w: f32, bounds_h: f32) -> vec3<f32> {
    var color = bg;
    let dist_x = yplot_grid_distance_px(plotUV.x, xMin, xMax, x_step, x_log, bounds_w);
    // plotUV.y grows downward; the grid is symmetric so orientation is moot.
    let dist_y = yplot_grid_distance_px(plotUV.y, yMax, yMin, y_step, y_log, bounds_h);
    let half_px = 0.75;
    if (dist_x < half_px || dist_y < half_px) {
        color = mix(color, YPLOT_GRID_COLOR, 0.5);
    }
    return color;
}

fn yplot_draw_axes(bg: vec3<f32>, plotUV: vec2<f32>,
                   xMin: f32, xMax: f32, yMin: f32, yMax: f32,
                   x_log: bool, y_log: bool,
                   bounds_w: f32, bounds_h: f32) -> vec3<f32> {
    var color = bg;
    // Thickness in PIXELS, not plot-UV — a UV width makes the vertical axis
    // fat on wide plots and the horizontal axis thin on short ones. Half-width
    // 1.0 → a crisp ~2px line at any plot size.
    let halfPx = 1.0;

    // Y-axis at x=0. Zero is never on a log axis, so skip the line there.
    if (!x_log) {
        let xZero = (0.0 - xMin) / (xMax - xMin);
        if (xZero >= 0.0 && xZero <= 1.0 && abs(plotUV.x - xZero) * bounds_w < halfPx) {
            color = YPLOT_AXIS_COLOR;
        }
    }

    // X-axis at y=0. plotUV.y = 0 is the top (= yMax), so the row for data
    // y=0 is measured from yMax down, matching the curve mapping.
    if (!y_log) {
        let yZeroRow = (yMax - 0.0) / (yMax - yMin);
        if (yZeroRow >= 0.0 && yZeroRow <= 1.0 && abs(plotUV.y - yZeroRow) * bounds_h < halfPx) {
            color = YPLOT_AXIS_COLOR;
        }
    }

    // Border
    if (plotUV.x * bounds_w < halfPx || (1.0 - plotUV.x) * bounds_w < halfPx ||
        plotUV.y * bounds_h < halfPx || (1.0 - plotUV.y) * bounds_h < halfPx) {
        color = YPLOT_AXIS_COLOR * 0.7;
    }

    return color;
}

// Anti-aliased horizontal line at y == yNorm (in plot UV). Returns the
// blended foreground over `color`. Shared by expression and buffer paths.
fn yplot_line_blend(color: vec3<f32>, plotUV_y: f32, yNorm: f32,
                    lineWidth: f32, curveColor: vec3<f32>) -> vec3<f32> {
    let dist = abs(plotUV_y - yNorm);
    if (dist < lineWidth) {
        let alpha = 1.0 - dist / lineWidth;
        return mix(color, curveColor, alpha);
    }
    return color;
}

// Row fraction (plot UV, 0 = top) a data value maps to, honouring the y
// scale. On a log axis a non-positive value has no row — return a far
// off-screen sentinel so yplot_line_blend never matches it.
fn yplot_value_row(value: f32, yMin: f32, yMax: f32, y_log: bool) -> f32 {
    if (y_log) {
        if (value <= 0.0) {
            return 1.0e9;
        }
        let log_min = yplot_log10(yMin);
        let log_max = yplot_log10(yMax);
        return (log_max - yplot_log10(value)) / max(log_max - log_min, 1.0e-6);
    }
    return (yMax - value) / (yMax - yMin);
}

// Main yplot render function - called by ydraw dispatcher.
fn yplot_render(local_pos: vec2<f32>) -> vec4<f32> {
    // `local_pos` is ALREADY relative to the plot's top-left — fs_main has
    // subtracted bounds_x/y and discarded fragments outside [0..bounds_w] ×
    // [0..bounds_h]. Do NOT re-subtract bounds_xy here. Doing so was the
    // historic "second plot invisible" bug.
    let bounds_w = yplot_get_bounds_w();
    let bounds_h = yplot_get_bounds_h();

    let plotUV = local_pos / vec2<f32>(bounds_w, bounds_h);

    let flags      = yplot_get_flags();
    let func_count = yplot_get_function_count();
    let xMin       = yplot_get_x_min();
    let xMax       = yplot_get_x_max();
    let yMin       = yplot_get_y_min();
    let yMax       = yplot_get_y_max();

    let xLog = (flags & YPLOT_FLAG_XLOG) != 0u;
    let yLog = (flags & YPLOT_FLAG_YLOG) != 0u;

    var dataX = mix(xMin, xMax, plotUV.x);
    if (xLog) {
        dataX = pow(10.0, mix(yplot_log10(xMin), yplot_log10(xMax), plotUV.x));
    }
    // plotUV.y grows downward, so the top row (y=0) maps to yMax.
    var dataY = mix(yMax, yMin, plotUV.y);
    if (yLog) {
        dataY = pow(10.0, mix(yplot_log10(yMax), yplot_log10(yMin), plotUV.y));
    }
    let lineWidth = 3.0 / bounds_h;
    let isField = (flags & YPLOT_FLAG_FIELD) != 0u;

    // -------- pre-sample data buffers into the sampler array ---------------
    // Expressions that reference `f(x)` (where f was declared as `f=buffer`)
    // compile to LOAD_S against the buffer's slot. Walk the buffers ONCE,
    // fill samplers[i] with the linear-interpolated sample at plotUV.x.
    let data_count = yplot_data_count();
    var samplers: array<f32, 8>;
    samplers[0] = dataX; // legacy default for @buffer1 in expressions w/o data
    {
        var cursor: u32 = yplot_data_count_offset() + 1u;
        for (var bi = 0u; bi < YPLOT_MAX_CURVES; bi++) {
            if (bi >= data_count) { break; }
            let len = storage_buffer[cursor];
            let samples_off = cursor + 1u;
            if (len >= 2u && bi < 8u) {
                let idx_f   = plotUV.x * f32(len - 1u);
                var idx     = u32(floor(idx_f));
                var nxt     = min(idx + 1u, len - 1u);
                let t_lerp  = fract(idx_f);
                // Ring mode: display order starts at the head (the oldest
                // sample), so the newest is always at the right edge.
                let ring = yplot_get_ring_heads(bi);
                if (ring != 0u) {
                    let head = ring - 1u;
                    idx = (head + idx) % len;
                    nxt = (head + nxt) % len;
                }
                let v1 = bitcast<f32>(storage_buffer[samples_off + idx]);
                let v2 = bitcast<f32>(storage_buffer[samples_off + nxt]);
                let sv = mix(v1, v2, t_lerp);
                switch (bi) {
                    case 0u: { samplers[0] = sv; }
                    case 1u: { samplers[1] = sv; }
                    case 2u: { samplers[2] = sv; }
                    case 3u: { samplers[3] = sv; }
                    case 4u: { samplers[4] = sv; }
                    case 5u: { samplers[5] = sv; }
                    case 6u: { samplers[6] = sv; }
                    default: { samplers[7] = sv; }
                }
            }
            cursor = cursor + 1u + len;
        }
    }

    // -------- base layer: heatmap field OR background + grid/axes ----------
    var color = YPLOT_BG_COLOR;
    if (isField && func_count > 0u) {
        // 2D field f(x,y): evaluate function 0 per pixel and colormap it
        // over the configured field range (min == max means unset — use
        // the historical [-1, 1] mapping).
        let bc_off = yplot_bytecode_offset();
        let value = yfsvm_execute(bc_off, 0u, dataX, dataY, yplot_get_time(), samplers);
        var field_lo = yplot_get_field_min();
        var field_hi = yplot_get_field_max();
        if (field_lo == field_hi) {
            field_lo = -1.0;
            field_hi = 1.0;
        }
        let t = (value - field_lo) / (field_hi - field_lo);
        color = yplot_colormap(t, yplot_get_colormap_id());
    } else if ((flags & YPLOT_FLAG_GRID) != 0u) {
        color = yplot_draw_grid(color, plotUV, xMin, xMax, yMin, yMax,
                                yplot_get_x_step(), yplot_get_y_step(), xLog, yLog,
                                bounds_w, bounds_h);
    }
    if ((flags & YPLOT_FLAG_AXES) != 0u) {
        color = yplot_draw_axes(color, plotUV, xMin, xMax, yMin, yMax, xLog, yLog,
                                bounds_w, bounds_h);
    }

    // -------- uncertainty bands / whiskers ----------------------------------
    // band_slots[i] attaches an envelope (lo/hi data-buffer slots) to color
    // slot i: style 0 fills between the envelope curves with a translucent
    // wash of the slot color; style 1 draws whisker bars at decimated
    // sample positions. Drawn under the curves. Skipped in field mode.
    if (!isField) {
        for (var slot = 0u; slot < 8u; slot++) {
            let band = yplot_get_band_slots(slot);
            if (band == 0u) { continue; }
            let lo_slot = (band & 0xFFu) - 1u;
            let hi_slot = ((band >> 8u) & 0xFFu) - 1u;
            let style = (band >> 16u) & 0xFFu;
            if (lo_slot >= 8u || hi_slot >= 8u) { continue; }
            let lo_value = samplers[lo_slot];
            let hi_value = samplers[hi_slot];
            let lo_row = yplot_value_row(lo_value, yMin, yMax, yLog);
            let hi_row = yplot_value_row(hi_value, yMin, yMax, yLog);
            let row_top = min(lo_row, hi_row);
            let row_bottom = max(lo_row, hi_row);
            let band_color = yplot_unpack_color(yplot_get_colors(slot));

            if (style == 0u) {
                if (plotUV.y >= row_top && plotUV.y <= row_bottom) {
                    color = mix(color, band_color, 0.22);
                }
            } else {
                // Whiskers: a vertical bar spanning the envelope plus end
                // caps, at every k-th sample of the lo buffer, k chosen so
                // bars sit ~40 px apart.
                var sample_count = 0u;
                {
                    var cursor: u32 = yplot_data_count_offset() + 1u;
                    for (var bi = 0u; bi < YPLOT_MAX_CURVES; bi++) {
                        if (bi >= data_count) { break; }
                        let len = storage_buffer[cursor];
                        if (bi == lo_slot) { sample_count = len; break; }
                        cursor = cursor + 1u + len;
                    }
                }
                if (sample_count >= 2u) {
                    let bars_that_fit = max(bounds_w / 40.0, 1.0);
                    let stride = max(u32(ceil(f32(sample_count) / bars_that_fit)), 1u);
                    let index_f = plotUV.x * f32(sample_count - 1u);
                    let nearest = u32(round(index_f / f32(stride))) * stride;
                    if (nearest < sample_count) {
                        let bar_x_uv = f32(nearest) / f32(sample_count - 1u);
                        let bar_dist_px = abs(plotUV.x - bar_x_uv) * bounds_w;
                        let cap_half_px = 3.0;
                        let on_stem = bar_dist_px < 1.0 &&
                                      plotUV.y >= row_top && plotUV.y <= row_bottom;
                        let near_cap = bar_dist_px < cap_half_px &&
                                       (abs(plotUV.y - row_top) * bounds_h < 1.0 ||
                                        abs(plotUV.y - row_bottom) * bounds_h < 1.0);
                        if (on_stem || near_cap) {
                            color = mix(color, band_color, 0.8);
                        }
                    }
                }
            }
        }
    }

    // -------- expression curves (yfsvm bytecode) -----------------------------
    if (!isField && func_count > 0u) {
        // bytecode begins at storage word 1 (word 0 is bytecode_len).
        let bc_off = yplot_bytecode_offset();

        for (var fi = 0u; fi < min(func_count, 8u); fi++) {
            let curve_color = yplot_unpack_color(yplot_get_colors(fi));
            let y = yfsvm_execute(bc_off, fi, dataX, dataY, yplot_get_time(), samplers);
            // plotUV.y grows downward (0 = top = yMax), so a larger value maps
            // to a row nearer the top — measure the value's row from yMax down.
            let yRow = yplot_value_row(y, yMin, yMax, yLog);
            color = yplot_line_blend(color, plotUV.y, yRow, lineWidth, curve_color);
        }
    }

    // -------- data-buffer curves --------------------------------------------
    // Sequentially walk [data_count][len_0][samples_0...][len_1]...
    // The shader can't binary-search since each entry's length is data-driven,
    // but a linear walk over N≤16 entries is fine. Skipped in field mode.
    if (!isField) {
        var cursor: u32 = yplot_data_count_offset() + 1u;  // first len_i

        for (var bi = 0u; bi < YPLOT_MAX_CURVES; bi++) {
            if (bi >= data_count) { break; }

            let len = storage_buffer[cursor];
            let samples_off = cursor + 1u;

            let is_hidden = bi < 32u && (yplot_get_hidden_mask() & (1u << bi)) != 0u;
            if (len >= 2u && !is_hidden) {
                // Color slot: expressions first, then buffers, modulo 8.
                let color_slot = (func_count + bi) % 8u;
                let curve_color = yplot_unpack_color(yplot_get_colors(color_slot));

                // Map plotUV.x → fractional sample index, lerp neighbours.
                let idx_f   = plotUV.x * f32(len - 1u);
                var idx     = u32(floor(idx_f));
                var nxt     = min(idx + 1u, len - 1u);
                let t_lerp  = fract(idx_f);

                // Ring mode: unwrap so the newest sample is at the right.
                let ring = yplot_get_ring_heads(bi);
                if (ring != 0u) {
                    let head = ring - 1u;
                    idx = (head + idx) % len;
                    nxt = (head + nxt) % len;
                }

                let v1 = bitcast<f32>(storage_buffer[samples_off + idx]);
                let v2 = bitcast<f32>(storage_buffer[samples_off + nxt]);
                let y  = mix(v1, v2, t_lerp);

                // Larger value → row nearer the top (plotUV.y = 0 = yMax).
                let yRow = yplot_value_row(y, yMin, yMax, yLog);
                color = yplot_line_blend(color, plotUV.y, yRow, lineWidth, curve_color);
            }

            cursor = cursor + 1u + len;
        }
    }

    return vec4<f32>(color, 1.0);
}

// Vertex/Fragment entry points for standalone pipeline.
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    // Pane-local pixel coordinate (origin at the pane's top-left). The
    // raw @builtin(position) in the fragment is the FRAMEBUFFER pixel
    // — once the layer's render target has a non-zero viewport offset
    // (yui titlebar pushes the terminal pane down by ~32 px), framebuffer
    // coords no longer line up with the pane origin, and the bounds-x/y
    // check below would compare canvas-local bounds against framebuffer
    // pixels — yplot ends up painting over the titlebar / tabbar header
    // strip instead of clipping to its widget box. Map NDC → pane pixels
    // here so the FS reasons in the pane's own coordinate system,
    // independent of where the pane sits in the big surface. Same trick
    // text-layer.wgsl / ydraw-layer.wgsl already use.
    @location(0) @interpolate(linear) pane_pixel: vec2<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertex_index: u32) -> VertexOutput {
    // Fullscreen triangle - 3 vertices cover entire screen.
    var pos: array<vec2<f32>, 3> = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );

    let vp_w = uniforms.yplot_viewport_w;
    let vp_h = uniforms.yplot_viewport_h;

    var out: VertexOutput;
    out.position = vec4<f32>(pos[vertex_index], 0.0, 1.0);
    // NDC.x: -1 → 0, 1 → vp_w
    // NDC.y:  1 → 0, -1 → vp_h (framebuffer y is top-down)
    out.pane_pixel = vec2<f32>(
        (pos[vertex_index].x * 0.5 + 0.5) * vp_w,
        (0.5 - pos[vertex_index].y * 0.5) * vp_h
    );
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    // Viewport is the full pane. Transform this fragment's pane pixel into
    // the plot's SOURCE pixel by composing TWO independent zoom transforms:
    //     visual_zoom_* : non-intrusive (Ctrl+Scroll, mouse-anchored)
    //     cell_zoom_*   : intrusive (Ctrl+Shift+Scroll, cell-size scale)
    let vz_scale = uniforms.yplot_visual_zoom_scale;
    let vz_off   = vec2<f32>(uniforms.yplot_visual_zoom_off_x,
                             uniforms.yplot_visual_zoom_off_y);
    let cz_scale = uniforms.yplot_cell_zoom_scale;
    let cz_off   = vec2<f32>(uniforms.yplot_cell_zoom_off_x,
                             uniforms.yplot_cell_zoom_off_y);
    let vp       = vec2<f32>(uniforms.yplot_viewport_w,
                             uniforms.yplot_viewport_h);
    let vp_c     = vp * 0.5;

    let pane_px      = in.pane_pixel;
    let after_visual = (pane_px - vp_c) / max(vz_scale, 0.0001) + vp_c + vz_off;
    let source_px    = after_visual / max(cz_scale, 0.0001) + cz_off;

    let bounds_x = yplot_get_bounds_x();
    let bounds_y = yplot_get_bounds_y();
    let bounds_w = yplot_get_bounds_w();
    let bounds_h = yplot_get_bounds_h();

    let local_pos = source_px - vec2<f32>(bounds_x, bounds_y);
    if (local_pos.x < 0.0 || local_pos.y < 0.0 ||
        local_pos.x >= bounds_w || local_pos.y >= bounds_h) {
        discard;
    }

    return yplot_render(local_pos);
}
