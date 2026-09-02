/*
 * rich-store.h — the rich-block store of the yvterm rolling rich content.
 *
 * One rich BLOCK is the unit of rolling placement: one insertion/reservation
 * event (one wire envelope) creates one block, anchored at its insertion
 * rolling row and owned by its bottom line. A block owns an ordered set of
 * RECORDS (verbatim wire word spans in the block's arena; a complex record
 * additionally owns its figure runtime). Line rings store generation-checked
 * block HANDLES; the store owns the blocks, so moving or reflowing a line
 * moves handles, never the externally indexed objects.
 *
 * The store is deliberately free of libvterm types and of any grid/GPU
 * dependency: the grid supplies placement (cursor rows, row movement,
 * dirtying) through its own code, and later hosts (figure-only consumers)
 * can reuse the same store without the terminal.
 */
#ifndef YETTY_YVTERM_RICH_STORE_H
#define YETTY_YVTERM_RICH_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_complex;

/* Generation-checked handle: external references must never be raw array
 * indexes or pointers. A stale handle fails generation validation instead of
 * resolving to recycled memory. slot == UINT32_MAX is the invalid handle. */
struct yetty_yvterm_rich_handle {
    uint32_t slot;
    uint32_t generation;
};

YETTY_YRESULT_DECLARE(yetty_yvterm_rich_handle, struct yetty_yvterm_rich_handle);

enum yetty_yvterm_rich_record_kind {
    YETTY_YVTERM_RICH_RECORD_PRIMITIVE = 0,
    YETTY_YVTERM_RICH_RECORD_COMPLEX = 1,
};

enum yetty_yvterm_rich_block_state {
    YETTY_YVTERM_RICH_BLOCK_LIVE = 0,
    /* Sealed = immutable history: addressability removed, placement kept.
     * Minted at the active/history boundary (phase 2+) and for every block
     * materialized from the archive. */
    YETTY_YVTERM_RICH_BLOCK_SEALED = 1,
};

/* Which surface a block belongs to. Cache-local blocks back archive
 * materialization lines and die with their cache entry. */
enum yetty_yvterm_rich_block_screen {
    YETTY_YVTERM_RICH_SCREEN_PRIMARY = 0,
    YETTY_YVTERM_RICH_SCREEN_ALTERNATE = 1,
    YETTY_YVTERM_RICH_SCREEN_CACHE = 2,
};

/* One retained wire record: a span of u32 words in the owning block's arena.
 * A complex record owns the retained creation envelope AND (while hot) the
 * figure runtime derived from it. A dead record (its group was deleted or
 * replaced) stays in place WITHIN the mutation batch that killed it, so
 * surviving indices/order are stable until the batch closes; renderers and
 * the serializer skip it. Between batches LIVE COMPACTION (block_compact)
 * may reclaim dead descriptors and re-pack survivors — record indices are
 * SNAPSHOT values that expire at the next rich mutation; long-lived
 * index-holders (the grid's complex bindings) are remapped by the
 * compaction caller, and archival compacts the rest away. */
struct yetty_yvterm_rich_record {
    uint32_t arena_offset;
    uint32_t word_count;
    /* 0 = the block's anonymous root group. */
    uint32_t group_slot;

    /* Paint key: the record's position in the one total paint order
     * (paint_z, paint_sequence, record_ordinal) — lower keys render first.
     * paint_z is the producer's explicit depth, extracted from the wire
     * words exactly once at append. paint_sequence comes from the store's
     * session-scoped monotone domain; array position is never paint order.
     * record_ordinal distinguishes records sharing a group's stable
     * replacement sequence (fresh-sequence records use ordinal zero). */
    int32_t paint_z;
    uint64_t paint_sequence;
    uint32_t record_ordinal;

    enum yetty_yvterm_rich_record_kind kind;
    bool alive;
    /* Whether the record paints (becomes a render-plan leaf): complexes
     * always; primitives unless they are resources (FONT) or empty.
     * Classified once at append, next to the paint-z extraction. */
    bool render_leaf;
    /* Content-space vertical extent (pre-offset pixels, from the record's
     * effective AABB at ingest). extent_known false = a producer path that
     * did not supply it — such records exempt their node from scroll
     * retirement (conservative: the node stays addressable). */
    float content_top_px;
    float content_bottom_px;
    bool extent_known;
    /* Per-node scroll retirement (UC-12): the record's own projected
     * footprint left the live surface at a terminal scroll. Permanent —
     * the content stays rendered as immutable history, the binding stops
     * resolving. */
    bool retired;
    struct yetty_ydraw_complex *complex_runtime;

