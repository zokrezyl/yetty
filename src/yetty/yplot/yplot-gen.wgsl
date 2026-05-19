// Auto-generated from yplot.yaml - DO NOT EDIT
//
// Uniform accessors + storage-walker helpers for the yplot complex prim.
// The storage region (storage_buffer) is laid out as:
//   [0]                     bytecode_len      (u32, in words)
//   [1 .. bytecode_len]     bytecode          (yfsvm program)
//   [bc_len + 1]            data_count        (u32, number of data buffers)
//   then for each i:
//     [...]                 len_i             (in f32 samples)
//     [...]                 samples_i...      (f32 bitcast to u32)
//
// No const offsets are emitted — every per-instance value is read from the
// buffer at runtime, so the pipeline never recompiles when an instance
// adds, resizes, or removes a data buffer.

// ---- uniform accessors ------------------------------------------------------

fn yplot_get_bounds_x() -> f32 { return uniforms.yplot_bounds_x; }
fn yplot_get_bounds_y() -> f32 { return uniforms.yplot_bounds_y; }
fn yplot_get_bounds_w() -> f32 { return uniforms.yplot_bounds_w; }
fn yplot_get_bounds_h() -> f32 { return uniforms.yplot_bounds_h; }
fn yplot_get_x_min()    -> f32 { return uniforms.yplot_x_min; }
fn yplot_get_x_max()    -> f32 { return uniforms.yplot_x_max; }
fn yplot_get_y_min()    -> f32 { return uniforms.yplot_y_min; }
fn yplot_get_y_max()    -> f32 { return uniforms.yplot_y_max; }
fn yplot_get_flags()    -> u32 { return uniforms.yplot_flags; }
fn yplot_get_function_count() -> u32 { return uniforms.yplot_function_count; }

// `time` — monotonic seconds, written CPU-side by yplot-time.c's tick
// handler when the compiled bytecode uses LOAD_T (yfsvm `uses_time`).
// Always present in the uniform buffer; reads 0 on static plots.
fn yplot_get_time() -> f32 { return uniforms.yplot_time; }

fn yplot_get_colors(idx: u32) -> u32 {
    // Array uniform: yplot_colors_0 through yplot_colors_7.
    // WGSL has no dynamic struct-field indexing, so switch.
    switch idx {
        case 0u: { return uniforms.yplot_colors_0; }
        case 1u: { return uniforms.yplot_colors_1; }
        case 2u: { return uniforms.yplot_colors_2; }
        case 3u: { return uniforms.yplot_colors_3; }
        case 4u: { return uniforms.yplot_colors_4; }
        case 5u: { return uniforms.yplot_colors_5; }
        case 6u: { return uniforms.yplot_colors_6; }
        case 7u: { return uniforms.yplot_colors_7; }
        default: { return uniforms.yplot_colors_0; }
    }
}

// ---- storage layout helpers -------------------------------------------------

// Length of the bytecode block (in u32 words). Bytecode itself starts at word 1.
fn yplot_bytecode_len()    -> u32 { return storage_buffer[0]; }
fn yplot_bytecode_offset() -> u32 { return 1u; }

// Word index of the data_count u32 (right after the bytecode block).
fn yplot_data_count_offset() -> u32 { return 1u + yplot_bytecode_len(); }

// Number of data buffers carried by this instance (0 → expression-only).
fn yplot_data_count() -> u32 {
    return storage_buffer[yplot_data_count_offset()];
}
