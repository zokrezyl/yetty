/*
 * rich-store.c — rich-block store implementation. See rich-store.h for the
 * model. No libvterm, no GPU, no grid types: blocks + records + the slot
 * pool with generation-checked handles.
 */
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
/* Wire-format metadata accessors (all GPU-less; none of these pull the
 * clashing `struct yetty_ydraw_complex` definition — that stays opaque). */
#include <yetty/ydraw-list/complex.h>
#include <yetty/ydraw-list/font-resource.h>
#include <yetty/ydraw-list/text-drawable-list.h>
#include <yetty/ysdf/handler.h>

#include "rich-store.h"

/* Hand-declared from ydraw-factory (its header defines the clashing
 * `struct yetty_ydraw_complex` — same avoidance as grid.c /
 * scroll-tiers.c). */
void yetty_ydraw_complex_destroy(struct yetty_ydraw_complex *complex);

#define RICH_STORE_INVALID_SLOT UINT32_MAX

/* Membership/key change on `screen`: any cached render plan covering that
 * screen must rebuild. */
static void store_bump_paint_generation(struct yetty_yvterm_rich_store *store,
                                        enum yetty_yvterm_rich_block_screen screen)
{
    store->paint_generation[screen]++;
}

void yetty_yvterm_rich_store_init(struct yetty_yvterm_rich_store *store)
{
    memset(store, 0, sizeof(*store));
    store->next_paint_sequence = 1u;
}

bool yetty_yvterm_rich_store_push_paint_z(struct yetty_yvterm_rich_store *store, int32_t z)
{
    uint32_t capacity = sizeof(store->ambient_paint_z) / sizeof(store->ambient_paint_z[0]);
    if (store->ambient_paint_z_depth >= capacity) {
        /* Bounded stack — deeper nesting keeps the innermost accepted z, but
         * remember the ignored push so its matching pop is absorbed rather than
         * popping a real accepted entry. The false return makes the overflow
         * OBSERVABLE: a caller that requires its z to take effect (local
         * chrome replacement) can abort instead of stamping records with a
         * stranger scope's depth. */
        store->ambient_paint_z_overflow++;
        return false;
    }
    store->ambient_paint_z[store->ambient_paint_z_depth++] = z;
    return true;
}

void yetty_yvterm_rich_store_pop_paint_z(struct yetty_yvterm_rich_store *store)
{
    if (store->ambient_paint_z_overflow > 0) {
        store->ambient_paint_z_overflow--; /* cancel an ignored overflow push */
        return;
    }
    if (store->ambient_paint_z_depth > 0) {
        store->ambient_paint_z_depth--;
    }
}

void yetty_yvterm_rich_store_reset_paint_z(struct yetty_yvterm_rich_store *store)
{
    store->ambient_paint_z_depth = 0;
    store->ambient_paint_z_overflow = 0;
}

static void record_free_journal(struct yetty_yvterm_rich_store *store,
                                struct yetty_yvterm_rich_record *record)
{
    if (record->journal) {
        size_t bytes = (size_t)record->journal_capacity * sizeof(uint32_t);
        store->journal_bytes_used =
            store->journal_bytes_used > bytes ? store->journal_bytes_used - bytes : 0u;
        free(record->journal);
        record->journal = NULL;
    }
    record->journal_count = 0;
    record->journal_capacity = 0;
}

static void block_free_contents_store(struct yetty_yvterm_rich_store *store,
                                      struct yetty_yvterm_rich_block *block)
{
    for (uint32_t index = 0; index < block->record_count; ++index) {
        if (block->records[index].complex_runtime) {
            yetty_ydraw_complex_destroy(block->records[index].complex_runtime);
            block->records[index].complex_runtime = NULL;
        }
        record_free_journal(store, &block->records[index]);
    }
    free(block->records);
    block->records = NULL;
    block->record_count = 0;
    block->record_capacity = 0;
    free(block->groups);
    block->groups = NULL;
    block->group_count = 0;
    block->group_capacity = 0;
    free(block->reusable_slots);
    block->reusable_slots = NULL;
    block->reusable_count = 0;
    block->reusable_capacity = 0;
    block->topology_generation++;
    block->reusable_generation = block->topology_generation;
    free(block->arena);
    block->arena = NULL;
    block->arena_count = 0;
    block->arena_capacity = 0;
}

void yetty_yvterm_rich_store_destroy(struct yetty_yvterm_rich_store *store)
{
    for (uint32_t slot = 0; slot < store->block_capacity; ++slot) {
        if (store->blocks[slot].in_use) {
            block_free_contents_store(store, &store->blocks[slot]);
        }
    }
    free(store->blocks);
    free(store->free_slots);
    memset(store, 0, sizeof(*store));
}

static struct yetty_ycore_void_result store_reserve_slot(struct yetty_yvterm_rich_store *store,
                                                         uint32_t *out_slot)
{
    if (store->free_count) {
        *out_slot = store->free_slots[--store->free_count];
        return YETTY_OK_VOID();
    }
    /* Grow the block pool; generations survive in place because the array is
     * reallocated, never reset. */
    if (store->block_capacity == UINT32_MAX) {
        return YETTY_ERR(yetty_ycore_void, "rich store: block pool exhausted");
    }
    uint32_t new_capacity = store->block_capacity ? store->block_capacity * 2u : 16u;
    struct yetty_yvterm_rich_block *grown =
        realloc(store->blocks, (size_t)new_capacity * sizeof(struct yetty_yvterm_rich_block));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "rich store: block pool grow failed");
    }
    memset(grown + store->block_capacity, 0,
           (size_t)(new_capacity - store->block_capacity) * sizeof(struct yetty_yvterm_rich_block));
    store->blocks = grown;
    /* Newly minted slots go onto the free list back-to-front so allocation
     * hands them out in ascending order. */
    uint32_t added = new_capacity - store->block_capacity;
    uint32_t *free_grown =
        realloc(store->free_slots, (size_t)(store->free_capacity + added) * sizeof(uint32_t));
    if (!free_grown) {
        return YETTY_ERR(yetty_ycore_void, "rich store: free list grow failed");
    }
    store->free_slots = free_grown;
    store->free_capacity += added;
    for (uint32_t slot = new_capacity; slot > store->block_capacity + 1u; --slot) {
        store->free_slots[store->free_count++] = slot - 1u;
    }
    *out_slot = store->block_capacity;
    store->block_capacity = new_capacity;
    return YETTY_OK_VOID();
}