    /* Accepted-update journal: entries {target_field u32, payload_words u32,
     * payload...} back to back, replayed after the creation envelope when the
     * figure re-materializes. Live-first budget policy: once the aggregate
     * journal budget is exhausted the record's journal is discarded and
     * journal_overflowed latches — later updates stay live-only and
     * re-materialization degrades to the creation envelope. */
    uint32_t *journal;
    uint32_t journal_count;
    uint32_t journal_capacity;
    bool journal_overflowed;
};

/* One named (or anonymous-root) group scope inside a block: addressability
 * and containment only — groups own no coordinates. Slot 0 is the implicit
 * anonymous root (never stored; group_slot 0 on records means "root").
 * external_key is the group's nested-id PATH folded to 64 bits
 * (yetty_yvterm_group_key_fold); has_external_id distinguishes a bound group
 * from a structural child scope. */
struct yetty_yvterm_rich_group {
    uint32_t parent_slot; /* 0 = child of the anonymous root */
    bool alive;
    bool has_external_id;
    uint64_t external_key;
    /* Slot-reuse guard: bumped when a dead, unreferenced slot is RECYCLED
     * for a fresh group (group_open reuses garbage left behind by
     * exact-subtree replacements instead of growing the table forever).
     * External references that index groups by slot (the grid's GROUP
     * bindings) stamp this and re-validate on lookup, so a stale path can
     * never resolve to the slot's new occupant. */
    uint32_t generation;
    /* Per-node scroll retirement (UC-12): the group's subtree footprint
     * left the live surface at a terminal scroll. Permanent, and a chain
     * BARRIER: everything under a retired group is frozen history — kept
     * rendered, excluded from ancestor replace/delete sweeps, and no
     * binding under it resolves. */
    bool retired;

    /* Mutable group state — the translation OFFSET (pixels, content space).
     * The ONE mover of the model: children's declared coordinates never
     * change; rendering projects them at local + accumulated ancestor
     * offsets, clipped to the insertion's row span. Set via
     * update(path, GROUP_FIELD_OFFSET, x, y); absolute values, default 0. */
    float offset_x;
    float offset_y;

    /* Mutable group state — the CLIP rectangle (local content space,
     * pre-offset). Non-layout PROJECTION state: rendering intersects the
     * subtree with it; span/placement never see it. clip_valid 0 = no clip.
     * Set via update(path, GROUP_FIELD_CLIP, x, y, w, h). */
    float clip_x;
    float clip_y;
    float clip_w;
    float clip_h;
    bool clip_valid;

    /* Replacement anchor: the paint sequence the group's subtree stacks at
     * across reopens. Fixed during the creation walk — the first direct
     * record's sequence, or the declaration sequence when the creation body
     * had no direct record. Reopens never move it. A group created INSIDE a
     * replacement body inherits the replacing ancestor's anchor (the whole
     * replaced subtree occupies the replaced emission slot — new nested
     * groups must not leapfrog content created after the original, e.g. an
     * overlay), so several groups can share one anchor sequence. */
    uint64_t anchor_sequence;
    bool anchor_from_record;
    /* Slot of the group whose next_replacement_ordinal mints ordinals for
     * this group's anchor space: self for an anchor-owning group, the
     * replacing root's owner for inherited anchors. One monotone counter per
     * anchor keeps (sequence, ordinal) keys unique across every group that
     * shares the anchor. */
    uint32_t anchor_owner_slot;

    /* Reopen in progress (replace_open .. replace_close): records appended
     * under the subtree now share anchor_sequence and mint record_ordinal
     * values in body emission order (from the anchor owner's monotone
     * counter) instead of fresh sequences. */
    bool replacing;
    uint32_t next_replacement_ordinal;
};

struct yetty_yvterm_rich_block {
    /* Slot-reuse guard: bumped on every create AND destroy, so any handle
     * minted for a previous occupant of this slot fails validation. */
    uint32_t generation;
    int in_use;

    enum yetty_yvterm_rich_block_screen screen;
    enum yetty_yvterm_rich_block_state state;

    /* Rolling placement. insertion_rolling_row is the timeline row the block
     * was emitted at; bottom_owner_row = insertion + span - 1 owns storage
     * and retention. span_rows 0 = single-row block not yet relocated. */
    uint64_t insertion_rolling_row;
    uint64_t bottom_owner_row;
    uint32_t span_rows;
    /* Resize staging: reflow computes the remapped span here while it is
     * only CONSTRUCTING the replacement ring; the resize commit consumes it
     * (span_rows = pending) and the abort path clears it — live metadata
     * never mutates before the commit point. 0 = nothing staged. */
    uint32_t pending_span_rows;

