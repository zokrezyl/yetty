/*
 * dom.c — the yscene retained content tree (module-internal, GPU-free).
 *
 * See internal.h for the data model, the paint-order key, and the
 * ownership contract. This TU implements the mutation/versioning logic;
 * scene.c traverses the structs directly and never mutates them except
 * through these functions.
 *
 * Atomicity: every mutation validates and allocates BEFORE any
 * destructive step, so a malformed span or an allocation failure leaves
 * the tree exactly as it was.
 *
 * What the dom deliberately does NOT contain: world/derived state
 * (transform, clip and AABB accumulation live in the scene), GPU
 * anything, fonts, complex instances, wire-envelope walking (the
 * adapter that unpacks a legacy ydraw envelope with embedded CMD_GROUP
 * bodies into these typed mutations lives scene-side). Text stays as
 * TEXT_DRAWABLE_LIST records in the spans — glyph expansion is derived
 * data and belongs to the scene, not the master payload.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-iterator.h>
#include <yetty/ydraw-core/drawable-list-registry.h>
#include <yetty/ysdf/default-registry.h>

#include "internal.h"

/* id_index bucket states (see internal.h: id 0 = root sentinel, never
 * stored; UINT64_MAX = tombstone, rejected as a producer id). */
#define YSCENE_DOM_ID_EMPTY 0u
#define YSCENE_DOM_ID_TOMBSTONE UINT64_MAX

/*===========================================================================
 * id_index — open addressing, linear probe, power-of-two capacity.
 * Load accounting tracks BOTH live keys and tombstones: the rehash
 * trigger fires on occupied load, and rehashing re-inserts only live
 * keys, so delete/recreate churn cannot degrade probe lengths.
 *=========================================================================*/

static uint32_t dom_id_hash(uint64_t external_id)
{
    /* splitmix64 finalizer — spreads sequential producer ids. */
    uint64_t mixed = external_id;
    mixed ^= mixed >> 30;
    mixed *= 0xbf58476d1ce4e5b9ull;
    mixed ^= mixed >> 27;
    mixed *= 0x94d049bb133111ebull;
    mixed ^= mixed >> 31;
    return (uint32_t)mixed;
}

static uint32_t dom_id_index_find(const struct yetty_yscene_dom *dom, uint64_t external_id)
{
    if (dom->id_index_capacity == 0) {
        return YETTY_YSCENE_DOM_INVALID_SLOT;
    }
    uint32_t mask = dom->id_index_capacity - 1;
    uint32_t probe = dom_id_hash(external_id) & mask;
    for (uint32_t step = 0; step < dom->id_index_capacity; step++) {
        const struct yetty_yscene_dom_id_entry *entry = &dom->id_index[probe];
        if (entry->external_id == YSCENE_DOM_ID_EMPTY) {
            return YETTY_YSCENE_DOM_INVALID_SLOT;
        }
        if (entry->external_id == external_id) {
            return entry->slot;
        }
        probe = (probe + 1) & mask;
    }
    return YETTY_YSCENE_DOM_INVALID_SLOT;
}

/* Raw insert into a table known to have room; no growth, no accounting. */
enum dom_id_place_outcome {
    DOM_ID_PLACED_EMPTY,     /* new key in a fresh bucket (occupied grows) */
    DOM_ID_PLACED_TOMBSTONE, /* new key reusing a tombstone (occupied flat) */
    DOM_ID_UPDATED,          /* existing key, slot refreshed */
};

static enum dom_id_place_outcome dom_id_index_place(struct yetty_yscene_dom_id_entry *entries,
                                                    uint32_t capacity, uint64_t external_id,
                                                    uint32_t slot)
{
    uint32_t mask = capacity - 1;
    uint32_t probe = dom_id_hash(external_id) & mask;
    for (;;) {
        struct yetty_yscene_dom_id_entry *entry = &entries[probe];
        if (entry->external_id == YSCENE_DOM_ID_EMPTY ||
            entry->external_id == YSCENE_DOM_ID_TOMBSTONE) {
            bool was_tombstone = entry->external_id == YSCENE_DOM_ID_TOMBSTONE;
            entry->external_id = external_id;
            entry->slot = slot;
            return was_tombstone ? DOM_ID_PLACED_TOMBSTONE : DOM_ID_PLACED_EMPTY;
        }
        if (entry->external_id == external_id) {
            entry->slot = slot;
            return DOM_ID_UPDATED;
        }
        probe = (probe + 1) & mask;
    }
}

/* Rebuild the table at `new_capacity` (a power of two), dropping every
 * tombstone. Live entries re-place; occupied collapses to live. */
static struct yetty_ycore_void_result dom_id_index_rehash(struct yetty_yscene_dom *dom,
                                                          uint32_t new_capacity)
{
    struct yetty_yscene_dom_id_entry *new_entries =
        calloc(new_capacity, sizeof(struct yetty_yscene_dom_id_entry));
    if (!new_entries) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom: id_index alloc failed");
    }
    for (uint32_t i = 0; i < dom->id_index_capacity; i++) {
        uint64_t stored_id = dom->id_index[i].external_id;
        if (stored_id != YSCENE_DOM_ID_EMPTY && stored_id != YSCENE_DOM_ID_TOMBSTONE) {
            dom_id_index_place(new_entries, new_capacity, stored_id, dom->id_index[i].slot);
        }
    }
    free(dom->id_index);
    dom->id_index = new_entries;
    dom->id_index_capacity = new_capacity;
    dom->id_index_occupied = dom->id_index_live;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dom_id_index_insert(struct yetty_yscene_dom *dom,
                                                          uint64_t external_id, uint32_t slot)
{
    /* Keep OCCUPIED load (live + tombstones) below ~70%. Grow only when
     * the live keys need it; a tombstone-heavy table rehashes at the
     * same capacity, purging the tombstones. */
    if (dom->id_index_capacity == 0 ||
        (dom->id_index_occupied + 1) * 10 >= dom->id_index_capacity * 7) {
        uint32_t new_capacity = dom->id_index_capacity ? dom->id_index_capacity : 64;
        while ((dom->id_index_live + 1) * 2 >= new_capacity) {
            new_capacity *= 2;
        }
        struct yetty_ycore_void_result rehash_res = dom_id_index_rehash(dom, new_capacity);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rehash_res, "yscene dom: id_index rehash");
    }
    switch (dom_id_index_place(dom->id_index, dom->id_index_capacity, external_id, slot)) {
    case DOM_ID_PLACED_EMPTY:
        dom->id_index_live++;
        dom->id_index_occupied++;
        break;
    case DOM_ID_PLACED_TOMBSTONE:
        /* The bucket was already occupied (by the tombstone). */
        dom->id_index_live++;
        break;
    case DOM_ID_UPDATED:
        break;
    }
    return YETTY_OK_VOID();
}