struct yetty_yvterm_rich_handle_result yetty_yvterm_rich_store_block_create(
    struct yetty_yvterm_rich_store *store, enum yetty_yvterm_rich_block_screen screen,
    uint64_t insertion_rolling_row)
{
    uint32_t slot = RICH_STORE_INVALID_SLOT;
    struct yetty_ycore_void_result slot_res = store_reserve_slot(store, &slot);
    YETTY_RETURN_IF_ERR(yetty_yvterm_rich_handle, slot_res, "rich store: block create");

    struct yetty_yvterm_rich_block *block = &store->blocks[slot];
    uint32_t generation = block->generation + 1u;
    memset(block, 0, sizeof(*block));
    block->generation = generation ? generation : 1u; /* 0 is never a live generation */
    block->in_use = 1;
    block->screen = screen;
    block->state = YETTY_YVTERM_RICH_BLOCK_LIVE;
    block->insertion_rolling_row = insertion_rolling_row;
    block->bottom_owner_row = insertion_rolling_row;
    block->span_rows = 0;
    store->live_count++;
    store_bump_paint_generation(store, screen);

    struct yetty_yvterm_rich_handle handle = {.slot = slot, .generation = block->generation};
    return YETTY_OK(yetty_yvterm_rich_handle, handle);
}

struct yetty_yvterm_rich_block *yetty_yvterm_rich_store_resolve(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle)
{
    if (handle.slot >= store->block_capacity) {
        return NULL;
    }
    struct yetty_yvterm_rich_block *block = &store->blocks[handle.slot];
    if (!block->in_use || block->generation != handle.generation) {
        return NULL;
    }
    return block;
}

static struct yetty_ycore_void_result block_ensure_arena(struct yetty_yvterm_rich_block *block,
                                                         uint32_t need)
{
    if (need <= block->arena_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_capacity = block->arena_capacity ? block->arena_capacity : 16u;
    while (new_capacity < need) {
        new_capacity *= 2u;
    }
    uint32_t *grown = realloc(block->arena, (size_t)new_capacity * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "rich store: arena grow failed");
    }
    block->arena = grown;
    block->arena_capacity = new_capacity;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result block_ensure_records(struct yetty_yvterm_rich_block *block,
                                                           uint32_t need)
{
    if (need <= block->record_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_capacity = block->record_capacity ? block->record_capacity * 2u : 4u;
    while (new_capacity < need) {
        new_capacity *= 2u;
    }
    struct yetty_yvterm_rich_record *grown =
        realloc(block->records, (size_t)new_capacity * sizeof(struct yetty_yvterm_rich_record));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "rich store: record table grow failed");
    }
    block->records = grown;
    block->record_capacity = new_capacity;
    return YETTY_OK_VOID();
}

/* Decode the producer's explicit z from the record's wire words. SDF is
 * probed FIRST — an id-carrying SDF prim (top bit + SDF base) must not be
 * mistaken for a complex. Records with no z field (FONT, cmds, complexes
 * until a versioned format adds one) sit on the z-0 plane. */
static int32_t record_extract_paint_z(const uint32_t *words, uint32_t word_count)
{
    if (!words || word_count == 0u) {
        return 0;
    }
    uint32_t type = words[0];
    uint32_t id_words = (type & YETTY_YDRAW_HAS_ID_FLAG) ? 1u : 0u;
    if (yetty_ysdf_primitive_size(type & ~YETTY_YDRAW_HAS_ID_FLAG) > 0u) {
        if (word_count < 2u + id_words) {
            return 0; /* truncated — no z word to read */
        }
        return yetty_ysdf_drawable_paint_z(words);
    }
    if (type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST) {
        struct yetty_ydraw_drawable_list_entry_ops_ptr_result ops_res =
            yetty_ydraw_text_drawable_list_handler(type);
        if (YETTY_IS_ERR(ops_res)) {
            yetty_ycore_error_destroy(ops_res.error);
            return 0;
        }
        return ops_res.value->paint_z ? ops_res.value->paint_z(words) : 0;
    }
    if (yetty_ydraw_is_complex(type)) {
        return yetty_ydraw_complex_record_paint_z(words);
    }
    return 0;
}

/* Nearest group in the parent chain (self included) with a reopen in
 * progress, or 0. Cycle-guarded like the dead-chain walk. */
static uint32_t group_nearest_replacing(const struct yetty_yvterm_rich_block *block,
                                        uint32_t group_slot)
{
    uint32_t guard = 0;
    while (group_slot != 0 && group_slot <= block->group_count && guard++ <= block->group_count) {
        const struct yetty_yvterm_rich_group *group = &block->groups[group_slot - 1u];
        if (group->replacing) {
            return group_slot;
        }
        group_slot = group->parent_slot;
    }
    return 0;
}

/* Mint the sequence/ordinal part of a new record's paint key from the
 * owning group's state. A record anywhere under a replacing subtree stacks
 * at the replaced subtree's anchor sequence with a body-order ordinal from
 * the anchor OWNER's monotone counter — so a structural reopen can never
 * push replacement content past records emitted after the original (the
 * overlay-stacking guarantee), and shared-anchor keys stay unique.
 * Exhaustion is rejected BEFORE any state mutates — a duplicate key must
 * never publish. */