    struct yetty_yvterm_rich_record *records;
    uint32_t record_count;
    uint32_t record_capacity;
    /* Dead (killed, not yet reclaimed) records — incremental, maintained
     * at every record-death site and reset by block_compact, so the
     * per-close compaction gate never scans the table just to learn
     * nothing died. */
    uint32_t dead_record_count;

    /* Named/child group scopes. groups[N] is group slot N+1 (slot 0 is the
     * implicit anonymous root). */
    struct yetty_yvterm_rich_group *groups;
    uint32_t group_count;
    uint32_t group_capacity;
    /* Dead (killed, not yet recycled) group slots — the gate on the
     * reuse scan in group_open: pure construction (no deaths yet) must
     * pay O(1) per open, not a full table scan. Maintained at every
     * group-death site and decremented on recycle. */
    uint32_t dead_group_count;
    /* Scroll-retirement dedupe: the sweep id of the last
     * grid_retire_departed_nodes run over this block — one scroll batch
     * reaches the same block from every departing covered row, and the
     * scan is idempotent, so repeats are pure waste. Grid-owned value;
     * the store only provides the slot. */
    uint64_t retire_sweep_stamp;

    /* Reusable-group-slot cache: the reclaimable set (dead, unretired,
     * unreachable from any live anchor) computed by ONE full scan, then
     * consumed per group-open with only a per-pop protect-chain check.
     * Valid while `reusable_generation` matches `topology_generation`
     * (bumped by every group-death event, by compaction, and by an
     * append that anchors a chain with dead links). Bounds the
     * exact-subtree replacement path: recreating N nodes costs one scan
     * per replacement window, not one scan per node. `reusable_scans`
     * counts rebuilds — the complexity pin the churn tests assert on. */
    uint32_t *reusable_slots;
    uint32_t reusable_count;
    uint32_t reusable_capacity;
    uint64_t reusable_generation;
    uint64_t topology_generation;
    uint64_t reusable_scans;

    uint32_t *arena;
    uint32_t arena_count;
    uint32_t arena_capacity;
};

struct yetty_yvterm_rich_store {
    struct yetty_yvterm_rich_block *blocks;
    uint32_t block_capacity;
    uint32_t *free_slots;
    uint32_t free_count;
    uint32_t free_capacity;
    uint64_t next_paint_sequence;
    /* Count of in-use blocks — teardown sanity + tests. */
    uint32_t live_count;

    /* Ambient paint-z scope stack (CMD_PAINT_Z / CMD_PAINT_Z_END). While
     * non-empty, every appended render-leaf record takes the innermost
     * ambient z as its paint_z, overriding the z decoded from its wire
     * words — the mechanism that lets a complex (which carries no wire z)
     * be stacked at an arbitrary depth. Reset at the envelope boundary so
     * an unbalanced scope cannot leak into later output. */
    int32_t ambient_paint_z[8];
    uint32_t ambient_paint_z_depth;
    /* Pushes that overflowed the fixed stack. A push past the cap is ignored
     * (the innermost accepted z keeps applying), but its matching pop must
     * cancel the ignored push FIRST — otherwise it would wrongly pop a real
     * accepted entry and evaluate later content under the wrong scope. */
    uint32_t ambient_paint_z_overflow;

    /* Per-screen paint generations, indexed by rich_block_screen. Bumped
     * only by MEMBERSHIP/key changes (block create/destroy, record append,
     * subtree kill, replace-open) — never by runtime eviction, journal
     * traffic, sealing or anchor movement. Monotone, so a render plan
     * covering several screens can validate against a sum of them. */
    uint64_t paint_generation[3];

    /* Aggregate update-journal accounting. budget 0 = unlimited. */
    size_t journal_bytes_used;
    size_t journal_bytes_budget;
};

void yetty_yvterm_rich_store_init(struct yetty_yvterm_rich_store *store);

/* Destroys every remaining block (runtimes included) and frees the pools. */
void yetty_yvterm_rich_store_destroy(struct yetty_yvterm_rich_store *store);

struct yetty_yvterm_rich_handle_result yetty_yvterm_rich_store_block_create(
    struct yetty_yvterm_rich_store *store, enum yetty_yvterm_rich_block_screen screen,
    uint64_t insertion_rolling_row);