static void dom_id_index_remove(struct yetty_yscene_dom *dom, uint64_t external_id)
{
    if (dom->id_index_capacity == 0) {
        return;
    }
    uint32_t mask = dom->id_index_capacity - 1;
    uint32_t probe = dom_id_hash(external_id) & mask;
    for (uint32_t step = 0; step < dom->id_index_capacity; step++) {
        struct yetty_yscene_dom_id_entry *entry = &dom->id_index[probe];
        if (entry->external_id == YSCENE_DOM_ID_EMPTY) {
            return;
        }
        if (entry->external_id == external_id) {
            /* Tombstone: still occupies a bucket (probe chains must not
             * break); reclaimed by the next rehash. */
            entry->external_id = YSCENE_DOM_ID_TOMBSTONE;
            if (dom->id_index_live > 0) {
                dom->id_index_live--;
            }
            return;
        }
        probe = (probe + 1) & mask;
    }
}

/*===========================================================================
 * Dirty tracking — flags on the node + the dom-level dirty_slots list.
 * Recording happens on the clean→dirty transition only, so the list
 * holds each dirtied slot once per generation (a released-and-reused
 * slot may appear twice; clearing twice is harmless).
 *=========================================================================*/

static void dom_dirty_record(struct yetty_yscene_dom *dom, uint32_t slot)
{
    if (dom->dirty_slot_count == UINT32_MAX) {
        return; /* already in overflow mode — full sweep will clear */
    }
    if (dom->dirty_slot_count == dom->dirty_slot_capacity) {
        uint32_t new_capacity = dom->dirty_slot_capacity ? dom->dirty_slot_capacity * 2 : 64;
        uint32_t *grown = realloc(dom->dirty_slots, (size_t)new_capacity * sizeof(uint32_t));
        if (!grown) {
            /* Losing the record must not lose the dirt: the node flags
             * still mark it; flag overflow so the next clear_dirty
             * falls back to one full high-water sweep. */
            dom->dirty_slot_count = UINT32_MAX;
            return;
        }
        dom->dirty_slots = grown;
        dom->dirty_slot_capacity = new_capacity;
    }
    dom->dirty_slots[dom->dirty_slot_count++] = slot;
}

static bool dom_node_all_clean(const struct yetty_yscene_dom_node *node)
{
    return !node->placement_dirty && !node->content_dirty && !node->subtree_dirty;
}

/* Roll the "something under me changed" bit up to the root so the derive
 * walk can skip clean subtrees. Stops at the first ancestor that already
 * carries it (its path to the root is set by construction). */
static void dom_bubble_subtree_dirty(struct yetty_yscene_dom *dom, uint32_t slot)
{
    uint32_t walk = slot;
    while (walk != YETTY_YSCENE_DOM_INVALID_SLOT) {
        struct yetty_yscene_dom_node *node = &dom->nodes[walk];
        if (node->subtree_dirty) {
            return;
        }
        if (dom_node_all_clean(node)) {
            dom_dirty_record(dom, walk);
        }
        node->subtree_dirty = true;
        walk = node->parent_slot;
    }
}

/* Mark `slot` placement- and/or content-dirty (+ rollup + pending). */
static void dom_mark_dirty(struct yetty_yscene_dom *dom, uint32_t slot, bool placement,
                           bool content)
{
    struct yetty_yscene_dom_node *node = &dom->nodes[slot];
    if (dom_node_all_clean(node)) {
        dom_dirty_record(dom, slot);
    }
    node->placement_dirty = node->placement_dirty || placement;
    node->content_dirty = node->content_dirty || content;
    dom_bubble_subtree_dirty(dom, slot);
    dom->has_pending = true;
}

/*===========================================================================
 * Node arena
 *=========================================================================*/

static void dom_node_reset(struct yetty_yscene_dom_node *node)
{
    node->external_id = 0;
    node->parent_slot = YETTY_YSCENE_DOM_INVALID_SLOT;
    node->paint_z = 0;
    node->m00 = 1.0f;
    node->m01 = 0.0f;
    node->m10 = 0.0f;
    node->m11 = 1.0f;
    node->translate_x = 0.0f;
    node->translate_y = 0.0f;
    node->has_clip = false;
    node->clip = (struct yetty_ycore_rectangle){0};
    node->opacity = 1.0f;
    node->child_count = 0;
    node->batch_count = 0;
    node->placement_dirty = false;
    node->content_dirty = false;
    node->subtree_dirty = false;
}

static struct yetty_ycore_uint32_result dom_node_alloc(struct yetty_yscene_dom *dom)
{
    uint32_t slot;
    if (dom->free_node_head != YETTY_YSCENE_DOM_INVALID_SLOT) {
        slot = dom->free_node_head;
        dom->free_node_head = dom->nodes[slot].next_free;
    } else {
        if (dom->node_high_water == dom->node_capacity) {
            uint32_t new_capacity = dom->node_capacity ? dom->node_capacity * 2 : 32;
            struct yetty_yscene_dom_node *grown =
                realloc(dom->nodes, (size_t)new_capacity * sizeof(struct yetty_yscene_dom_node));
            if (!grown) {
                return YETTY_ERR(yetty_ycore_uint32, "yscene dom: node arena alloc failed");
            }
            memset(grown + dom->node_capacity, 0,
                   (size_t)(new_capacity - dom->node_capacity) *
                       sizeof(struct yetty_yscene_dom_node));
            dom->nodes = grown;
            dom->node_capacity = new_capacity;
        }
        slot = dom->node_high_water++;
    }
    struct yetty_yscene_dom_node *node = &dom->nodes[slot];
    /* Keep the (possibly recycled) child/batch arrays; reset the rest. */
    dom_node_reset(node);
    node->in_use = true;
    node->next_free = YETTY_YSCENE_DOM_INVALID_SLOT;
    node->node_seq = dom->next_seq++;
    dom->live_node_count++;
    return YETTY_OK(yetty_ycore_uint32, slot);
}