static struct yetty_ycore_void_result record_mint_paint_key(struct yetty_yvterm_rich_store *store,
                                                            struct yetty_yvterm_rich_block *block,
                                                            uint32_t group_slot,
                                                            struct yetty_yvterm_rich_record *record)
{
    struct yetty_yvterm_rich_group *group = group_slot ? &block->groups[group_slot - 1u] : NULL;
    uint32_t replacing_slot = group_nearest_replacing(block, group_slot);
    if (replacing_slot) {
        struct yetty_yvterm_rich_group *replacing_group = &block->groups[replacing_slot - 1u];
        uint32_t owner_slot = replacing_group->anchor_owner_slot
                                  ? replacing_group->anchor_owner_slot
                                  : replacing_slot;
        if (owner_slot > block->group_count) {
            return YETTY_ERR(yetty_ycore_void, "rich store: anchor owner out of range");
        }
        struct yetty_yvterm_rich_group *owner = &block->groups[owner_slot - 1u];
        if (owner->next_replacement_ordinal == UINT32_MAX) {
            return YETTY_ERR(yetty_ycore_void, "rich store: replacement ordinal exhausted");
        }
        record->paint_sequence = replacing_group->anchor_sequence;
        record->record_ordinal = owner->next_replacement_ordinal++;
        return YETTY_OK_VOID();
    }
    if (store->next_paint_sequence == UINT64_MAX) {
        return YETTY_ERR(yetty_ycore_void, "rich store: paint sequence exhausted");
    }
    record->paint_sequence = store->next_paint_sequence++;
    record->record_ordinal = 0u;
    if (group && !group->anchor_from_record) {
        /* The creation body's first direct record fixes the group's
         * replacement anchor. */
        group->anchor_sequence = record->paint_sequence;
        group->anchor_from_record = true;
    }
    return YETTY_OK_VOID();
}

/* Shared append body: validates, stores the wire words, stamps the given
 * paint key. The two public entry points differ only in where the key
 * comes from (minted here vs reproduced from the archive). */
static struct yetty_ycore_void_result block_append_record_keyed(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot, const uint32_t *words, uint32_t word_count, bool mint_key, int32_t paint_z,
    uint64_t paint_sequence, uint32_t record_ordinal)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return YETTY_ERR(yetty_ycore_void, "rich store: append to stale block handle");
    }
    if (word_count && !words) {
        return YETTY_ERR(yetty_ycore_void, "rich store: append with NULL words");
    }
    if (group_slot > block->group_count) {
        return YETTY_ERR(yetty_ycore_void, "rich store: append to unknown group slot");
    }
    struct yetty_ycore_void_result arena_res =
        block_ensure_arena(block, block->arena_count + word_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, arena_res, "rich store: append arena");
    struct yetty_ycore_void_result record_res =
        block_ensure_records(block, block->record_count + 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, record_res, "rich store: append record slot");
    if (block->reusable_count) {
        /* A record can anchor a chain with DEAD links (append into a
         * scope whose path was deleted while open): a cached reuse
         * candidate on that chain must never be recycled afterwards —
         * drop the cache so the next open rescans with this record's
         * chain marked. Walked only while a cache exists. */
        uint32_t walk = group_slot;
        uint32_t guard = 0;
        while (walk != 0 && walk <= block->group_count && guard++ <= block->group_count) {
            const struct yetty_yvterm_rich_group *chain_group = &block->groups[walk - 1u];
            if (!chain_group->alive && !chain_group->retired) {
                block->topology_generation++;
                block->reusable_count = 0;
                break;
            }
            walk = chain_group->parent_slot;
        }
    }

    struct yetty_yvterm_rich_record *record = &block->records[block->record_count];
    memset(record, 0, sizeof(*record));
    record->arena_offset = block->arena_count;
    record->word_count = word_count;
    record->group_slot = group_slot;
    record->alive = true;
    /* An id'd SDF prim carries the top bit too — probe SDF before treating
     * the type word as a complex. FONT resources install fonts but never
     * paint. */
    record->kind = (word_count && yetty_ydraw_is_complex(words[0]) &&
                    yetty_ysdf_primitive_size(words[0] & ~YETTY_YDRAW_HAS_ID_FLAG) == 0u)
                       ? YETTY_YVTERM_RICH_RECORD_COMPLEX
                       : YETTY_YVTERM_RICH_RECORD_PRIMITIVE;
    record->render_leaf = record->kind == YETTY_YVTERM_RICH_RECORD_COMPLEX ||
                          (word_count > 0u && words[0] != YETTY_YDRAW_RESOURCE_FONT);
    if (mint_key) {
        /* An open ambient paint-z scope overrides the record's own wire z,
         * so a whole figure (complex + its label/legend prims) stacks at
         * one depth. */
        record->paint_z = store->ambient_paint_z_depth > 0
                              ? store->ambient_paint_z[store->ambient_paint_z_depth - 1u]
                              : record_extract_paint_z(words, word_count);
        struct yetty_ycore_void_result key_res =
            record_mint_paint_key(store, block, group_slot, record);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, key_res, "rich store: append paint key");
    } else {
        record->paint_z = paint_z;
        record->paint_sequence = paint_sequence;
        record->record_ordinal = record_ordinal;
    }
    if (word_count) {
        memcpy(block->arena + block->arena_count, words, (size_t)word_count * sizeof(uint32_t));
        block->arena_count += word_count;
    }
    block->record_count++;
    store_bump_paint_generation(store, block->screen);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvterm_rich_store_block_append_record(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot, const uint32_t *words, uint32_t word_count)
{
    return block_append_record_keyed(store, handle, group_slot, words, word_count, true, 0, 0u, 0u);
}