/* The live block behind `handle`, or NULL when the handle is stale/invalid.
 * A stale handle is a normal outcome (the block was destroyed), not an
 * error. */
struct yetty_yvterm_rich_block *yetty_yvterm_rich_store_resolve(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle);

/* Append one verbatim wire record (the whole record incl. its type word)
 * under group scope `group_slot` (0 = the block's anonymous root). kind is
 * derived from the leading word; the runtime (if any) attaches later. The
 * paint key mints here: paint_z decodes from the wire words, and the
 * sequence/ordinal follow the owning group's state (fresh sequence +
 * ordinal 0 normally; the group's anchor sequence + body-order ordinal
 * while the group is replace-open). */
struct yetty_ycore_void_result yetty_yvterm_rich_store_block_append_record(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot, const uint32_t *words, uint32_t word_count);

/* Push / pop the ambient paint-z scope. While the stack is non-empty every
 * appended render-leaf record paints at the innermost z instead of the z
 * decoded from its wire words. push past the fixed depth is IGNORED and
 * returns false (its matching pop is still absorbed — the stack stays
 * balanced); pop is a no-op on an empty stack; reset drops the whole
 * stack (envelope boundary). */
bool yetty_yvterm_rich_store_push_paint_z(struct yetty_yvterm_rich_store *store, int32_t z);
void yetty_yvterm_rich_store_pop_paint_z(struct yetty_yvterm_rich_store *store);
void yetty_yvterm_rich_store_reset_paint_z(struct yetty_yvterm_rich_store *store);

/* Append with an EXPLICIT paint key — archive materialization only. The
 * serialized key reproduces exactly; nothing is re-extracted or re-minted
 * and the session sequence allocator does not advance. */
struct yetty_ycore_void_result yetty_yvterm_rich_store_block_append_record_exact(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot, const uint32_t *words, uint32_t word_count, int32_t paint_z,
    uint64_t paint_sequence, uint32_t record_ordinal);

/* Pre-grow the block's record table and word arena so the next
 * `extra_records` appends totalling `extra_words` words cannot fail on
 * allocation — the staging half of an atomic in-place replacement. The
 * caller validates its replacement content first, reserves, and only
 * THEN kills the old subtree and appends; a reservation failure leaves
 * the block (and the old content) untouched. */
struct yetty_ycore_void_result yetty_yvterm_rich_store_block_reserve(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t extra_records, uint32_t extra_words);

/* block_reserve PLUS the paint-key preflight for a replacement into
 * `group_slot`: the replacement-anchor owner must have ordinal headroom
 * for `extra_records` more appends. With this reserved, a subsequent
 * replace-open + `extra_records` appends cannot fail at all — the whole
 * commit half of an atomic replacement is infallible. */
struct yetty_ycore_void_result yetty_yvterm_rich_store_block_group_reserve(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot, uint32_t extra_records, uint32_t extra_words);

/* Dead (killed, not yet reclaimed) records currently retained in the
 * block — the live-compaction trigger metric. */
uint32_t yetty_yvterm_rich_store_block_dead_records(struct yetty_yvterm_rich_store *store,
                                                    struct yetty_yvterm_rich_handle handle);

/* Reclaim the block's dead records IN PLACE: live records keep their
 * relative order (and their paint keys), their word payloads pack
 * densely, and dead descriptors/bytes drop out — the LIVE counterpart of
 * the archival compaction, so a long-lived insertion's repeated
 * replacements cannot grow the block without bound. Record POSITIONS
 * change: `remap` (>= record_count entries) receives old→new indices,
 * UINT32_MAX for dropped entries, and the caller must remap anything
 * that addresses records by index (the grid's complex bindings). Returns
 * the number of records dropped (0 = nothing dead, remap untouched). */
uint32_t yetty_yvterm_rich_store_block_compact(struct yetty_yvterm_rich_store *store,
                                               struct yetty_yvterm_rich_handle handle,
                                               uint32_t *remap, uint32_t remap_capacity);

/* Open a group scope in the block under `parent_slot`. `external_key` is the
 * group's nested-id path folded to 64 bits (yetty_yvterm_group_key_fold); pass
 * has_external_id = false for a structural (unnamed nested) scope. Returns the
 * new group slot (>= 1). */
struct yetty_ycore_uint32_result yetty_yvterm_rich_store_block_group_open(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t parent_slot, bool has_external_id, uint64_t external_key);

/* Kill the group subtree rooted at `group_slot`: the group, every descendant
 * group, and every record they own turn dead (runtimes destroyed, bytes kept
 * in place until serialization compacts them). Kills the whole block content
 * when group_slot is 0 (the anonymous root). */
