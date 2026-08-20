/* GENERATED — do not edit. */
/* Object API for regular class(es) `rich` (implementation module: ymux).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YMUX_RICH_H
#define YETTY_YCLASSGEN_API_YMUX_RICH_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_buffer;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_RICH_ANCHOR_KIND
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_RICH_ANCHOR_KIND
/* Tagged anchor kinds. */
enum yetty_ymux_rich_anchor_kind {
    YETTY_YMUX_RICH_ANCHOR_PRIMARY = 0,
    YETTY_YMUX_RICH_ANCHOR_ALT = 1,
};
#endif

/* The store — the yclass data block. */
struct yetty_yclass_ptr_result yetty_ymux_rich_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_rich;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_RICH_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_RICH_PTR_RESULT
struct yetty_ymux_rich_ptr_result {
    int ok;
    union {
        struct yetty_ymux_rich *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_rich_ptr_result yetty_ymux_rich_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_rich_to(struct yetty_ymux_rich *data);

struct yetty_yclass_object_ptr_result yetty_ymux_rich_create(struct yetty_yclass_ctx *ctx);

struct yetty_yclass_object_ptr_result yetty_ymux_rich_make(void);
struct yetty_ycore_void_result yetty_ymux_rich_dispose(struct yetty_yclass_object *obj);
/* Mint a stable rich id for one creating wire record (copied, store-owned)
 * anchored at the tagged coordinate. Returns the id (never 0, never
 * reused). */
struct yetty_ycore_uint64_result yetty_ymux_rich_mint(struct yetty_yclass_object *obj,
                                                      const uint32_t *creation_words,
                                                      uint32_t word_count, int anchor_kind,
                                                      uint64_t anchor_a, uint32_t anchor_b,
                                                      uint32_t span_rows);
struct yetty_ycore_void_result yetty_ymux_rich_anchor(struct yetty_yclass_object *obj,
                                                      uint64_t rich_id, int *out_kind,
                                                      uint64_t *out_anchor_a,
                                                      uint32_t *out_anchor_b,
                                                      uint32_t *out_span_rows);
struct yetty_ycore_void_result yetty_ymux_rich_journal_append(struct yetty_yclass_object *obj,
                                                              uint64_t rich_id,
                                                              const uint32_t *words,
                                                              uint32_t word_count);
struct yetty_ycore_uint32_result yetty_ymux_rich_journal_count(struct yetty_yclass_object *obj,
                                                               uint64_t rich_id);
/* Creation record / journal entry accessors (borrowed spans; index 0 with
 * NULL out_count semantics like the other bulk accessors). */
struct yetty_ycore_const_uint32_ptr_result yetty_ymux_rich_creation(struct yetty_yclass_object *obj,
                                                                    uint64_t rich_id,
                                                                    uint32_t *out_word_count);
/* The 64-bit content hash of an object's creation payload — the key the
 * content-addressed wire uses to reference a heavy payload the client already
 * has, instead of re-sending it. 0 if the id is unknown/compacted. */
struct yetty_ycore_uint64_result yetty_ymux_rich_creation_hash(struct yetty_yclass_object *obj,
                                                               uint64_t rich_id);
/* Distinct content-addressed creation payloads currently stored — equals the
 * number of live objects only when every payload is unique; identical blobs
 * collapse to one. Exposed for tests/introspection of the dedup. */
struct yetty_ycore_uint32_result yetty_ymux_rich_distinct_resource_count(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_uint32_ptr_result yetty_ymux_rich_journal_entry(
    struct yetty_yclass_object *obj, uint64_t rich_id, uint32_t entry_index,
    uint32_t *out_word_count);
struct yetty_ycore_void_result yetty_ymux_rich_tombstone(struct yetty_yclass_object *obj,
                                                         uint64_t rich_id);
struct yetty_ycore_int_result yetty_ymux_rich_is_tombstoned(struct yetty_yclass_object *obj,
                                                            uint64_t rich_id);
/* Enumeration for projection: total object count (tombstoned included —
 * callers filter) and the id at a dense index. */
struct yetty_ycore_uint32_result yetty_ymux_rich_count(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_ymux_rich_id_at(struct yetty_yclass_object *obj,
                                                       uint32_t index);
/* Monotonic store revision — moves on every state change. */
struct yetty_ycore_uint64_result yetty_ymux_rich_revision(struct yetty_yclass_object *obj);
/* Terminal CLEAR / RIS: tombstone every active object and close the stream
 * namespace — cleared figures never resurrect. */
struct yetty_ycore_void_result yetty_ymux_rich_clear_all(struct yetty_yclass_object *obj);
/* Phase 6 retention: free the creation + journal payloads of tombstoned objects.
 * A tombstoned id is never re-emitted or replayed (project_rich skips it), so its
 * payloads are dead weight — churned rich content would otherwise grow memory
 * unbounded. The slot + id + tombstone flag are KEPT so is_tombstoned stays
 * correct and the id is never reused. Returns the number of objects compacted;
 * does NOT bump the revision (no visible-state change). */
struct yetty_ycore_uint32_result yetty_ymux_rich_compact_tombstoned(
    struct yetty_yclass_object *obj);
/* Serialize the whole object set into `out` (caller owns/creates the buffer). */
struct yetty_ycore_void_result yetty_ymux_rich_snapshot(struct yetty_yclass_object *obj,
                                                        struct yetty_ycore_buffer *out);
/* Restore a store from a snapshot: pre-validate (atomic — a malformed snapshot
 * leaves the store UNCHANGED), then HARD-reset and rebuild every object
 * (creation + journal + anchor + tombstone) plus next_rich_id and revision.
 * After validation the only possible failure is OOM, which leaves the store
 * empty (fail closed); the running object_count/journal_count always covers
 * exactly the materialized entries so the cleanup free is exact. */
struct yetty_ycore_void_result yetty_ymux_rich_restore(struct yetty_yclass_object *obj,
                                                       const uint32_t *words, size_t word_count);
/* Bind a producer stream ordinal to a rich id (ADD re-binds an existing
 * ordinal to the fresh object). */
struct yetty_ycore_void_result yetty_ymux_rich_map_bind(struct yetty_yclass_object *obj,
                                                        uint32_t ordinal, uint64_t rich_id);
/* Resolve a producer ordinal to its bound rich id (0 = unbound). */
struct yetty_ycore_uint64_result yetty_ymux_rich_map_resolve(struct yetty_yclass_object *obj,
                                                             uint32_t ordinal);
/* DELETE: tombstone the bound object and unbind the ordinal. */
struct yetty_ycore_void_result yetty_ymux_rich_map_delete(struct yetty_yclass_object *obj,
                                                          uint32_t ordinal);
/* Producer exit: the namespace closes — later ordinals resolve to nothing;
 * objects stay (their content remains in history). */
struct yetty_ycore_void_result yetty_ymux_rich_map_close(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