struct yetty_ycore_void_result yetty_yvterm_rich_store_block_reserve(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t extra_records, uint32_t extra_words)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return YETTY_ERR(yetty_ycore_void, "rich store: reserve on stale block handle");
    }
    if (extra_words > UINT32_MAX - block->arena_count ||
        extra_records > UINT32_MAX - block->record_count) {
        return YETTY_ERR(yetty_ycore_void, "rich store: reserve overflows the block counters");
    }
    struct yetty_ycore_void_result arena_res =
        block_ensure_arena(block, block->arena_count + extra_words);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, arena_res, "rich store: reserve arena");
    struct yetty_ycore_void_result records_res =
        block_ensure_records(block, block->record_count + extra_records);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, records_res, "rich store: reserve record slots");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvterm_rich_store_block_group_reserve(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot, uint32_t extra_records, uint32_t extra_words)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return YETTY_ERR(yetty_ycore_void, "rich store: group reserve on stale block handle");
    }
    if (group_slot == 0 || group_slot > block->group_count) {
        return YETTY_ERR(yetty_ycore_void, "rich store: group reserve on unknown slot");
    }
    /* Ordinal preflight — mirror record_mint_paint_key's replacement
     * branch: appends during the replace window take the anchor OWNER's
     * next_replacement_ordinal. Reserving the headroom here makes the
     * later per-append exhaustion check unreachable. */
    const struct yetty_yvterm_rich_group *group = &block->groups[group_slot - 1u];
    uint32_t owner_slot = group->anchor_owner_slot ? group->anchor_owner_slot : group_slot;
    if (owner_slot > block->group_count) {
        return YETTY_ERR(yetty_ycore_void, "rich store: group reserve anchor out of range");
    }
    const struct yetty_yvterm_rich_group *owner = &block->groups[owner_slot - 1u];
    if (extra_records >= UINT32_MAX - owner->next_replacement_ordinal) {
        return YETTY_ERR(yetty_ycore_void, "rich store: replacement ordinal headroom exhausted");
    }
    return yetty_yvterm_rich_store_block_reserve(store, handle, extra_records, extra_words);
}

uint32_t yetty_yvterm_rich_store_block_dead_records(struct yetty_yvterm_rich_store *store,
                                                    struct yetty_yvterm_rich_handle handle)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return 0u;
    }
    /* Incremental counter (maintained at every record-death site): the
     * compaction gate runs on every outermost group close, so it must
     * not pay a record-table scan just to learn nothing died. */
    return block->dead_record_count;
}

uint32_t yetty_yvterm_rich_store_block_compact(struct yetty_yvterm_rich_store *store,
                                               struct yetty_yvterm_rich_handle handle,
                                               uint32_t *remap, uint32_t remap_capacity)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block || !remap || remap_capacity < block->record_count) {
        return 0u;
    }
    uint32_t write_index = 0;
    uint32_t arena_write = 0;
    uint32_t dropped = 0;
    for (uint32_t read_index = 0; read_index < block->record_count; ++read_index) {
        struct yetty_yvterm_rich_record *record = &block->records[read_index];
        if (!record->alive) {
            /* Kill paths already destroyed the runtime and freed the
             * journal — only the descriptor and its bytes remain. */
            remap[read_index] = UINT32_MAX;
            dropped++;
            continue;
        }
        remap[read_index] = write_index;
        if (read_index != write_index) {
            block->records[write_index] = *record;
        }
        struct yetty_yvterm_rich_record *packed = &block->records[write_index];
        if (packed->word_count) {
            /* Arena payloads sit in append order, so the pack is a
             * forward, overlap-safe slide. */
            if (packed->arena_offset != arena_write) {
                memmove(block->arena + arena_write, block->arena + packed->arena_offset,
                        (size_t)packed->word_count * sizeof(uint32_t));
                packed->arena_offset = arena_write;
            }
        }
        arena_write += packed->word_count;
        write_index++;
    }
    if (!dropped) {
        return 0u;
    }
    block->record_count = write_index;
    block->arena_count = arena_write;
    block->dead_record_count = 0; /* every dead descriptor was dropped */
    block->topology_generation++; /* record indices moved — rescan reuse */
    block->reusable_count = 0;
    store_bump_paint_generation(store, block->screen);
    return dropped;
}

struct yetty_ycore_void_result yetty_yvterm_rich_store_block_append_record_exact(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot, const uint32_t *words, uint32_t word_count, int32_t paint_z,
    uint64_t paint_sequence, uint32_t record_ordinal)
{
    return block_append_record_keyed(store, handle, group_slot, words, word_count, false, paint_z,
                                     paint_sequence, record_ordinal);
}

/* Mark every group slot reachable from a live anchor: alive records'
 * scope chains and live/retired groups' parent + anchor-owner chains.
 * A DEAD slot outside this set is garbage nothing can ever walk into
 * again — recyclable without rewiring history (retired subtrees keep
 * their whole frozen chain marked through the record/parent walks). */
static void group_mark_chain(const struct yetty_yvterm_rich_block *block, uint8_t *marked,
                             uint32_t slot)
{
    uint32_t guard = 0;
    while (slot != 0 && slot <= block->group_count && guard++ <= block->group_count) {
        if (marked[slot - 1u]) {
            return;
        }
        marked[slot - 1u] = 1;
        slot = block->groups[slot - 1u].parent_slot;
    }
}

/* Rebuild the reusable-slot cache: ONE full scan collecting EVERY dead,
 * unretired slot unreachable from any live anchor. The cache is consumed
 * per group-open (with only a per-pop protect-chain check) until a
 * topology change invalidates it, so an exact-subtree replacement that
 * recreates N nodes pays one scan per replacement window instead of one
 * scan (and one heap mark array) per node. Allocation failure leaves an
 * empty-but-valid cache: reclamation is an optimization, never a
 * correctness dependency. */
