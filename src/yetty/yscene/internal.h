/*
 * internal.h — yscene module-private contract between scene.c and dom.c.
 *
 * The dom is the scene's INTERNAL retained content tree: a plain struct +
 * plain functions, owned 1:1 by its scene, never exposed through yclass,
 * RPC, or FFI. The single public object of this module is the `scene`
 * class (scene.c); every client-facing content operation lives there and
 * forwards here in-process.
 *
 * Terminology: a NODE is the retained coordinate/content scope other
 * parts of the codebase historically call a CMD_GROUP or an "entity".
 * Drawables are wire records owned by nodes through batches; a node is
 * never itself a primitive.
 *
 *   dom
 *   └── node (legacy synonym: group / entity)
 *       ├── placement: local transform / clip / opacity / paint z
 *       ├── child nodes
 *       └── content batches (immutable verbatim wire spans)
 *           └── primitive / text / complex wire records
 *
 * Paint order — the converged #691 key, refined to batch granularity:
 *
 *     (paint_z, seq, record_index)
 *
 * `seq` is minted from ONE dom-global counter for both nodes (node_seq,
 * at first declaration) and batches (paint_seq, at the batch's creation).
 * Because batches and child nodes draw seqs from the same counter, the
 * source order "record run A, child node, record run B" is expressible:
 * batch A (seq n) < child (seq n+1) < batch B (seq n+2). A node's
 * FIRST content batch anchors at the node's own node_seq — its
 * declaration position — so the typed sequence "append parent A,
 * declare child, append parent B, then set the child's content" also
 * interleaves A < child < B no matter when the child content arrives.
 * Replacing a batch's bytes PRESERVES its paint_seq — a re-emitted
 * background repaints at its original depth, which is the flicker fix
 * by construction. node_seq itself is the node's immutable identity stamp
 * (dirt tracking, owner stamping); records sort by their batch's
 * paint_seq, records inherit paint_z from the owning node.
 *
 * scene.c reads the node/batch structs directly (derive walks every
 * dirty node — no accessor indirection on that path), but MUTATES only
 * through the functions below so dirty tracking, the pending generation,
 * and span retirement stay consistent.
 *
 * Nothing outside src/yetty/yscene/ may include this header.
 */
#ifndef YETTY_YSCENE_INTERNAL_H
#define YETTY_YSCENE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list_registry;

#define YETTY_YSCENE_DOM_ROOT_SLOT 0u
#define YETTY_YSCENE_DOM_INVALID_SLOT UINT32_MAX

/*===========================================================================
 * Tree records. Nodes are GROUPS (few, ~100 B each); the many drawables
 * live compactly as verbatim wire records inside batch spans. All
 * durable cross-references are arena slot indices — the arenas grow by
 * realloc, so raw pointers into them must never be stored (#691
 * "indices, not pointers"). Batch → owner references additionally carry
 * the owner's immutable node_seq, because node slots are recycled while
 * retired batches may still be referenced by in-flight derived state.
 *=========================================================================*/

struct yetty_yscene_dom_node {
    /* -- 8-byte members first (layout) -- */
    uint64_t external_id;

    /* -- 4-byte members -- */
    uint32_t parent_slot;
    /* Immutable identity stamp, minted from the dom-global seq counter
     * at first declaration; preserved across re-emission, content
     * replacement, and reparenting. Root = 0. */
    uint32_t node_seq;
    int32_t paint_z;

    /* Local 2D affine: world = M * local + translate, with
     * M = [m00 m01; m10 m11]. Identity by default. */
    float m00, m01, m10, m11;
    float translate_x, translate_y;

    struct yetty_ycore_rectangle clip;
    float opacity;

    /* Child slots, declaration order (iteration/dump determinism; paint
     * order is carried by the seq key, not by this array). */
    uint32_t *children;
    uint32_t child_count;
    uint32_t child_capacity;

    /* Slots into dom->batches, content order. record_index runs across
     * the spans in this order. */
    uint32_t *batch_slots;
    uint32_t batch_count;
    uint32_t batch_capacity;

    uint32_t next_free;

    /* -- flags -- */
    bool has_clip;
    bool in_use;
    /* Dirty flags the scene consumes at derive time. subtree_dirty is
     * the rolled-up "self or any descendant is dirty" bit that lets the
     * derive walk skip clean subtrees whole. Every slot whose flags go
     * from all-clean to any-set is recorded in dom->dirty_slots, so
     * clearing after derive is O(dirtied), not O(high-water). */
    bool placement_dirty;
    bool content_dirty;
    bool subtree_dirty;
};

