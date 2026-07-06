#ifndef YETTY_YSDF_MERGE_H
#define YETTY_YSDF_MERGE_H

/*
 * merge — splice one drawable list into another under a 2D affine
 * transform (scale, then translate).
 *
 * The workhorse behind embedding a self-contained produced scene inside a
 * larger one: ybrowser renders an SVG through ysvg into its own drawable
 * list, then merges that list into the page's paint output at the image
 * box's rectangle. Each source record is copied, its geometry transformed
 * per the primitive's own semantics (coordinates get scale+offset, axis
 * extents get the axis scale, radii get the mean scale — see the generated
 * yetty_ysdf_geometry_transform), and appended to the destination.
 *
 * Coverage:
 *   - SDF primitives (with or without the wire id word): geometry words
 *     transformed; stroke_width scaled by the mean factor.
 *   - TEXT_DRAWABLE_LIST: position transformed, font_size scaled by the
 *     mean factor.
 *   - FONT resources: copied through unchanged. Font ids are envelope-local
 *     producer counters — when BOTH lists declare fonts the ids collide, so
 *     merging such lists needs id remapping this API does not yet do.
 *     (ysvg emits no FONT records; its text uses the canvas default font.)
 *   - CMD_ZERO: dropped — embedded content never clears the host canvas.
 *   - Any other structured command: copied through UNCHANGED. Extend the
 *     transform switch before merging lists that carry group/update
 *     commands with geometry.
 */

#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

/* Append a transformed copy of every record in `source` to `destination`:
 * positions map as p' = p * scale + offset. `z_order_offset` is added to
 * every SDF primitive's z_order and every text span's layer, so embedded
 * content slots into the host scene's paint order (hosts that assign
 * consecutive z values advance their counter by the returned count).
 * The source is not modified; destination scene bounds are left untouched
 * (the caller owns framing). Returns the number of records merged. */
struct yetty_ycore_int_result yetty_ydraw_drawable_list_merge_transformed(
    struct yetty_ydraw_drawable_list *destination, const struct yetty_ydraw_drawable_list *source,
    float offset_x, float offset_y, float scale_x, float scale_y, uint32_t z_order_offset);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YSDF_MERGE_H */