static void block_reusable_cache_rebuild(struct yetty_yvterm_rich_block *block)
{
    block->reusable_count = 0;
    block->reusable_generation = block->topology_generation;
    if (!block->group_count || !block->dead_group_count) {
        return;
    }
    /* Candidates are a subset of the dead slots, so dead_group_count is
     * an exact capacity bound. */
    if (block->reusable_capacity < block->dead_group_count) {
        uint32_t *grown =
            realloc(block->reusable_slots, (size_t)block->dead_group_count * sizeof(uint32_t));
        if (!grown) {
            return;
        }
        block->reusable_slots = grown;
        block->reusable_capacity = block->dead_group_count;
    }
    uint8_t *marked = calloc(block->group_count, sizeof(uint8_t));
    if (!marked) {
        return;
    }
    block->reusable_scans++;
    for (uint32_t index = 0; index < block->group_count; ++index) {
        const struct yetty_yvterm_rich_group *group = &block->groups[index];
        if (group->alive || group->retired) {
            group_mark_chain(block, marked, index + 1u);
            group_mark_chain(block, marked, group->anchor_owner_slot);
        }
    }
    for (uint32_t index = 0; index < block->record_count; ++index) {
        const struct yetty_yvterm_rich_record *record = &block->records[index];
        if (record->alive) {
            group_mark_chain(block, marked, record->group_slot);
        }
    }
    for (uint32_t index = 0; index < block->group_count; ++index) {
        const struct yetty_yvterm_rich_group *group = &block->groups[index];
        if (!group->alive && !group->retired && !marked[index] && group->generation < UINT32_MAX &&
            block->reusable_count < block->reusable_capacity) {
            block->reusable_slots[block->reusable_count++] = index + 1u;
        }
    }
    free(marked);
}

/* Whether `candidate` sits on the parent chain of `chain_slot` (dead
 * links included). */
static bool group_on_chain(const struct yetty_yvterm_rich_block *block, uint32_t chain_slot,
                           uint32_t candidate)
{
    uint32_t guard = 0;
    while (chain_slot != 0 && chain_slot <= block->group_count && guard++ <= block->group_count) {
        if (chain_slot == candidate) {
            return true;
        }
        chain_slot = block->groups[chain_slot - 1u].parent_slot;
    }
    return false;
}

/* A recyclable dead group slot, or 0 when none. `protect_chain_slot` is
 * the parent the caller is about to open under: its WHOLE parent chain
 * is excluded even where it passes dead groups — a slot deleted while
 * still on the grid's open-scope stack (delete of an open path) is
 * otherwise unreachable from any live anchor, and recycling it for its
 * own nested child would mint a self-parent cycle that silently swallows
 * the new subtree. The protect chain differs per call, so it is checked
 * at POP time; the reachability half comes from the cached scan. */
static uint32_t block_group_reusable_slot(struct yetty_yvterm_rich_block *block,
                                          uint32_t protect_chain_slot)
{
    /* Pure construction (nothing has died yet) must not pay any scan:
     * without this gate a fresh N-group tree costs O(N^2). */
    if (!block->group_count || !block->dead_group_count) {
        return 0;
    }
    if (block->reusable_generation != block->topology_generation) {
        block_reusable_cache_rebuild(block);
    }
    uint32_t scan = block->reusable_count;
    while (scan > 0) {
        uint32_t candidate = block->reusable_slots[scan - 1u];
        const struct yetty_yvterm_rich_group *group = &block->groups[candidate - 1u];
        bool stale = group->alive || group->retired || group->generation >= UINT32_MAX;
        if (!stale && !group_on_chain(block, protect_chain_slot, candidate)) {
            block->reusable_slots[scan - 1u] = block->reusable_slots[block->reusable_count - 1u];
            block->reusable_count--;
            return candidate;
        }
        if (stale) {
            /* Consumed or frozen since the scan — drop the entry; a
             * PROTECTED candidate instead stays cached for opens under
             * other chains. */
            block->reusable_slots[scan - 1u] = block->reusable_slots[block->reusable_count - 1u];
            block->reusable_count--;
        }
        scan--;
        if (scan > block->reusable_count) {
            scan = block->reusable_count;
        }
    }
    return 0;
}