struct yetty_yscene_dom_batch {
    /* Owned immutable copy of the producer's records, verbatim. */
    uint8_t *bytes;
    size_t bytes_len;
    /* Byte offset of each record start within `bytes`, sized exactly
     * record_count (no growth slack kept after the validation walk). */
    uint32_t *record_offsets;
    uint32_t record_count;

    /* Paint-order component #2 for every record in this span. Minted at
     * the batch's structural creation; PRESERVED by replace — content
     * re-emission never changes where the span sorts. */
    uint32_t paint_seq;
    /* Owning node's immutable node_seq (NOT its reusable slot — the
     * node arena recycles slots while this batch may still be
     * referenced by in-flight derived state). */
    uint32_t owner_seq;

    /* Generation this span became (or will become, pre-commit) content. */
    uint64_t live_generation;
    /* Generation the span was retired under; 0 = still live. */
    uint64_t retire_generation;

    bool in_use;
    uint32_t next_free;
    /* Singly-linked retired list (head at dom->retired_batch_head). */
    uint32_t next_retired;
};

/* Open-addressing hash entry: external_id → node slot. external_id 0 is
 * the root sentinel (never stored); UINT64_MAX is the tombstone value —
 * and therefore rejected as a producer id. */
struct yetty_yscene_dom_id_entry {
    uint64_t external_id;
    uint32_t slot;
};

struct yetty_yscene_dom {
    /* Wire-record stride dispatch. Borrowed when handed in at create;
     * lazily created (and then owned) on first content walk otherwise,
     * so headless tests need no wiring. */
    struct yetty_ydraw_drawable_list_registry *registry;
    bool owns_registry;

    struct yetty_yscene_dom_node *nodes;
    uint32_t node_capacity;
    /* One past the highest slot ever handed out; the free list reuses
     * released slots before this mark grows. */
    uint32_t node_high_water;
    uint32_t free_node_head;
    uint32_t live_node_count;

    struct yetty_yscene_dom_batch *batches;
    uint32_t batch_capacity;
    uint32_t batch_high_water;
    uint32_t free_batch_head;
    uint32_t live_batch_count;
    uint32_t retired_batch_head;

    struct yetty_yscene_dom_id_entry *id_index;
    uint32_t id_index_capacity;
    /* live keys only. */
    uint32_t id_index_live;
    /* live + tombstones — the probe-length driver. Rehash triggers on
     * THIS load (purging tombstones), so delete/recreate churn cannot
     * degrade lookups while live count stays low. */
    uint32_t id_index_occupied;

    /* Slots whose dirty flags went from all-clean to any-set since the
     * last clear — the O(dirtied) clear list. */
    uint32_t *dirty_slots;
    uint32_t dirty_slot_count;
    uint32_t dirty_slot_capacity;

    /* The ONE seq counter behind both node_seq and batch paint_seq.
     * Monotonic, never rewound (not even by zero() — keeps historic
     * paint keys unambiguous). Root reserves 0. */
    uint32_t next_seq;

    uint64_t committed_generation;
    bool has_pending;
};

YETTY_YRESULT_DECLARE(yetty_yscene_dom_ptr, struct yetty_yscene_dom *);

/* Everything a node's placement carries, set atomically so partial
 * producer updates (a transform-only animation frame) are one call:
 * read the node, patch the fields that changed, write the whole thing. */
struct yetty_yscene_dom_placement {
    int32_t paint_z;
    float m00, m01, m10, m11;
    float translate_x, translate_y;
    bool has_clip;
    struct yetty_ycore_rectangle clip;
    float opacity;
};

/*===========================================================================
 * Lifecycle
 *=========================================================================*/

/* `registry` is BORROWED (typically the framework's shared instance);
 * NULL makes the dom build and own a default registry on first content
 * walk — the headless/test path. */
struct yetty_yscene_dom_ptr_result yetty_yscene_dom_create(
    struct yetty_ydraw_drawable_list_registry *registry);

struct yetty_ycore_void_result yetty_yscene_dom_destroy(struct yetty_yscene_dom *dom);

/* Bind (or rebind) the borrowed wire-record registry. Permitted only
 * while the dom has never held content; rejected afterwards (record
 * strides must not change under existing spans). */
struct yetty_ycore_void_result yetty_yscene_dom_set_registry(
    struct yetty_yscene_dom *dom, struct yetty_ydraw_drawable_list_registry *registry);

