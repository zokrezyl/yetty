// YDraw Complex Primitive Types - Wire Format
//
// Pure data layout for complexes traveling over the DCS wire:
//   - struct figure (type + payload_size + FAM data)
//   - type-id ranges (simple SDF / drawable-list entry / complex)
//   - helpers that operate on raw bytes (is_complex_type, size, aabb, handler)
//
// This header is intentionally GPU-less so client tools (ycat, yecho, yplot
// CLI, demos, riscv64 builds) can include it without dragging in WebGPU.
//
// The server-side runtime (struct concrete_factory, struct figure_instance,
// the abstract-factory registry) lives in
// include/yetty/ydraw-factory/complex-factory.h.
//
// See docs/ydraw.md for full documentation.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Complex type IDs (0x80000000+ to avoid collision with SDF 0-255)
//=============================================================================

/* Tier ranges for ydraw primitive types:
 *   [0x00000000, 0x000000FF]   Simple SDF (fixed-size, generated)
 *   [0x40000000, 0x7FFFFFFF]   Drawable-list entry (variable-size, no GPU pipeline)
 *                                FONT       — yetty/ydraw-list/font-resource.h
 *                                TEXT_DRAWABLE_LIST  — yetty/ydraw-list/text-drawable-list.h
 *   [0x80000000, 0xFFFFFFFF]   Complex (factory + per-instance GPU resources)
 *                                each concrete factory owns its own type id
 *                                (e.g. YETTY_YPLOT_TYPE_ID in yplot-gen.h).
 */
#define YETTY_YDRAW_COMPLEX_TYPE_BASE 0x80000000u

//=============================================================================
// Complex header (FAM wire format)
//=============================================================================
//
struct yetty_ydraw_complex_header {
    uint32_t type;
    uint32_t payload_size;
};

struct yetty_ydraw_complex_record {
    struct yetty_ydraw_complex_header header;
    uint8_t data[];
};

// Check if type uses FAM format
bool yetty_ydraw_is_complex(uint32_t type);

// Get AABB (reads bounds from standard offset 0-15 in payload)
struct rectangle_result yetty_ydraw_complex_record_aabb(const void *data);

//=============================================================================
// Base ops for complexes (for drawable-list registry)
// Returns base ops pointer for buffer iteration
//=============================================================================

#include <yetty/ydraw-list/drawable-list-registry.h>

// Handler for complex prim types (>= 0x80000000)
struct yetty_ydraw_drawable_list_entry_ops_ptr_result yetty_ydraw_complex_record_handler(
    uint32_t drawable_type);

#ifdef __cplusplus
}
#endif