struct yetty_ycore_uint32_result yetty_yvterm_rich_store_block_group_open(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t parent_slot, bool has_external_id, uint64_t external_key)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return YETTY_ERR(yetty_ycore_uint32, "rich store: group open on stale block handle");
    }
    if (parent_slot > block->group_count) {
        return YETTY_ERR(yetty_ycore_uint32, "rich store: group open under unknown parent");
    }
    /* Recycle a dead, unreferenced slot when one exists — exact-subtree
     * replacements kill descendant groups every reopen, and a long-lived
     * insertion must not grow its group table with the number of frames. */
    uint32_t slot = block_group_reusable_slot(block, parent_slot);
    struct yetty_yvterm_rich_group *group;
    if (slot) {
        group = &block->groups[slot - 1u];
        uint32_t next_generation = group->generation + 1u;
        memset(group, 0, sizeof(*group));
        group->generation = next_generation;
        if (block->dead_group_count) {
            block->dead_group_count--; /* recycled */
        }
    } else {
        if (block->group_count == block->group_capacity) {
            uint32_t new_capacity = block->group_capacity ? block->group_capacity * 2u : 4u;
            struct yetty_yvterm_rich_group *grown = realloc(
                block->groups, (size_t)new_capacity * sizeof(struct yetty_yvterm_rich_group));
            if (!grown) {
                return YETTY_ERR(yetty_ycore_uint32, "rich store: group table grow failed");
            }
            block->groups = grown;
            block->group_capacity = new_capacity;
        }
        group = &block->groups[block->group_count];
        memset(group, 0, sizeof(*group));
        slot = block->group_count + 1u;
    }
    group->parent_slot = parent_slot;
    group->alive = true;
    group->has_external_id = has_external_id;
    group->external_key = external_key;
    uint32_t replacing_slot = group_nearest_replacing(block, parent_slot);
    if (replacing_slot) {
        /* Created inside a replacement body: the whole replaced subtree
         * occupies ONE emission slot, so the fresh group inherits the
         * replacing ancestor's anchor (frozen — no re-election) and its
         * anchor owner's ordinal counter. Without this, a structural reopen
         * would mint the nested group a fresh global sequence and its
         * content could stack above everything emitted since the original
         * (e.g. an overlay created after the app root). */
        const struct yetty_yvterm_rich_group *replacing_group = &block->groups[replacing_slot - 1u];
        group->anchor_sequence = replacing_group->anchor_sequence;
        group->anchor_from_record = true;
        group->anchor_owner_slot = replacing_group->anchor_owner_slot
                                       ? replacing_group->anchor_owner_slot
                                       : replacing_slot;
    } else {
        if (store->next_paint_sequence == UINT64_MAX) {
            group->alive = false; /* roll the fresh slot back */
            if (slot <= block->group_count) {
                block->dead_group_count++; /* a recycled slot is dead again */
                block->topology_generation++;
            }
            return YETTY_ERR(yetty_ycore_uint32, "rich store: paint sequence exhausted");
        }
        /* Declaration sequence: the group's replacement anchor until
         * (unless) its creation body appends a first direct record. */
        group->anchor_sequence = store->next_paint_sequence++;
        group->anchor_from_record = false;
        group->anchor_owner_slot = slot; /* self */
    }
    if (slot > block->group_count) {
        block->group_count++;
    }
    if (block->reusable_count) {
        /* The fresh group ANCHORS its parent chain exactly like a record
         * does — including any DEAD links (a group opened under a scope
         * whose path was deleted while open). A cached reuse candidate on
         * that chain — the dead ancestor itself — must never be recycled
         * afterwards, or a later unrelated open would repoint this
         * child's parent_slot under fresh structure and resurrect its
         * address. The per-pop protect check covered only THIS call's
         * chain; drop the cache AFTER creation so the next rebuild's
         * reachability scan marks the chain through the now-alive child.
         * (An empty-but-valid cache needs nothing: the next rebuild only
         * runs after a topology change and already sees this child.) */
        uint32_t dead_chain_walk = group->parent_slot;
        uint32_t dead_chain_guard = 0;
        while (dead_chain_walk != 0 && dead_chain_walk <= block->group_count &&
               dead_chain_guard++ <= block->group_count) {
            const struct yetty_yvterm_rich_group *chain_group =
                &block->groups[dead_chain_walk - 1u];
            if (!chain_group->alive && !chain_group->retired) {
                block->topology_generation++;
                block->reusable_count = 0;
                break;
            }
            dead_chain_walk = chain_group->parent_slot;
        }
    }
    return YETTY_OK(yetty_ycore_uint32, slot);
}

bool yetty_yvterm_rich_block_group_dead(const struct yetty_yvterm_rich_block *block,
                                        uint32_t group_slot)
{
    uint32_t guard = 0;
    while (group_slot != 0) {
        if (group_slot > block->group_count || guard++ > block->group_count) {
            return true; /* out of range / cyclic corruption — treat as dead */
        }
        const struct yetty_yvterm_rich_group *group = &block->groups[group_slot - 1u];
        if (!group->alive) {
            return true;
        }
        if (group->retired) {
            /* Retirement BARRIER: a retired subtree is frozen history — a
             * later ancestor delete/replace must not sweep it, so ancestor
             * death is invisible from inside it. (Whole-block invalidation
             * marks every group dead, which the check above catches first.) */
            return false;
        }
        group_slot = group->parent_slot;
    }
    return false;
}

/* Whether the chain from group_slot to the root passes any retired group
 * (self included) — how binding resolution detects a node frozen into
 * history by per-node scroll retirement. */
bool yetty_yvterm_rich_block_group_chain_retired(const struct yetty_yvterm_rich_block *block,
                                                 uint32_t group_slot)
{
    uint32_t guard = 0;
    while (group_slot != 0 && group_slot <= block->group_count && guard++ <= block->group_count) {
        const struct yetty_yvterm_rich_group *group = &block->groups[group_slot - 1u];
        if (group->retired) {
            return true;
        }
        group_slot = group->parent_slot;
    }
    return false;
}

void yetty_yvterm_rich_store_block_group_kill(struct yetty_yvterm_rich_store *store,
                                              struct yetty_yvterm_rich_handle handle,
                                              uint32_t group_slot)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return;
    }
    if (group_slot == 0) {
        /* Root kill: everything in the block dies. */
        for (uint32_t index = 0; index < block->group_count; ++index) {
            if (block->groups[index].alive) {
                block->groups[index].alive = false;
                block->dead_group_count++;
            }
        }
    } else if (group_slot <= block->group_count) {
        if (block->groups[group_slot - 1u].alive) {
            block->groups[group_slot - 1u].alive = false;
            block->dead_group_count++;
        }
        /* Every DESCENDANT group dies with the subtree (the documented
         * kill contract, and what keeps recycling effective: an
         * alive-flagged husk under a dead ancestor would pin both slots
         * against reuse forever). Retired descendants — and everything
         * under them, via the chain barrier — are frozen history and
         * spared, mirroring the replace-open sweep. */
        for (uint32_t index = 0; index < block->group_count; ++index) {
            struct yetty_yvterm_rich_group *descendant = &block->groups[index];
            if (!descendant->alive || descendant->retired || index + 1u == group_slot) {
                continue;
            }
            uint32_t walk = descendant->parent_slot;
            uint32_t guard = 0;
            while (walk != 0 && walk <= block->group_count && guard++ <= block->group_count) {
                if (block->groups[walk - 1u].retired) {
                    break; /* frozen subtree — spare */
                }
                if (walk == group_slot) {
                    descendant->alive = false;
                    block->dead_group_count++;
                    break;
                }
                walk = block->groups[walk - 1u].parent_slot;
            }
        }
    } else {
        return;
    }
    block->topology_generation++; /* group deaths invalidate the reuse cache */
    /* Records whose scope chain is now dead lose their runtimes and turn
     * dead; their bytes stay until serialization compacts them. Retired
     * records are immutable history and survive a live ancestor's delete
     * (the chain barrier spares frozen SUBTREES; this spares frozen DIRECT
     * records) — except the whole-block root kill, which is the
     * block-granular invalidation and sweeps everything. */
    for (uint32_t index = 0; index < block->record_count; ++index) {
        struct yetty_yvterm_rich_record *record = &block->records[index];
        if (!record->alive) {
            continue;
        }
        if (group_slot != 0 && record->retired) {
            continue;
        }
        if (group_slot == 0 || yetty_yvterm_rich_block_group_dead(block, record->group_slot)) {
            if (record->complex_runtime) {
                yetty_ydraw_complex_destroy(record->complex_runtime);
                record->complex_runtime = NULL;
            }
            record_free_journal(store, record);
            record->alive = false;
            block->dead_record_count++;
        }
    }
    store_bump_paint_generation(store, block->screen);
}