/*===========================================================================
 * Tree mutation. All functions resolve nodes by external id (0 = root).
 * Mutations are atomic: any validation or allocation failure leaves the
 * tree exactly as it was.
 *=========================================================================*/

/* Declare (or re-declare) `external_id` under `parent_external_id`. A
 * fresh id mints a node (and its immutable node_seq). An existing id is
 * a no-op when the parent matches, and a subtree MOVE when it differs —
 * the node keeps its seq, content, and children. Rejected: id 0 (the
 * root), id UINT64_MAX, and a move that would make a node its own
 * ancestor. */
struct yetty_ycore_void_result yetty_yscene_dom_node_declare(struct yetty_yscene_dom *dom,
                                                             uint64_t external_id,
                                                             uint64_t parent_external_id);

struct yetty_ycore_void_result yetty_yscene_dom_node_set_placement(
    struct yetty_yscene_dom *dom, uint64_t external_id,
    const struct yetty_yscene_dom_placement *placement);

/* Wholesale content replace: the node ends up with exactly ONE batch
 * holding `bytes`. The span's paint_seq is inherited from the node's
 * previous first batch (so wire-protocol re-emission repaints at the
 * original depth); a node gaining content for the first time mints a
 * fresh seq. Empty (len 0, bytes NULL allowed) clears the content. */
struct yetty_ycore_void_result yetty_yscene_dom_node_set_content(struct yetty_yscene_dom *dom,
                                                                 uint64_t external_id,
                                                                 const uint8_t *bytes,
                                                                 size_t bytes_len);

/* Batch-granular content — the update-locality path (#691 review): a
 * small change copies only its own independently addressable span,
 * never "every batch owned by this node". Batches are addressed by
 * their index in the node's content order. append mints a fresh
 * paint_seq (the span sorts after everything declared so far); replace
 * PRESERVES the replaced span's paint_seq; remove shifts subsequent
 * indices down by one. */
struct yetty_ycore_void_result yetty_yscene_dom_node_append_batch(struct yetty_yscene_dom *dom,
                                                                  uint64_t external_id,
                                                                  const uint8_t *bytes,
                                                                  size_t bytes_len);

struct yetty_ycore_void_result yetty_yscene_dom_node_replace_batch(struct yetty_yscene_dom *dom,
                                                                   uint64_t external_id,
                                                                   uint32_t batch_index,
                                                                   const uint8_t *bytes,
                                                                   size_t bytes_len);

struct yetty_ycore_void_result yetty_yscene_dom_node_remove_batch(struct yetty_yscene_dom *dom,
                                                                  uint64_t external_id,
                                                                  uint32_t batch_index);

/* Delete the node and its whole subtree. Spans are retired, not freed —
 * see reclaim. The root cannot be deleted; use zero. */
struct yetty_ycore_void_result yetty_yscene_dom_node_delete(struct yetty_yscene_dom *dom,
                                                            uint64_t external_id);

/* Clear the whole document: every non-root node released, every span
 * retired, the root's placement reset (and marked placement-dirty). */
struct yetty_ycore_void_result yetty_yscene_dom_zero(struct yetty_yscene_dom *dom);

/*===========================================================================
 * Versioning
 *=========================================================================*/

/* Promote pending mutations to the next committed generation; returns
 * the (possibly unchanged) committed generation. The scene derives only
 * committed generations, so a half-sent frame is never rendered. */
struct yetty_ycore_uint64_result yetty_yscene_dom_commit(struct yetty_yscene_dom *dom);

/* Free every retired span with retire_generation <= derived_generation.
 * The scene calls this after deriving that generation — the single-
 * thread contract that keeps committed spans immutable while the
 * scene's derived state still references them. */
struct yetty_ycore_void_result yetty_yscene_dom_reclaim(struct yetty_yscene_dom *dom,
                                                        uint64_t derived_generation);

/* Clear the dirty flags of exactly the nodes dirtied since the last
 * clear (the dirty_slots list) — O(dirtied). */
struct yetty_ycore_void_result yetty_yscene_dom_clear_dirty(struct yetty_yscene_dom *dom);

/*===========================================================================
 * Queries
 *=========================================================================*/

/* external_id → node slot; id 0 resolves to the root. */
struct yetty_ycore_uint32_result yetty_yscene_dom_lookup(const struct yetty_yscene_dom *dom,
                                                         uint64_t external_id);

/* Count of retired-but-not-yet-reclaimed spans (reclaim-contract
 * observability for tests). */
uint32_t yetty_yscene_dom_retired_batch_count(const struct yetty_yscene_dom *dom);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YSCENE_INTERNAL_H */
