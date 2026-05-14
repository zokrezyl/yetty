/* scene-canvas.h — public API for the scene-canvas variant.
 *
 * A fixed-size grid of cells, with named entities owning primitives.
 * Entities form a tree (parent / children). Each entity owns its own
 * primitive sequence and a list of cells it has placed entries in.
 *
 * Cells hold an associative array: entity-slot to a list of local
 * indices into that entity's primitive sequence. The hierarchy is held
 * by the entities themselves, never in the cells.
 *
 * Deletion of an entity walks its own touched-cells list, removes its
 * entry from each, then recurses into its children. No grid-wide scan,
 * no AABB recomputation at delete time.
 *
 * Two input paths share one internal API:
 *   - OSC state machine via the polymorphic process_input.
 *   - Direct in-process calls — the entity API below.
 */
#pragma once

#include <stdint.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/flyweight.h>
#include <yetty/ypaint/canvas.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;

/* Opaque entity handle. Stable for the entity's lifetime; never moves
 * until the entity is deleted. */
struct yetty_ypaint_scene_entity;

YETTY_YRESULT_DECLARE(yetty_ypaint_scene_entity_ptr,
                      struct yetty_ypaint_scene_entity *);

/* Create a scene-canvas. Returns the polymorphic base pointer; the
 * caller frees with yetty_ypaint_canvas_destroy. */
YETTY_ANNOT_CALLER_OWNED
struct yetty_ypaint_canvas_ptr_result yetty_ypaint_scene_canvas_create(
    const struct yetty_context *context);

/* The implicit root entity. Receives any primitive added without a
 * specific entity (e.g. free prims arriving on the wire). Lives for
 * the canvas's lifetime. */
struct yetty_ypaint_scene_entity *yetty_ypaint_scene_canvas_root(
    struct yetty_ypaint_canvas *canvas);

/* Create a new entity as a child of `parent` (root if NULL).
 * `external_id` is the caller's stable id (e.g. a ygui widget id);
 * passing the same external_id as an existing entity returns an error. */
struct yetty_ypaint_scene_entity_ptr_result yetty_ypaint_scene_entity_create(
    struct yetty_ypaint_canvas *canvas,
    struct yetty_ypaint_scene_entity *parent,
    uint64_t external_id);

/* Look up an entity by external_id. Returns NULL if not found. */
struct yetty_ypaint_scene_entity *yetty_ypaint_scene_entity_lookup(
    struct yetty_ypaint_canvas *canvas, uint64_t external_id);

/* Append a primitive to the entity. The primitive's payload words are
 * copied into the entity's prim arena; the AABB is computed from
 * `fw->ops->aabb` and the resulting cell range is inserted into each
 * overlapping cell's map (and recorded in the entity's touched-cells
 * list). */
struct yetty_ycore_void_result yetty_ypaint_scene_entity_add_prim(
    struct yetty_ypaint_canvas *canvas,
    struct yetty_ypaint_scene_entity *entity,
    const struct yetty_ypaint_core_prim_flyweight *fw);

/* Remove this entity's own primitives and detach them from every cell
 * it has touched. Children are untouched. The entity remains usable
 * and may be repopulated. */
struct yetty_ycore_void_result yetty_ypaint_scene_entity_clear(
    struct yetty_ypaint_canvas *canvas,
    struct yetty_ypaint_scene_entity *entity);

/* Recursively delete this entity, its children, and all of their
 * primitives. The handle is invalid after this call. The root entity
 * cannot be deleted — clear it instead. */
struct yetty_ycore_void_result yetty_ypaint_scene_entity_delete(
    struct yetty_ypaint_canvas *canvas,
    struct yetty_ypaint_scene_entity *entity);

#ifdef __cplusplus
}
#endif