static void dom_node_release(struct yetty_yscene_dom *dom, uint32_t slot)
{
    struct yetty_yscene_dom_node *node = &dom->nodes[slot];
    node->in_use = false;
    node->next_free = dom->free_node_head;
    dom->free_node_head = slot;
    if (dom->live_node_count > 0) {
        dom->live_node_count--;
    }
}

/* Make room for one more child BEFORE any destructive step (a reparent
 * detaches from the old parent only after attach cannot fail anymore). */
static struct yetty_ycore_void_result dom_child_reserve(struct yetty_yscene_dom *dom,
                                                        uint32_t parent_slot)
{
    struct yetty_yscene_dom_node *parent = &dom->nodes[parent_slot];
    if (parent->child_count < parent->child_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_capacity = parent->child_capacity ? parent->child_capacity * 2 : 4;
    uint32_t *grown = realloc(parent->children, (size_t)new_capacity * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom: child list alloc failed");
    }
    parent->children = grown;
    parent->child_capacity = new_capacity;
    return YETTY_OK_VOID();
}

/* Append `child_slot` under `parent_slot`. Capacity must have been
 * reserved; cannot fail. */
static void dom_child_attach(struct yetty_yscene_dom *dom, uint32_t parent_slot,
                             uint32_t child_slot)
{
    struct yetty_yscene_dom_node *parent = &dom->nodes[parent_slot];
    parent->children[parent->child_count++] = child_slot;
    dom->nodes[child_slot].parent_slot = parent_slot;
}

static void dom_child_detach(struct yetty_yscene_dom *dom, uint32_t child_slot)
{
    uint32_t parent_slot = dom->nodes[child_slot].parent_slot;
    if (parent_slot == YETTY_YSCENE_DOM_INVALID_SLOT) {
        return;
    }
    struct yetty_yscene_dom_node *parent = &dom->nodes[parent_slot];
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child_slot) {
            /* Preserve declaration order for the remaining siblings. */
            memmove(&parent->children[i], &parent->children[i + 1],
                    (size_t)(parent->child_count - i - 1) * sizeof(uint32_t));
            parent->child_count--;
            break;
        }
    }
    dom->nodes[child_slot].parent_slot = YETTY_YSCENE_DOM_INVALID_SLOT;
}

/* Is `candidate_slot` on the ancestor path of `slot` (or equal to it)?
 * Used to reject reparents that would create a cycle. */
static bool dom_is_self_or_ancestor(const struct yetty_yscene_dom *dom, uint32_t candidate_slot,
                                    uint32_t slot)
{
    uint32_t walk = slot;
    while (walk != YETTY_YSCENE_DOM_INVALID_SLOT) {
        if (walk == candidate_slot) {
            return true;
        }
        walk = dom->nodes[walk].parent_slot;
    }
    return false;
}

/*===========================================================================
 * Batch table
 *=========================================================================*/

static struct yetty_ycore_uint32_result dom_batch_alloc(struct yetty_yscene_dom *dom)
{
    uint32_t slot;
    if (dom->free_batch_head != YETTY_YSCENE_DOM_INVALID_SLOT) {
        slot = dom->free_batch_head;
        dom->free_batch_head = dom->batches[slot].next_free;
    } else {
        if (dom->batch_high_water == dom->batch_capacity) {
            uint32_t new_capacity = dom->batch_capacity ? dom->batch_capacity * 2 : 32;
            struct yetty_yscene_dom_batch *grown =
                realloc(dom->batches, (size_t)new_capacity * sizeof(struct yetty_yscene_dom_batch));
            if (!grown) {
                return YETTY_ERR(yetty_ycore_uint32, "yscene dom: batch table alloc failed");
            }
            memset(grown + dom->batch_capacity, 0,
                   (size_t)(new_capacity - dom->batch_capacity) *
                       sizeof(struct yetty_yscene_dom_batch));
            dom->batches = grown;
            dom->batch_capacity = new_capacity;
        }
        slot = dom->batch_high_water++;
    }
    struct yetty_yscene_dom_batch *batch = &dom->batches[slot];
    memset(batch, 0, sizeof(*batch));
    batch->in_use = true;
    batch->batch_stamp = ++dom->next_batch_stamp;
    batch->next_free = YETTY_YSCENE_DOM_INVALID_SLOT;
    batch->next_retired = YETTY_YSCENE_DOM_INVALID_SLOT;
    dom->live_batch_count++;
    return YETTY_OK(yetty_ycore_uint32, slot);
}

/* Move a live batch onto the retired list. The span stays allocated —
 * the scene's derived state may still reference it — until reclaim()
 * frees everything retired at or before a derived generation. */
static void dom_batch_retire(struct yetty_yscene_dom *dom, uint32_t batch_slot)
{
    struct yetty_yscene_dom_batch *batch = &dom->batches[batch_slot];
    batch->retire_generation = dom->committed_generation + 1;
    batch->next_retired = dom->retired_batch_head;
    dom->retired_batch_head = batch_slot;
    if (dom->live_batch_count > 0) {
        dom->live_batch_count--;
    }
}

static void dom_batch_free(struct yetty_yscene_dom *dom, uint32_t batch_slot)
{
    struct yetty_yscene_dom_batch *batch = &dom->batches[batch_slot];
    free(batch->bytes);
    free(batch->record_offsets);
    batch->bytes = NULL;
    batch->record_offsets = NULL;
    batch->in_use = false;
    batch->next_free = dom->free_batch_head;
    dom->free_batch_head = batch_slot;
}

/*===========================================================================
 * Content walk — validate a producer span, index its record offsets
 *=========================================================================*/

static struct yetty_ydraw_drawable_list_registry_ptr_result dom_registry(
    struct yetty_yscene_dom *dom)
{
    if (dom->registry) {
        return YETTY_OK(yetty_ydraw_drawable_list_registry_ptr, dom->registry);
    }
    struct yetty_ydraw_drawable_list_registry_ptr_result created_res =
        yetty_ydraw_drawable_list_registry_create_default();
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list_registry_ptr, created_res,
                        "yscene dom: default registry create");
    dom->registry = created_res.value;
    dom->owns_registry = true;
    return YETTY_OK(yetty_ydraw_drawable_list_registry_ptr, dom->registry);
}

/* A staged-but-not-yet-installed span: everything a batch needs, built
 * up-front so installation cannot fail. */
struct dom_staged_span {
    uint8_t *bytes;
    size_t bytes_len;
    uint32_t *record_offsets;
    uint32_t record_count;
};