void yetty_yvterm_rich_store_block_group_kill(struct yetty_yvterm_rich_store *store,
                                              struct yetty_yvterm_rich_handle handle,
                                              uint32_t group_slot);

/* Whether `group_slot` (or, transitively, one of its ancestors) is dead or
 * out of range. Slot 0 (the root) is always alive on a live block. A
 * RETIRED group on the chain stops the walk with "not dead" — frozen
 * history is immune to ancestor death (the chain barrier). */
bool yetty_yvterm_rich_block_group_dead(const struct yetty_yvterm_rich_block *block,
                                        uint32_t group_slot);

/* Whether the chain from `group_slot` to the root passes any RETIRED group
 * (self included) — binding resolution's frozen-history check. */
bool yetty_yvterm_rich_block_group_chain_retired(const struct yetty_yvterm_rich_block *block,
                                                 uint32_t group_slot);

/* REOPEN a live group for EXACT-SUBTREE replacement: every LIVE record and
 * every live descendant group of the subtree dies (runtimes destroyed,
 * journals freed, descendant slots freed for reuse) — the reopened body IS
 * the subtree's new full content. RETIRED descendants are the frozen
 * scrollback history and survive untouched, and the group's own slot stays
 * bound. The group enters replacing mode — records appended until the
 * matching replace_close share the group's replacement-anchor sequence
 * with body-order ordinals, taking the replaced run's paint position
 * without moving any published record index. */
struct yetty_ycore_void_result yetty_yvterm_rich_store_block_group_replace_open(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t group_slot);

/* Leave replacing mode (pairs with replace_open; harmless on a group that
 * never entered it, and on a stale handle). */
void yetty_yvterm_rich_store_block_group_replace_close(struct yetty_yvterm_rich_store *store,
                                                       struct yetty_yvterm_rich_handle handle,
                                                       uint32_t group_slot);

/* Kill ONE record: destroy its runtime, drop its journal, mark it dead. The
 * bytes stay in the arena until serialization compacts them. Used to delete /
 * replace an addressable complex NODE without touching its siblings. */
void yetty_yvterm_rich_store_record_kill(struct yetty_yvterm_rich_store *store,
                                         struct yetty_yvterm_rich_handle handle,
                                         uint32_t record_index);

/* Poison one record's journal: drop it and latch journal_overflowed, so a
 * partial journal can never replay as if it were the complete state (the
 * ragged-update path). */
void yetty_yvterm_rich_store_record_journal_poison(struct yetty_yvterm_rich_store *store,
                                                   struct yetty_yvterm_rich_handle handle,
                                                   uint32_t record_index);

/* Attach a figure runtime to the block's last complex record that has none
 * yet; a block with no such record gains a bytes-less complex record (the
 * legacy attach-without-envelope path). */
struct yetty_ycore_void_result yetty_yvterm_rich_store_block_attach_runtime(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    struct yetty_ydraw_complex *complex_runtime);

/* Destroy the block's figure runtimes but keep every retained record byte —
 * the hot-tier age-out. */
void yetty_yvterm_rich_store_block_evict_runtimes(struct yetty_yvterm_rich_store *store,
                                                  struct yetty_yvterm_rich_handle handle);

/* Destroy the block completely: runtimes, records, arena, slot. Safe on a
 * stale/invalid handle (no-op). */
void yetty_yvterm_rich_store_block_destroy(struct yetty_yvterm_rich_store *store,
                                           struct yetty_yvterm_rich_handle handle);

/* Number of complex records currently carrying a live runtime. */
uint32_t yetty_yvterm_rich_block_runtime_count(const struct yetty_yvterm_rich_block *block);

/* Number of complex records (with or without a live runtime) — the
 * materialization test ("does this block need its figures rebuilt"). */
uint32_t yetty_yvterm_rich_block_complex_record_count(const struct yetty_yvterm_rich_block *block);

/* Append one accepted update to a record's journal. Live-first: on budget
 * exhaustion the record's journal is discarded, journal_overflowed latches,
 * and the call still reports OK (the live runtime already applied the
 * update; only re-materialization fidelity degrades). */
struct yetty_ycore_void_result yetty_yvterm_rich_store_record_journal_append(
    struct yetty_yvterm_rich_store *store, struct yetty_yvterm_rich_handle handle,
    uint32_t record_index, uint32_t target_field, const uint32_t *payload_words,
    uint32_t payload_word_count);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_RICH_STORE_H */