struct yetty_ycore_void_result yetty_yvterm_rich_store_block_attach_runtime(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    struct yetty_ydraw_complex *complex_runtime)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return YETTY_ERR(yetty_ycore_void, "rich store: attach to stale block handle");
    }
    if (!complex_runtime) {
        return YETTY_ERR(yetty_ycore_void, "rich store: attach NULL runtime");
    }
    /* The ingest order is envelope-then-runtime: bind to the newest complex
     * record still waiting for its runtime. */
    for (uint32_t index = block->record_count; index > 0; --index) {
        struct yetty_yvterm_rich_record *record = &block->records[index - 1u];
        if (record->alive && record->kind == YETTY_YVTERM_RICH_RECORD_COMPLEX &&
            !record->complex_runtime) {
            record->complex_runtime = complex_runtime;
            return YETTY_OK_VOID();
        }
    }
    /* Attach without a retained envelope: legal (the figure just cannot be
     * rebuilt after eviction) — record it as a bytes-less complex record.
     * It still needs its own paint key: no wire z (plane 0), fresh
     * sequence under the root scope. */
    struct yetty_ycore_void_result record_res =
        block_ensure_records(block, block->record_count + 1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, record_res, "rich store: attach record slot");
    struct yetty_yvterm_rich_record *record = &block->records[block->record_count];
    memset(record, 0, sizeof(*record));
    record->kind = YETTY_YVTERM_RICH_RECORD_COMPLEX;
    record->alive = true;
    record->render_leaf = true;
    /* Bytes-less complex — no wire z; an open ambient scope still applies. */
    record->paint_z = store->ambient_paint_z_depth > 0
                          ? store->ambient_paint_z[store->ambient_paint_z_depth - 1u]
                          : 0;
    struct yetty_ycore_void_result key_res = record_mint_paint_key(store, block, 0u, record);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, key_res, "rich store: attach paint key");
    record->complex_runtime = complex_runtime;
    block->record_count++;
    store_bump_paint_generation(store, block->screen);
    return YETTY_OK_VOID();
}

void yetty_yvterm_rich_store_block_evict_runtimes(struct yetty_yvterm_rich_store *store,
                                                  struct yetty_yvterm_rich_handle handle)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return;
    }
    for (uint32_t index = 0; index < block->record_count; ++index) {
        if (block->records[index].complex_runtime) {
            yetty_ydraw_complex_destroy(block->records[index].complex_runtime);
            block->records[index].complex_runtime = NULL;
        }
    }
}

void yetty_yvterm_rich_store_block_destroy(struct yetty_yvterm_rich_store *store,
                                           struct yetty_yvterm_rich_handle handle)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return;
    }
    store_bump_paint_generation(store, block->screen);
    block_free_contents_store(store, block);
    block->in_use = 0;
    block->generation++; /* stale-ify every outstanding handle */
    if (store->free_count < store->free_capacity) {
        store->free_slots[store->free_count++] = handle.slot;
    } else {
        uint32_t new_capacity = store->free_capacity ? store->free_capacity * 2u : 16u;
        uint32_t *grown = realloc(store->free_slots, (size_t)new_capacity * sizeof(uint32_t));
        if (grown) {
            store->free_slots = grown;
            store->free_capacity = new_capacity;
            store->free_slots[store->free_count++] = handle.slot;
        }
        /* On grow failure the slot is simply never reused — safe leak of one
         * pool slot, no dangling state. */
    }
    store->live_count--;
}

uint32_t yetty_yvterm_rich_block_runtime_count(const struct yetty_yvterm_rich_block *block)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < block->record_count; ++index) {
        if (block->records[index].alive && block->records[index].complex_runtime) {
            count++;
        }
    }
    return count;
}

uint32_t yetty_yvterm_rich_block_complex_record_count(const struct yetty_yvterm_rich_block *block)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < block->record_count; ++index) {
        if (block->records[index].alive &&
            block->records[index].kind == YETTY_YVTERM_RICH_RECORD_COMPLEX) {
            count++;
        }
    }
    return count;
}