static void dom_staged_span_discard(struct dom_staged_span *staged)
{
    free(staged->bytes);
    free(staged->record_offsets);
    memset(staged, 0, sizeof(*staged));
}

/* Walk `bytes` as concatenated leaf records; copy the span and build its
 * exact-size offset table. Structural commands are rejected — tree shape
 * arrives ONLY through the typed mutation functions, never embedded in a
 * content span. */
static struct yetty_ycore_void_result dom_stage_span(struct yetty_yscene_dom *dom,
                                                     const uint8_t *bytes, size_t bytes_len,
                                                     struct dom_staged_span *staged)
{
    memset(staged, 0, sizeof(*staged));
    if (bytes_len == 0) {
        return YETTY_OK_VOID();
    }
    if (!bytes) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom: non-empty span with NULL bytes");
    }
    if (bytes_len > UINT32_MAX) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom: content span exceeds 4 GiB");
    }
    struct yetty_ydraw_drawable_list_registry_ptr_result registry_res = dom_registry(dom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, registry_res, "yscene dom: content walk registry");

    uint32_t *offsets = NULL;
    uint32_t count = 0;
    uint32_t offsets_capacity = 0;
    uint32_t cursor = 0;

    while (cursor < bytes_len) {
        uint32_t record_type;
        if (bytes_len - cursor < sizeof(record_type)) {
            free(offsets);
            return YETTY_ERR(yetty_ycore_void, "yscene dom: trailing bytes shorter than a record");
        }
        memcpy(&record_type, bytes + cursor, sizeof(record_type));
        if (record_type == YETTY_YDRAW_CMD_GROUP || record_type == YETTY_YDRAW_CMD_GROUP_REF ||
            record_type == YETTY_YDRAW_CMD_ZERO || record_type == YETTY_YDRAW_CMD_DELETE ||
            record_type == YETTY_YDRAW_CMD_UPDATE) {
            free(offsets);
            return YETTY_ERR(yetty_ycore_void,
                             "yscene dom: structural command embedded in content span "
                             "(use the typed mutation functions)");
        }
        struct yetty_ydraw_command command;
        struct yetty_ycore_size_result consumed_res = yetty_ydraw_drawable_command_parse(
            registry_res.value, bytes + cursor, (uint32_t)(bytes_len - cursor), &command);
        if (YETTY_IS_ERR(consumed_res)) {
            free(offsets);
            return YETTY_ERR(yetty_ycore_void, "yscene dom: content record parse", consumed_res);
        }
        if (consumed_res.value == 0) {
            free(offsets);
            return YETTY_ERR(yetty_ycore_void, "yscene dom: zero-size content record");
        }
        if (count == offsets_capacity) {
            uint32_t new_capacity = offsets_capacity ? offsets_capacity * 2 : 16;
            uint32_t *grown = realloc(offsets, (size_t)new_capacity * sizeof(uint32_t));
            if (!grown) {
                free(offsets);
                return YETTY_ERR(yetty_ycore_void, "yscene dom: record offset table alloc failed");
            }
            offsets = grown;
            offsets_capacity = new_capacity;
        }
        offsets[count++] = cursor;
        cursor += (uint32_t)consumed_res.value;
    }
    if (cursor != bytes_len) {
        free(offsets);
        return YETTY_ERR(yetty_ycore_void, "yscene dom: content span ends mid-record");
    }

    uint8_t *span_copy = malloc(bytes_len);
    if (!span_copy) {
        free(offsets);
        return YETTY_ERR(yetty_ycore_void, "yscene dom: content span alloc failed");
    }
    memcpy(span_copy, bytes, bytes_len);

    /* Shrink the offset table to exactly record_count entries — growth
     * slack on a long-lived span is dead weight. */
    if (count > 0 && count < offsets_capacity) {
        uint32_t *exact = realloc(offsets, (size_t)count * sizeof(uint32_t));
        if (exact) {
            offsets = exact;
        }
    }

    staged->bytes = span_copy;
    staged->bytes_len = bytes_len;
    staged->record_offsets = offsets;
    staged->record_count = count;
    return YETTY_OK_VOID();
}

/* Reserve room for one more batch slot in the node's list (before any
 * destructive step). */
static struct yetty_ycore_void_result dom_batch_list_reserve(struct yetty_yscene_dom *dom,
                                                             uint32_t slot)
{
    struct yetty_yscene_dom_node *node = &dom->nodes[slot];
    if (node->batch_count < node->batch_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_capacity = node->batch_capacity ? node->batch_capacity * 2 : 2;
    uint32_t *grown = realloc(node->batch_slots, (size_t)new_capacity * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom: batch slot list alloc failed");
    }
    node->batch_slots = grown;
    node->batch_capacity = new_capacity;
    return YETTY_OK_VOID();
}

/* Install a staged span as a fresh batch owned by `slot`, carrying
 * `paint_seq`. Consumes the staged span; cannot fail (the batch slot is
 * pre-allocated by the caller). */
static void dom_batch_install(struct yetty_yscene_dom *dom, uint32_t slot, uint32_t batch_slot,
                              uint32_t paint_seq, struct dom_staged_span *staged)
{
    struct yetty_yscene_dom_batch *batch = &dom->batches[batch_slot];
    batch->bytes = staged->bytes;
    batch->bytes_len = staged->bytes_len;
    batch->record_offsets = staged->record_offsets;
    batch->record_count = staged->record_count;
    batch->paint_seq = paint_seq;
    batch->owner_seq = dom->nodes[slot].node_seq;
    batch->live_generation = dom->committed_generation + 1;
    memset(staged, 0, sizeof(*staged));
}

/*===========================================================================
 * Slot resolution + queries
 *=========================================================================*/

static struct yetty_ycore_uint32_result dom_resolve(const struct yetty_yscene_dom *dom,
                                                    uint64_t external_id)
{
    if (external_id == 0) {
        return YETTY_OK(yetty_ycore_uint32, YETTY_YSCENE_DOM_ROOT_SLOT);
    }
    uint32_t slot = dom_id_index_find(dom, external_id);
    if (slot == YETTY_YSCENE_DOM_INVALID_SLOT) {
        return YETTY_ERR(yetty_ycore_uint32, "yscene dom: unknown external id");
    }
    return YETTY_OK(yetty_ycore_uint32, slot);
}

struct yetty_ycore_uint32_result yetty_yscene_dom_lookup(const struct yetty_yscene_dom *dom,
                                                         uint64_t external_id)
{
    if (!dom) {
        return YETTY_ERR(yetty_ycore_uint32, "yscene dom lookup: NULL dom");
    }
    return dom_resolve(dom, external_id);
}

uint32_t yetty_yscene_dom_retired_batch_count(const struct yetty_yscene_dom *dom)
{
    uint32_t count = 0;
    uint32_t walk = dom->retired_batch_head;
    while (walk != YETTY_YSCENE_DOM_INVALID_SLOT) {
        count++;
        walk = dom->batches[walk].next_retired;
    }
    return count;
}

/*===========================================================================
 * Lifecycle
 *=========================================================================*/

struct yetty_yscene_dom_ptr_result yetty_yscene_dom_create(
    struct yetty_ydraw_drawable_list_registry *registry)
{
    struct yetty_yscene_dom *dom = calloc(1, sizeof(struct yetty_yscene_dom));
    if (!dom) {
        return YETTY_ERR(yetty_yscene_dom_ptr, "yscene dom create: calloc failed");
    }
    dom->registry = registry;
    dom->owns_registry = false;
    dom->free_node_head = YETTY_YSCENE_DOM_INVALID_SLOT;
    dom->free_batch_head = YETTY_YSCENE_DOM_INVALID_SLOT;
    dom->retired_batch_head = YETTY_YSCENE_DOM_INVALID_SLOT;

    /* Mint the root at slot 0 (external_id 0, node_seq 0). */
    struct yetty_ycore_uint32_result root_res = dom_node_alloc(dom);
    if (YETTY_IS_ERR(root_res)) {
        free(dom);
        return YETTY_ERR(yetty_yscene_dom_ptr, "yscene dom create: root alloc", root_res);
    }
    return YETTY_OK(yetty_yscene_dom_ptr, dom);
}

/* Bind (or rebind) the borrowed wire-record registry. Permitted only
 * while the dom has never held content — record strides must not change
 * under existing spans. An owned lazy-default registry is released. */
struct yetty_ycore_void_result yetty_yscene_dom_set_registry(
    struct yetty_yscene_dom *dom, struct yetty_ydraw_drawable_list_registry *registry)
{
    if (dom->batch_high_water > 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "yscene dom set_registry: rejected — content already exists");
    }
    if (dom->owns_registry && dom->registry && dom->registry != registry) {
        yetty_ydraw_drawable_list_registry_destroy(dom->registry);
    }
    dom->registry = registry;
    dom->owns_registry = false;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_destroy(struct yetty_yscene_dom *dom)
{
    if (!dom) {
        return YETTY_OK_VOID();
    }
    for (uint32_t i = 0; i < dom->node_high_water; i++) {
        free(dom->nodes[i].children);
        free(dom->nodes[i].batch_slots);
    }
    free(dom->nodes);
    for (uint32_t i = 0; i < dom->batch_high_water; i++) {
        free(dom->batches[i].bytes);
        free(dom->batches[i].record_offsets);
    }
    free(dom->batches);
    free(dom->id_index);
    free(dom->dirty_slots);
    if (dom->owns_registry && dom->registry) {
        yetty_ydraw_drawable_list_registry_destroy(dom->registry);
    }
    free(dom);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Tree mutation
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yscene_dom_node_declare(struct yetty_yscene_dom *dom,
                                                             uint64_t external_id,
                                                             uint64_t parent_external_id)
{
    if (external_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom node_declare: id 0 is the root");
    }
    if (external_id == YSCENE_DOM_ID_TOMBSTONE) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom node_declare: id UINT64_MAX is reserved");
    }
    struct yetty_ycore_uint32_result parent_res = dom_resolve(dom, parent_external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_res, "yscene dom node_declare: parent");
    uint32_t parent_slot = parent_res.value;

    uint32_t existing_slot = dom_id_index_find(dom, external_id);
    if (existing_slot != YETTY_YSCENE_DOM_INVALID_SLOT) {
        struct yetty_yscene_dom_node *node = &dom->nodes[existing_slot];
        if (node->parent_slot == parent_slot) {
            return YETTY_OK_VOID();
        }
        /* Subtree MOVE. Validate + reserve before any destructive step:
         * the new parent must not sit inside the moving subtree, and
         * its child list must already have room — after detach, attach
         * cannot be allowed to fail. */
        if (dom_is_self_or_ancestor(dom, existing_slot, parent_slot)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yscene dom node_declare: move would create a cycle");
        }
        struct yetty_ycore_void_result reserve_res = dom_child_reserve(dom, parent_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "yscene dom node_declare: reserve");
        /* The paint hole at the old location and the appearance at the
         * new one both need re-derivation. */
        dom_bubble_subtree_dirty(dom, node->parent_slot);
        dom_child_detach(dom, existing_slot);
        dom_child_attach(dom, parent_slot, existing_slot);
        dom_mark_dirty(dom, existing_slot, /*placement=*/true, /*content=*/false);
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result reserve_res = dom_child_reserve(dom, parent_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "yscene dom node_declare: reserve");
    struct yetty_ycore_uint32_result node_res = dom_node_alloc(dom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, node_res, "yscene dom node_declare: alloc");
    uint32_t slot = node_res.value;
    dom->nodes[slot].external_id = external_id;
    struct yetty_ycore_void_result index_res = dom_id_index_insert(dom, external_id, slot);
    if (YETTY_IS_ERR(index_res)) {
        dom_node_release(dom, slot);
        return YETTY_ERR(yetty_ycore_void, "yscene dom node_declare: index", index_res);
    }
    dom_child_attach(dom, parent_slot, slot);
    dom_mark_dirty(dom, slot, /*placement=*/true, /*content=*/false);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_node_set_placement(
    struct yetty_yscene_dom *dom, uint64_t external_id,
    const struct yetty_yscene_dom_placement *placement)
{
    if (!placement) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom set_placement: NULL placement");
    }
    if (!isfinite(placement->m00) || !isfinite(placement->m01) || !isfinite(placement->m10) ||
        !isfinite(placement->m11) || !isfinite(placement->translate_x) ||
        !isfinite(placement->translate_y) || !isfinite(placement->opacity)) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom set_placement: non-finite value");
    }
    if (placement->has_clip &&
        (!isfinite(placement->clip.min.x) || !isfinite(placement->clip.min.y) ||
         !isfinite(placement->clip.max.x) || !isfinite(placement->clip.max.y) ||
         placement->clip.max.x < placement->clip.min.x ||
         placement->clip.max.y < placement->clip.min.y)) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom set_placement: malformed clip rect");
    }
    struct yetty_ycore_uint32_result slot_res = dom_resolve(dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene dom set_placement: node");
    struct yetty_yscene_dom_node *node = &dom->nodes[slot_res.value];
    node->paint_z = placement->paint_z;
    node->m00 = placement->m00;
    node->m01 = placement->m01;
    node->m10 = placement->m10;
    node->m11 = placement->m11;
    node->translate_x = placement->translate_x;
    node->translate_y = placement->translate_y;
    node->has_clip = placement->has_clip;
    node->clip = placement->clip;
    node->opacity = placement->opacity < 0.0f   ? 0.0f
                    : placement->opacity > 1.0f ? 1.0f
                                                : placement->opacity;
    dom_mark_dirty(dom, slot_res.value, /*placement=*/true, /*content=*/false);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_node_set_content(struct yetty_yscene_dom *dom,
                                                                 uint64_t external_id,
                                                                 const uint8_t *bytes,
                                                                 size_t bytes_len)
{
    struct yetty_ycore_uint32_result slot_res = dom_resolve(dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene dom set_content: node");
    uint32_t slot = slot_res.value;
    struct yetty_yscene_dom_node *node = &dom->nodes[slot];

    if (bytes_len == 0) {
        /* Clear. */
        for (uint32_t i = 0; i < node->batch_count; i++) {
            dom_batch_retire(dom, node->batch_slots[i]);
        }
        node->batch_count = 0;
        dom_mark_dirty(dom, slot, /*placement=*/false, /*content=*/true);
        return YETTY_OK_VOID();
    }

    /* Stage everything fallible first. */
    struct dom_staged_span staged;
    struct yetty_ycore_void_result stage_res = dom_stage_span(dom, bytes, bytes_len, &staged);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stage_res, "yscene dom set_content: span");
    struct yetty_ycore_void_result reserve_res = dom_batch_list_reserve(dom, slot);
    if (YETTY_IS_ERR(reserve_res)) {
        dom_staged_span_discard(&staged);
        return YETTY_ERR(yetty_ycore_void, "yscene dom set_content: reserve", reserve_res);
    }
    struct yetty_ycore_uint32_result batch_res = dom_batch_alloc(dom);
    if (YETTY_IS_ERR(batch_res)) {
        dom_staged_span_discard(&staged);
        return YETTY_ERR(yetty_ycore_void, "yscene dom set_content: batch alloc", batch_res);
    }

    /* Wholesale replace keeps the content's paint position: the new
     * span inherits the previous FIRST batch's paint_seq (wire-protocol
     * re-emission repaints at its original depth). A node's FIRST
     * content anchors at the node's own seq — declaration position —
     * so the typed sequence "append parent A, declare child, append
     * parent B, then set child content" still interleaves A < child
     * < B regardless of when the child content arrives. */
    uint32_t paint_seq;
    if (node->batch_count > 0) {
        paint_seq = dom->batches[node->batch_slots[0]].paint_seq;
        for (uint32_t i = 0; i < node->batch_count; i++) {
            dom_batch_retire(dom, node->batch_slots[i]);
        }
        node->batch_count = 0;
    } else {
        paint_seq = node->node_seq;
    }
    dom_batch_install(dom, slot, batch_res.value, paint_seq, &staged);
    node->batch_slots[node->batch_count++] = batch_res.value;
    dom_mark_dirty(dom, slot, /*placement=*/false, /*content=*/true);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_node_append_batch(struct yetty_yscene_dom *dom,
                                                                  uint64_t external_id,
                                                                  const uint8_t *bytes,
                                                                  size_t bytes_len)
{
    struct yetty_ycore_uint32_result slot_res = dom_resolve(dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene dom append_batch: node");
    uint32_t slot = slot_res.value;
    if (bytes_len == 0) {
        return YETTY_OK_VOID();
    }

    struct dom_staged_span staged;
    struct yetty_ycore_void_result stage_res = dom_stage_span(dom, bytes, bytes_len, &staged);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stage_res, "yscene dom append_batch: span");
    struct yetty_ycore_void_result reserve_res = dom_batch_list_reserve(dom, slot);
    if (YETTY_IS_ERR(reserve_res)) {
        dom_staged_span_discard(&staged);
        return YETTY_ERR(yetty_ycore_void, "yscene dom append_batch: reserve", reserve_res);
    }
    struct yetty_ycore_uint32_result batch_res = dom_batch_alloc(dom);
    if (YETTY_IS_ERR(batch_res)) {
        dom_staged_span_discard(&staged);
        return YETTY_ERR(yetty_ycore_void, "yscene dom append_batch: batch alloc", batch_res);
    }
    struct yetty_yscene_dom_node *node = &dom->nodes[slot];
    /* First content anchors at the node's declaration position; later
     * appends sort where they are declared. */
    uint32_t paint_seq = node->batch_count == 0 ? node->node_seq : dom->next_seq++;
    dom_batch_install(dom, slot, batch_res.value, paint_seq, &staged);
    node->batch_slots[node->batch_count++] = batch_res.value;
    dom_mark_dirty(dom, slot, /*placement=*/false, /*content=*/true);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_node_replace_batch(struct yetty_yscene_dom *dom,
                                                                   uint64_t external_id,
                                                                   uint32_t batch_index,
                                                                   const uint8_t *bytes,
                                                                   size_t bytes_len)
{
    struct yetty_ycore_uint32_result slot_res = dom_resolve(dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene dom replace_batch: node");
    uint32_t slot = slot_res.value;
    struct yetty_yscene_dom_node *node = &dom->nodes[slot];
    if (batch_index >= node->batch_count) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom replace_batch: index out of range");
    }
    if (bytes_len == 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "yscene dom replace_batch: empty span (use remove_batch)");
    }

    struct dom_staged_span staged;
    struct yetty_ycore_void_result stage_res = dom_stage_span(dom, bytes, bytes_len, &staged);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, stage_res, "yscene dom replace_batch: span");
    struct yetty_ycore_uint32_result batch_res = dom_batch_alloc(dom);
    if (YETTY_IS_ERR(batch_res)) {
        dom_staged_span_discard(&staged);
        return YETTY_ERR(yetty_ycore_void, "yscene dom replace_batch: batch alloc", batch_res);
    }
    /* The replacement span sorts exactly where the replaced one did. */
    uint32_t old_batch_slot = node->batch_slots[batch_index];
    uint32_t preserved_seq = dom->batches[old_batch_slot].paint_seq;
    dom_batch_retire(dom, old_batch_slot);
    dom_batch_install(dom, slot, batch_res.value, preserved_seq, &staged);
    node->batch_slots[batch_index] = batch_res.value;
    dom_mark_dirty(dom, slot, /*placement=*/false, /*content=*/true);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_node_remove_batch(struct yetty_yscene_dom *dom,
                                                                  uint64_t external_id,
                                                                  uint32_t batch_index)
{
    struct yetty_ycore_uint32_result slot_res = dom_resolve(dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene dom remove_batch: node");
    struct yetty_yscene_dom_node *node = &dom->nodes[slot_res.value];
    if (batch_index >= node->batch_count) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom remove_batch: index out of range");
    }
    dom_batch_retire(dom, node->batch_slots[batch_index]);
    memmove(&node->batch_slots[batch_index], &node->batch_slots[batch_index + 1],
            (size_t)(node->batch_count - batch_index - 1) * sizeof(uint32_t));
    node->batch_count--;
    dom_mark_dirty(dom, slot_res.value, /*placement=*/false, /*content=*/true);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_node_delete(struct yetty_yscene_dom *dom,
                                                            uint64_t external_id)
{
    if (external_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom node_delete: cannot delete the root");
    }
    struct yetty_ycore_uint32_result slot_res = dom_resolve(dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene dom node_delete: node");
    uint32_t doomed_slot = slot_res.value;

    /* Phase 1 — collect the whole subtree into a list BEFORE any
     * destructive step, so an allocation failure cannot leave a
     * partially deleted tree. `collected` doubles as the BFS worklist:
     * entries before `scanned` are done, entries after it are pending. */
    uint32_t *collected = malloc(16 * sizeof(uint32_t));
    if (!collected) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom node_delete: scratch alloc");
    }
    uint32_t collected_count = 0;
    uint32_t collected_capacity = 16;
    uint32_t scanned = 0;
    collected[collected_count++] = doomed_slot;
    while (scanned < collected_count) {
        const struct yetty_yscene_dom_node *node = &dom->nodes[collected[scanned++]];
        for (uint32_t i = 0; i < node->child_count; i++) {
            if (collected_count == collected_capacity) {
                uint32_t new_capacity = collected_capacity * 2;
                uint32_t *grown = realloc(collected, (size_t)new_capacity * sizeof(uint32_t));
                if (!grown) {
                    free(collected);
                    return YETTY_ERR(yetty_ycore_void, "yscene dom node_delete: scratch alloc");
                }
                collected = grown;
                collected_capacity = new_capacity;
            }
            collected[collected_count++] = node->children[i];
        }
    }

    /* Phase 2 — destructive, allocation-free. The paint hole left
     * behind is at the parent. */
    dom_bubble_subtree_dirty(dom, dom->nodes[doomed_slot].parent_slot);
    dom_child_detach(dom, doomed_slot);
    for (uint32_t i = 0; i < collected_count; i++) {
        struct yetty_yscene_dom_node *node = &dom->nodes[collected[i]];
        for (uint32_t batch_index = 0; batch_index < node->batch_count; batch_index++) {
            dom_batch_retire(dom, node->batch_slots[batch_index]);
        }
        node->batch_count = 0;
        node->child_count = 0;
        dom_id_index_remove(dom, node->external_id);
        dom_node_release(dom, collected[i]);
    }
    free(collected);
    dom->has_pending = true;
    return YETTY_OK_VOID();
}

/* Allocation-FREE delete of a LEAF node (review #15): the rollback path
 * for staged rich nodes — they carry batches, never child nodes — must be
 * infallible under the exact allocator pressure that triggered the
 * rollback. No scratch, no BFS: resolve, verify leafness, run the
 * destructive phase for the single node. */
struct yetty_ycore_void_result yetty_yscene_dom_node_delete_leaf(struct yetty_yscene_dom *dom,
                                                                 uint64_t external_id)
{
    if (external_id == 0) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom node_delete_leaf: cannot delete the root");
    }
    struct yetty_ycore_uint32_result slot_res = dom_resolve(dom, external_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "yscene dom node_delete_leaf: node");
    uint32_t doomed_slot = slot_res.value;
    struct yetty_yscene_dom_node *node = &dom->nodes[doomed_slot];
    if (node->child_count != 0) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom node_delete_leaf: node has children");
    }
    dom_bubble_subtree_dirty(dom, node->parent_slot);
    dom_child_detach(dom, doomed_slot);
    for (uint32_t batch_index = 0; batch_index < node->batch_count; ++batch_index) {
        dom_batch_retire(dom, node->batch_slots[batch_index]);
    }
    node->batch_count = 0;
    dom_id_index_remove(dom, node->external_id);
    dom_node_release(dom, doomed_slot);
    dom->has_pending = true;
    return YETTY_OK_VOID();
}

/* Delete SEVERAL subtrees ATOMICALLY (review #14): every subtree is
 * collected (the only fallible phase) before ANY destructive step, so an
 * allocation failure leaves the whole set untouched — the caller's old
 * world survives every fallible pre-commit step. */
struct yetty_ycore_void_result yetty_yscene_dom_nodes_delete_atomic(struct yetty_yscene_dom *dom,
                                                                    const uint64_t *external_ids,
                                                                    uint32_t id_count)
{
    if (id_count == 0) {
        return YETTY_OK_VOID();
    }
    /* Phase 1 — resolve + collect every subtree into one arena. */
    uint32_t *doomed_slots = malloc((size_t)id_count * sizeof(uint32_t));
    if (!doomed_slots) {
        return YETTY_ERR(yetty_ycore_void, "yscene dom delete_atomic: slot arena");
    }
    uint32_t collected_capacity = 64;
    uint32_t *collected = malloc((size_t)collected_capacity * sizeof(uint32_t));
    if (!collected) {
        free(doomed_slots);
        return YETTY_ERR(yetty_ycore_void, "yscene dom delete_atomic: scratch");
    }
    uint32_t *subtree_starts = malloc(((size_t)id_count + 1) * sizeof(uint32_t));
    if (!subtree_starts) {
        free(collected);
        free(doomed_slots);
        return YETTY_ERR(yetty_ycore_void, "yscene dom delete_atomic: starts");
    }
    uint32_t collected_count = 0;
    for (uint32_t id_index = 0; id_index < id_count; ++id_index) {
        struct yetty_ycore_uint32_result slot_res = dom_resolve(dom, external_ids[id_index]);
        if (YETTY_IS_ERR(slot_res)) {
            free(subtree_starts);
            free(collected);
            free(doomed_slots);
            return YETTY_ERR(yetty_ycore_void, "yscene dom delete_atomic: resolve", slot_res);
        }
        doomed_slots[id_index] = slot_res.value;
        subtree_starts[id_index] = collected_count;
        uint32_t scanned = collected_count;
        if (collected_count == collected_capacity) {
            uint32_t new_capacity = collected_capacity * 2;
            uint32_t *grown = realloc(collected, (size_t)new_capacity * sizeof(uint32_t));
            if (!grown) {
                free(subtree_starts);
                free(collected);
                free(doomed_slots);
                return YETTY_ERR(yetty_ycore_void, "yscene dom delete_atomic: scratch grow");
            }
            collected = grown;
            collected_capacity = new_capacity;
        }
        collected[collected_count++] = slot_res.value;
        while (scanned < collected_count) {
            const struct yetty_yscene_dom_node *node = &dom->nodes[collected[scanned++]];
            for (uint32_t child = 0; child < node->child_count; ++child) {
                if (collected_count == collected_capacity) {
                    uint32_t new_capacity = collected_capacity * 2;
                    uint32_t *grown = realloc(collected, (size_t)new_capacity * sizeof(uint32_t));
                    if (!grown) {
                        free(subtree_starts);
                        free(collected);
                        free(doomed_slots);
                        return YETTY_ERR(yetty_ycore_void,
                                         "yscene dom delete_atomic: scratch grow");
                    }
                    collected = grown;
                    collected_capacity = new_capacity;
                }
                collected[collected_count++] = node->children[child];
            }
        }
    }
    subtree_starts[id_count] = collected_count;
    /* Phase 2 — destructive, allocation-free, for every subtree. */
    for (uint32_t id_index = 0; id_index < id_count; ++id_index) {
        uint32_t doomed_slot = doomed_slots[id_index];
        dom_bubble_subtree_dirty(dom, dom->nodes[doomed_slot].parent_slot);
        dom_child_detach(dom, doomed_slot);
        for (uint32_t i = subtree_starts[id_index]; i < subtree_starts[id_index + 1]; ++i) {
            struct yetty_yscene_dom_node *node = &dom->nodes[collected[i]];
            for (uint32_t batch_index = 0; batch_index < node->batch_count; ++batch_index) {
                dom_batch_retire(dom, node->batch_slots[batch_index]);
            }
            node->batch_count = 0;
            node->child_count = 0;
            dom_id_index_remove(dom, node->external_id);
            dom_node_release(dom, collected[i]);
        }
    }
    free(subtree_starts);
    free(collected);
    free(doomed_slots);
    dom->has_pending = true;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_zero(struct yetty_yscene_dom *dom)
{
    for (uint32_t slot = 0; slot < dom->node_high_water; slot++) {
        struct yetty_yscene_dom_node *node = &dom->nodes[slot];
        if (!node->in_use) {
            continue;
        }
        for (uint32_t i = 0; i < node->batch_count; i++) {
            dom_batch_retire(dom, node->batch_slots[i]);
        }
        node->batch_count = 0;
        if (slot != YETTY_YSCENE_DOM_ROOT_SLOT) {
            dom_node_release(dom, slot);
        }
    }
    /* Rebuild the id index empty (every live entry pointed at a
     * released node). */
    if (dom->id_index) {
        memset(dom->id_index, 0,
               (size_t)dom->id_index_capacity * sizeof(struct yetty_yscene_dom_id_entry));
    }
    dom->id_index_live = 0;
    dom->id_index_occupied = 0;

    /* Stale pre-zero dirty_slots entries are fine: clear_dirty skips
     * released slots, and the root is re-recorded below. */
    struct yetty_yscene_dom_node *root = &dom->nodes[YETTY_YSCENE_DOM_ROOT_SLOT];
    uint32_t preserved_seq = root->node_seq;
    dom_node_reset(root);
    root->node_seq = preserved_seq;
    /* The reset wiped the root's placement too — both dirty kinds. */
    dom_mark_dirty(dom, YETTY_YSCENE_DOM_ROOT_SLOT, /*placement=*/true, /*content=*/true);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Versioning
 *=========================================================================*/

struct yetty_ycore_uint64_result yetty_yscene_dom_commit(struct yetty_yscene_dom *dom)
{
    if (dom->has_pending) {
        dom->committed_generation++;
        dom->has_pending = false;
    }
    return YETTY_OK(yetty_ycore_uint64, dom->committed_generation);
}

struct yetty_ycore_void_result yetty_yscene_dom_reclaim(struct yetty_yscene_dom *dom,
                                                        uint64_t derived_generation)
{
    uint32_t *link = &dom->retired_batch_head;
    while (*link != YETTY_YSCENE_DOM_INVALID_SLOT) {
        uint32_t batch_slot = *link;
        struct yetty_yscene_dom_batch *batch = &dom->batches[batch_slot];
        if (batch->retire_generation <= derived_generation) {
            *link = batch->next_retired;
            dom_batch_free(dom, batch_slot);
        } else {
            link = &batch->next_retired;
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yscene_dom_clear_dirty(struct yetty_yscene_dom *dom)
{
    if (dom->dirty_slot_count == UINT32_MAX) {
        /* The record list overflowed (allocation failure while marking)
         * — fall back to the full sweep once. */
        for (uint32_t slot = 0; slot < dom->node_high_water; slot++) {
            struct yetty_yscene_dom_node *node = &dom->nodes[slot];
            node->placement_dirty = false;
            node->content_dirty = false;
            node->subtree_dirty = false;
        }
        dom->dirty_slot_count = 0;
        return YETTY_OK_VOID();
    }
    for (uint32_t i = 0; i < dom->dirty_slot_count; i++) {
        uint32_t slot = dom->dirty_slots[i];
        if (slot >= dom->node_high_water || !dom->nodes[slot].in_use) {
            continue;
        }
        struct yetty_yscene_dom_node *node = &dom->nodes[slot];
        node->placement_dirty = false;
        node->content_dirty = false;
        node->subtree_dirty = false;
    }
    dom->dirty_slot_count = 0;
    return YETTY_OK_VOID();
}