struct yetty_ycore_void_result yetty_yvterm_rich_store_record_journal_append(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t record_index, uint32_t target_field, const uint32_t *payload_words,
    uint32_t payload_word_count)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block || record_index >= block->record_count) {
        return YETTY_ERR(yetty_ycore_void, "rich store: journal append target gone");
    }
    struct yetty_yvterm_rich_record *record = &block->records[record_index];
    if (!record->alive) {
        return YETTY_ERR(yetty_ycore_void, "rich store: journal append to dead record");
    }
    if (record->journal_overflowed) {
        return YETTY_OK_VOID(); /* live-only from here on */
    }
    uint32_t entry_words = 2u + payload_word_count;
    uint32_t need = record->journal_count + entry_words;
    if (need > record->journal_capacity) {
        uint32_t new_capacity = record->journal_capacity ? record->journal_capacity * 2u : 32u;
        while (new_capacity < need) {
            new_capacity *= 2u;
        }
        size_t grow_bytes = (size_t)(new_capacity - record->journal_capacity) * sizeof(uint32_t);
        if (store->journal_bytes_budget &&
            store->journal_bytes_used + grow_bytes > store->journal_bytes_budget) {
            /* Budget exhausted: live-first degradation — the runtime already
             * applied the update; drop the journal and latch overflow so
             * re-materialization falls back to the creation envelope. */
            record_free_journal(store, record);
            record->journal_overflowed = true;
            return YETTY_OK_VOID();
        }
        uint32_t *grown = realloc(record->journal, (size_t)new_capacity * sizeof(uint32_t));
        if (!grown) {
            record_free_journal(store, record);
            record->journal_overflowed = true;
            return YETTY_OK_VOID();
        }
        record->journal = grown;
        record->journal_capacity = new_capacity;
        store->journal_bytes_used += grow_bytes;
    }
    record->journal[record->journal_count++] = target_field;
    record->journal[record->journal_count++] = payload_word_count;
    if (payload_word_count) {
        memcpy(record->journal + record->journal_count, payload_words,
               (size_t)payload_word_count * sizeof(uint32_t));
        record->journal_count += payload_word_count;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvterm_rich_store_block_group_replace_open(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block) {
        return YETTY_ERR(yetty_ycore_void, "rich store: replace_open on stale block");
    }
    if (group_slot == 0 || group_slot > block->group_count ||
        !block->groups[group_slot - 1u].alive) {
        return YETTY_ERR(yetty_ycore_void, "rich store: replace_open on dead group");
    }
    /* EXACT-SUBTREE replace of the LIVE remainder: the node at the path
     * becomes exactly the new content, except that RETIRED descendants are
     * immutable history — they (and everything under them, via the chain
     * barrier) are spared and stay rendered frozen. Kill every other
     * descendant group (the target itself stays alive — its slot, anchor
     * and binding survive the replacement)... */
    for (uint32_t index = 0; index < block->group_count; ++index) {
        struct yetty_yvterm_rich_group *descendant = &block->groups[index];
        if (!descendant->alive || descendant->retired || index + 1u == group_slot) {
            continue;
        }
        uint32_t walk = descendant->parent_slot;
        uint32_t guard = 0;
        int under_barrier = 0;
        while (walk != 0 && walk <= block->group_count && guard++ <= block->group_count) {
            if (block->groups[walk - 1u].retired) {
                under_barrier = 1; /* frozen subtree — spare */
                break;
            }
            if (walk == group_slot) {
                descendant->alive = false;
                block->dead_group_count++;
                break;
            }
            walk = block->groups[walk - 1u].parent_slot;
        }
        (void)under_barrier;
    }
    block->topology_generation++; /* group deaths invalidate the reuse cache */
    /* ...then kill every record of the LIVE subtree: the target's direct
     * non-retired records plus any record whose scope chain died with a
     * descendant group (the chain barrier keeps frozen records' chains
     * "not dead", so they are never swept here). */
    for (uint32_t index = 0; index < block->record_count; ++index) {
        struct yetty_yvterm_rich_record *record = &block->records[index];
        if (!record->alive || record->retired) {
            continue;
        }
        if (record->group_slot != group_slot &&
            !yetty_yvterm_rich_block_group_dead(block, record->group_slot)) {
            continue;
        }
        if (record->complex_runtime) {
            yetty_ydraw_complex_destroy(record->complex_runtime);
            record->complex_runtime = NULL;
        }
        record_free_journal(store, record);
        record->alive = false;
        block->dead_record_count++;
    }
    /* Replacement records now stack at the group's anchor sequence with
     * body-order ordinals; record indices never move DURING the window
     * (live compaction between batches may re-pack them — indices are
     * snapshots, see the record struct doc). The ordinal counter is NOT
     * reset: it is monotone for the anchor's lifetime, so keys stay
     * unique across every group sharing the anchor (a nested group
     * created inside an earlier replacement mints from the same
     * counter). */
    struct yetty_yvterm_rich_group *group = &block->groups[group_slot - 1u];
    group->replacing = true;
    store_bump_paint_generation(store, block->screen);
    return YETTY_OK_VOID();
}

void yetty_yvterm_rich_store_block_group_replace_close(struct yetty_yvterm_rich_store *store,
                                                       struct yetty_yvterm_rich_handle handle,
                                                       uint32_t group_slot)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block || group_slot == 0 || group_slot > block->group_count) {
        return;
    }
    struct yetty_yvterm_rich_group *group = &block->groups[group_slot - 1u];
    group->replacing = false;
}

void yetty_yvterm_rich_store_record_kill(struct yetty_yvterm_rich_store *store,
                                         struct yetty_yvterm_rich_handle handle,
                                         uint32_t record_index)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block || record_index >= block->record_count) {
        return;
    }
    struct yetty_yvterm_rich_record *record = &block->records[record_index];
    if (!record->alive) {
        return;
    }
    if (record->complex_runtime) {
        yetty_ydraw_complex_destroy(record->complex_runtime);
        record->complex_runtime = NULL;
    }
    record_free_journal(store, record);
    record->alive = false;
    block->dead_record_count++;
    store_bump_paint_generation(store, block->screen);
}

void yetty_yvterm_rich_store_record_journal_poison(struct yetty_yvterm_rich_store *store,
                                                   struct yetty_yvterm_rich_handle handle,
                                                   uint32_t record_index)
{
    struct yetty_yvterm_rich_block *block = yetty_yvterm_rich_store_resolve(store, handle);
    if (!block || record_index >= block->record_count) {
        return;
    }
    struct yetty_yvterm_rich_record *record = &block->records[record_index];
    record_free_journal(store, record);
    record->journal_overflowed = true;
}
