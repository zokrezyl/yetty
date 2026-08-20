/*
 * rich.c — the ymux durable rich-object store: class@ymux:rich (#695
 * phase 2).
 *
 * The store-global `rich_id -> object state` index of the plan: every rich
 * entity gets a stable 64-bit id (minted here, never reused) with a
 * STORE-OWNED verbatim copy of its creating wire record, a tagged anchor
 * (PRIMARY = stable logical-line id + cell offset; ALT = alt-screen epoch +
 * row/col), a lifecycle flag, and a LOSSLESS ordered update journal.
 * Updates land here regardless of where the anchor's row currently lives
 * (hot/warm/cold) — sealed history is never rewritten. Rematerialization =
 * creation record + journal replay; a tombstoned id never rematerializes.
 *
 * The producer stream mapping (ordinal -> rich_id) lives here too: ADD
 * re-binds an ordinal to a fresh id, DELETE tombstones and unbinds, a
 * clear tombstones the whole namespace, producer exit closes it. Current
 * yetty ingestion has no DELETE routing — this is new model behavior.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include "resource.h"

/* Tagged anchor kinds. */
enum YETTY_ANNOTATE("expose") yetty_ymux_rich_anchor_kind {
    YETTY_YMUX_RICH_ANCHOR_PRIMARY = 0, /* (logical_line_id, cell_offset) */
    YETTY_YMUX_RICH_ANCHOR_ALT = 1,     /* (alt_epoch, row) */
};

struct rich_journal_entry {
    uint32_t *words;
    uint32_t word_count;
};

struct rich_object {
    uint64_t rich_id;
    /* The creation payload is content-addressed: its bytes live once in the
     * store's resource store (deduped by hash across figures); this is a
     * BORROWED read-only view of that shared payload, valid while the object
     * holds its reference. creation_hash identifies the shared payload for
     * release. NULL after the object is compacted. */
    const uint32_t *creation_words;
    uint64_t creation_hash;
    uint32_t creation_word_count;
    int anchor_kind;
    uint64_t anchor_a; /* PRIMARY: logical_line_id; ALT: epoch */
    uint32_t anchor_b; /* PRIMARY: cell offset;    ALT: row */
    uint32_t span_rows;
    int tombstoned;
    struct rich_journal_entry *journal;
    uint32_t journal_count;
    uint32_t journal_capacity;
};

struct rich_stream_binding {
    uint32_t ordinal;
    uint64_t rich_id;
};

/* The store — the yclass data block. */
struct YETTY_ANNOTATE("class@ymux:rich") yetty_ymux_rich {
    struct rich_object *objects;
    uint32_t object_count;
    uint32_t object_capacity;
    uint64_t next_rich_id; /* starts at 1; 0 = invalid */

    struct rich_stream_binding *bindings;
    uint32_t binding_count;
    uint32_t binding_capacity;

    /* Monotonic store revision: bumps on every state change (mint,
     * journal append, tombstone, clear) so projectors can cheaply detect
     * rich-only updates. */
    uint64_t revision;

    /* Content-addressed store for creation payloads: identical heavy blobs
     * (an image/PDF/SVG ycat'd twice) are kept once and shared by hash. */
    struct yetty_ymux_resource_store resources;
};

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_rich_class_get(void);
struct yetty_ymux_rich_ptr_result yetty_ymux_rich_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_rich_ptr, struct yetty_ymux_rich *);

static struct rich_object *rich_find(struct yetty_ymux_rich *rich, uint64_t rich_id)
{
    for (uint32_t index = 0; index < rich->object_count; ++index) {
        if (rich->objects[index].rich_id == rich_id) {
            return &rich->objects[index];
        }
    }
    return NULL;
}

/*===========================================================================
 * Lifecycle.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_rich_make(void)
{
    struct yetty_yclass_ptr_result class_res = yetty_ymux_rich_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux rich_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux rich_make: alloc");
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(object_res.value);
    if (YETTY_IS_ERR(rich_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux rich_make: from_obj", rich_res);
    }
    rich_res.value->next_rich_id = 1;
    yetty_ymux_resource_store_init(&rich_res.value->resources);
    return YETTY_OK(yetty_yclass_object_ptr, object_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_dispose: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    for (uint32_t index = 0; index < rich->object_count; ++index) {
        struct rich_object *object = &rich->objects[index];
        if (object->creation_words) {
            yetty_ymux_resource_release(&rich->resources, object->creation_hash);
        }
        for (uint32_t entry = 0; entry < object->journal_count; ++entry) {
            free(object->journal[entry].words);
        }
        free(object->journal);
    }
    free(rich->objects);
    free(rich->bindings);
    yetty_ymux_resource_store_free(&rich->resources);
    return yetty_yclass_object_free(obj);
}

/*===========================================================================
 * Minting + anchors.
 *=========================================================================*/

/* Mint a stable rich id for one creating wire record (copied, store-owned)
 * anchored at the tagged coordinate. Returns the id (never 0, never
 * reused). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_rich_mint(struct yetty_yclass_object *obj,
                                                      const uint32_t *creation_words,
                                                      uint32_t word_count, int anchor_kind,
                                                      uint64_t anchor_a, uint32_t anchor_b,
                                                      uint32_t span_rows)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rich_res, "ymux rich_mint: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    if (!creation_words || word_count == 0) {
        return YETTY_ERR(yetty_ycore_uint64, "ymux rich_mint: empty creation record");
    }
    if (anchor_kind != YETTY_YMUX_RICH_ANCHOR_PRIMARY &&
        anchor_kind != YETTY_YMUX_RICH_ANCHOR_ALT) {
        return YETTY_ERR(yetty_ycore_uint64, "ymux rich_mint: bad anchor kind");
    }
    /* Content-address the creation payload: identical blobs share one copy. */
    uint64_t creation_hash = 0;
    struct yetty_ycore_void_result add_res =
        yetty_ymux_resource_add(&rich->resources, (const uint8_t *)creation_words,
                                (size_t)word_count * sizeof(uint32_t), &creation_hash);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, add_res, "ymux rich_mint: resource add");
    size_t stored_len = 0;
    const uint32_t *stored =
        (const uint32_t *)yetty_ymux_resource_get(&rich->resources, creation_hash, &stored_len);
    if (rich->object_count == rich->object_capacity) {
        uint32_t new_capacity = rich->object_capacity ? rich->object_capacity * 2 : 16;
        struct rich_object *grown =
            realloc(rich->objects, (size_t)new_capacity * sizeof(struct rich_object));
        if (!grown) {
            yetty_ymux_resource_release(&rich->resources, creation_hash);
            return YETTY_ERR(yetty_ycore_uint64, "ymux rich_mint: index grow");
        }
        rich->objects = grown;
        rich->object_capacity = new_capacity;
    }
    struct rich_object *object = &rich->objects[rich->object_count++];
    memset(object, 0, sizeof(*object));
    object->rich_id = rich->next_rich_id++;
    object->creation_words = stored;
    object->creation_hash = creation_hash;
    object->creation_word_count = word_count;
    object->anchor_kind = anchor_kind;
    object->anchor_a = anchor_a;
    object->anchor_b = anchor_b;
    object->span_rows = span_rows;
    ++rich->revision;
    return YETTY_OK(yetty_ycore_uint64, object->rich_id);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_anchor(struct yetty_yclass_object *obj,
                                                      uint64_t rich_id, int *out_kind,
                                                      uint64_t *out_anchor_a,
                                                      uint32_t *out_anchor_b,
                                                      uint32_t *out_span_rows)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_anchor: from_obj");
    struct rich_object *object = rich_find(rich_res.value, rich_id);
    if (!object) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_anchor: unknown id");
    }
    if (out_kind) {
        *out_kind = object->anchor_kind;
    }
    if (out_anchor_a) {
        *out_anchor_a = object->anchor_a;
    }
    if (out_anchor_b) {
        *out_anchor_b = object->anchor_b;
    }
    if (out_span_rows) {
        *out_span_rows = object->span_rows;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Journal — lossless, ordered; replay = creation + entries in order.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_journal_append(struct yetty_yclass_object *obj,
                                                              uint64_t rich_id,
                                                              const uint32_t *words,
                                                              uint32_t word_count)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_journal_append: from_obj");
    struct rich_object *object = rich_find(rich_res.value, rich_id);
    if (!object) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_journal_append: unknown id");
    }
    if (object->tombstoned) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_journal_append: tombstoned");
    }
    if (!words || word_count == 0) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_journal_append: empty record");
    }
    if (object->journal_count == object->journal_capacity) {
        uint32_t new_capacity = object->journal_capacity ? object->journal_capacity * 2 : 8;
        struct rich_journal_entry *grown =
            realloc(object->journal, (size_t)new_capacity * sizeof(struct rich_journal_entry));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ymux rich_journal_append: journal grow");
        }
        object->journal = grown;
        object->journal_capacity = new_capacity;
    }
    uint32_t *copy = malloc((size_t)word_count * sizeof(uint32_t));
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_journal_append: entry alloc");
    }
    memcpy(copy, words, (size_t)word_count * sizeof(uint32_t));
    object->journal[object->journal_count].words = copy;
    object->journal[object->journal_count].word_count = word_count;
    ++object->journal_count;
    ++rich_res.value->revision;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_rich_journal_count(struct yetty_yclass_object *obj,
                                                               uint64_t rich_id)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, rich_res, "ymux rich_journal_count: from_obj");
    struct rich_object *object = rich_find(rich_res.value, rich_id);
    if (!object) {
        return YETTY_ERR(yetty_ycore_uint32, "ymux rich_journal_count: unknown id");
    }
    return YETTY_OK(yetty_ycore_uint32, object->journal_count);
}

/* Creation record / journal entry accessors (borrowed spans; index 0 with
 * NULL out_count semantics like the other bulk accessors). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_const_uint32_ptr_result yetty_ymux_rich_creation(struct yetty_yclass_object *obj,
                                                                    uint64_t rich_id,
                                                                    uint32_t *out_word_count)
{
    if (out_word_count) {
        *out_word_count = 0;
    }
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_uint32_ptr, rich_res, "ymux rich_creation: from_obj");
    struct rich_object *object = rich_find(rich_res.value, rich_id);
    if (!object) {
        return YETTY_ERR(yetty_ycore_const_uint32_ptr, "ymux rich_creation: unknown id");
    }
    if (out_word_count) {
        *out_word_count = object->creation_word_count;
    }
    return YETTY_OK(yetty_ycore_const_uint32_ptr, object->creation_words);
}

/* The 64-bit content hash of an object's creation payload — the key the
 * content-addressed wire uses to reference a heavy payload the client already
 * has, instead of re-sending it. 0 if the id is unknown/compacted. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_rich_creation_hash(struct yetty_yclass_object *obj,
                                                               uint64_t rich_id)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rich_res, "ymux rich_creation_hash: from_obj");
    struct rich_object *object = rich_find(rich_res.value, rich_id);
    if (!object || !object->creation_words) {
        return YETTY_OK(yetty_ycore_uint64, 0);
    }
    return YETTY_OK(yetty_ycore_uint64, object->creation_hash);
}

/* Distinct content-addressed creation payloads currently stored — equals the
 * number of live objects only when every payload is unique; identical blobs
 * collapse to one. Exposed for tests/introspection of the dedup. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_rich_distinct_resource_count(
    struct yetty_yclass_object *obj)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, rich_res,
                        "ymux rich_distinct_resource_count: from_obj");
    return YETTY_OK(yetty_ycore_uint32, yetty_ymux_resource_count(&rich_res.value->resources));
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_uint32_ptr_result yetty_ymux_rich_journal_entry(
    struct yetty_yclass_object *obj, uint64_t rich_id, uint32_t entry_index,
    uint32_t *out_word_count)
{
    if (out_word_count) {
        *out_word_count = 0;
    }
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_uint32_ptr, rich_res,
                        "ymux rich_journal_entry: from_obj");
    struct rich_object *object = rich_find(rich_res.value, rich_id);
    if (!object || entry_index >= object->journal_count) {
        return YETTY_ERR(yetty_ycore_const_uint32_ptr, "ymux rich_journal_entry: out of range");
    }
    if (out_word_count) {
        *out_word_count = object->journal[entry_index].word_count;
    }
    return YETTY_OK(yetty_ycore_const_uint32_ptr, object->journal[entry_index].words);
}

/*===========================================================================
 * Lifecycle flags + stream mapping.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_tombstone(struct yetty_yclass_object *obj,
                                                         uint64_t rich_id)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_tombstone: from_obj");
    struct rich_object *object = rich_find(rich_res.value, rich_id);
    if (!object) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_tombstone: unknown id");
    }
    object->tombstoned = 1;
    ++rich_res.value->revision;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_rich_is_tombstoned(struct yetty_yclass_object *obj,
                                                            uint64_t rich_id)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rich_res, "ymux rich_is_tombstoned: from_obj");
    struct rich_object *object = rich_find(rich_res.value, rich_id);
    if (!object) {
        return YETTY_ERR(yetty_ycore_int, "ymux rich_is_tombstoned: unknown id");
    }
    return YETTY_OK(yetty_ycore_int, object->tombstoned);
}

/* Enumeration for projection: total object count (tombstoned included —
 * callers filter) and the id at a dense index. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_rich_count(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, rich_res, "ymux rich_count: from_obj");
    return YETTY_OK(yetty_ycore_uint32, rich_res.value->object_count);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_rich_id_at(struct yetty_yclass_object *obj,
                                                       uint32_t index)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rich_res, "ymux rich_id_at: from_obj");
    if (index >= rich_res.value->object_count) {
        return YETTY_ERR(yetty_ycore_uint64, "ymux rich_id_at: index out of range");
    }
    return YETTY_OK(yetty_ycore_uint64, rich_res.value->objects[index].rich_id);
}

/* Monotonic store revision — moves on every state change. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_rich_revision(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rich_res, "ymux rich_revision: from_obj");
    return YETTY_OK(yetty_ycore_uint64, rich_res.value->revision);
}

/* Terminal CLEAR / RIS: tombstone every active object and close the stream
 * namespace — cleared figures never resurrect. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_clear_all(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_clear_all: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    for (uint32_t index = 0; index < rich->object_count; ++index) {
        rich->objects[index].tombstoned = 1;
    }
    rich->binding_count = 0;
    ++rich->revision;
    return YETTY_OK_VOID();
}

/* Phase 6 retention: free the creation + journal payloads of tombstoned objects.
 * A tombstoned id is never re-emitted or replayed (project_rich skips it), so its
 * payloads are dead weight — churned rich content would otherwise grow memory
 * unbounded. The slot + id + tombstone flag are KEPT so is_tombstoned stays
 * correct and the id is never reused. Returns the number of objects compacted;
 * does NOT bump the revision (no visible-state change). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_rich_compact_tombstoned(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, rich_res, "ymux rich_compact_tombstoned: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    uint32_t compacted = 0;
    for (uint32_t index = 0; index < rich->object_count; ++index) {
        struct rich_object *object = &rich->objects[index];
        if (!object->tombstoned) {
            continue;
        }
        if (object->creation_words == NULL && object->journal == NULL) {
            continue; /* already compacted */
        }
        if (object->creation_words) {
            yetty_ymux_resource_release(&rich->resources, object->creation_hash);
        }
        object->creation_words = NULL;
        object->creation_hash = 0;
        object->creation_word_count = 0;
        for (uint32_t entry = 0; entry < object->journal_count; ++entry) {
            free(object->journal[entry].words);
        }
        free(object->journal);
        object->journal = NULL;
        object->journal_count = 0;
        object->journal_capacity = 0;
        ++compacted;
    }
    return YETTY_OK(yetty_ycore_uint32, compacted);
}

/*===========================================================================
 * Durable snapshot (Gate 6) — serialize the object set so a session can
 * persist its rich figures and restore them (e.g. on reattach). The producer
 * stream bindings are deliberately NOT snapshotted: they are live-producer
 * state a reattaching producer re-binds. Everything little-endian u32 words;
 * u64 fields split lo/hi.
 *
 *   MAGIC, VERSION, next_rich_id lo/hi, revision lo/hi, object_count,
 *   object_count x:
 *     rich_id lo/hi, anchor_kind, anchor_a lo/hi, anchor_b, span_rows,
 *     tombstoned, creation_word_count, creation_words[...],
 *     journal_count, journal_count x: (entry_word_count, entry_words[...])
 *=========================================================================*/

enum {
    YMUX_RICH_SNAPSHOT_MAGIC = 0x4E535259u, /* "YRSN" */
    YMUX_RICH_SNAPSHOT_VERSION = 1u,
    YMUX_RICH_SNAPSHOT_HEADER_WORDS = 7u,        /* magic..object_count */
    YMUX_RICH_SNAPSHOT_OBJECT_HEADER_WORDS = 9u, /* rich_id..creation_word_count */
};

static struct yetty_ycore_void_result rich_snapshot_put(struct yetty_ycore_buffer *out,
                                                        uint32_t word)
{
    return yetty_ycore_buffer_write(out, &word, sizeof(word));
}

/* Serialize the whole object set into `out` (caller owns/creates the buffer). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_snapshot(struct yetty_yclass_object *obj,
                                                        struct yetty_ycore_buffer *out)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_snapshot: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_snapshot: NULL out");
    }
    /* Durable state = LIVE objects. A tombstoned object is deleted; a restoring
     * client would never show it, so it is not persisted. next_rich_id in the
     * header still preserves id continuity so a dead id is never reused. */
    uint32_t live_count = 0;
    for (uint32_t index = 0; index < rich->object_count; ++index) {
        if (!rich->objects[index].tombstoned) {
            ++live_count;
        }
    }
    const uint32_t header[YMUX_RICH_SNAPSHOT_HEADER_WORDS] = {
        YMUX_RICH_SNAPSHOT_MAGIC,
        YMUX_RICH_SNAPSHOT_VERSION,
        (uint32_t)rich->next_rich_id,
        (uint32_t)(rich->next_rich_id >> 32),
        (uint32_t)rich->revision,
        (uint32_t)(rich->revision >> 32),
        live_count,
    };
    for (uint32_t index = 0; index < YMUX_RICH_SNAPSHOT_HEADER_WORDS; ++index) {
        struct yetty_ycore_void_result write_res = rich_snapshot_put(out, header[index]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "ymux rich_snapshot: header");
    }
    for (uint32_t index = 0; index < rich->object_count; ++index) {
        const struct rich_object *object = &rich->objects[index];
        if (object->tombstoned) {
            continue; /* not persisted */
        }
        const uint32_t object_header[YMUX_RICH_SNAPSHOT_OBJECT_HEADER_WORDS] = {
            (uint32_t)object->rich_id,
            (uint32_t)(object->rich_id >> 32),
            (uint32_t)object->anchor_kind,
            (uint32_t)object->anchor_a,
            (uint32_t)(object->anchor_a >> 32),
            object->anchor_b,
            object->span_rows,
            (uint32_t)(object->tombstoned ? 1 : 0),
            object->creation_word_count,
        };
        for (uint32_t field = 0; field < YMUX_RICH_SNAPSHOT_OBJECT_HEADER_WORDS; ++field) {
            struct yetty_ycore_void_result write_res = rich_snapshot_put(out, object_header[field]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "ymux rich_snapshot: object header");
        }
        struct yetty_ycore_void_result creation_res = yetty_ycore_buffer_write(
            out, object->creation_words, (size_t)object->creation_word_count * sizeof(uint32_t));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, creation_res, "ymux rich_snapshot: creation");
        struct yetty_ycore_void_result journal_count_res =
            rich_snapshot_put(out, object->journal_count);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, journal_count_res, "ymux rich_snapshot: journal");
        for (uint32_t entry = 0; entry < object->journal_count; ++entry) {
            struct yetty_ycore_void_result entry_count_res =
                rich_snapshot_put(out, object->journal[entry].word_count);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, entry_count_res, "ymux rich_snapshot: entry len");
            struct yetty_ycore_void_result entry_words_res = yetty_ycore_buffer_write(
                out, object->journal[entry].words,
                (size_t)object->journal[entry].word_count * sizeof(uint32_t));
            YETTY_RETURN_IF_ERR(yetty_ycore_void, entry_words_res, "ymux rich_snapshot: entry");
        }
    }
    return YETTY_OK_VOID();
}

/* Free every object's owned memory and empty the object set (a HARD reset,
 * unlike clear_all which only tombstones for lossless replay). */
static void rich_free_objects(struct yetty_ymux_rich *rich)
{
    for (uint32_t index = 0; index < rich->object_count; ++index) {
        struct rich_object *object = &rich->objects[index];
        if (object->creation_words) {
            yetty_ymux_resource_release(&rich->resources, object->creation_hash);
        }
        for (uint32_t entry = 0; entry < object->journal_count; ++entry) {
            free(object->journal[entry].words);
        }
        free(object->journal);
    }
    rich->object_count = 0;
}

/* Bounds/structure pre-scan: verify the whole snapshot fits WITHOUT allocating
 * or mutating, so restore is atomic — a malformed snapshot is rejected before
 * the live store is touched (bad input never corrupts good state). */
static struct yetty_ycore_void_result rich_snapshot_validate(const uint32_t *words,
                                                             size_t word_count)
{
    if (!words || word_count < YMUX_RICH_SNAPSHOT_HEADER_WORDS) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: short snapshot");
    }
    if (words[0] != YMUX_RICH_SNAPSHOT_MAGIC || words[1] != YMUX_RICH_SNAPSHOT_VERSION) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: bad magic/version");
    }
    uint32_t object_count = words[6];
    size_t offset = YMUX_RICH_SNAPSHOT_HEADER_WORDS;
    for (uint32_t index = 0; index < object_count; ++index) {
        if (offset + YMUX_RICH_SNAPSHOT_OBJECT_HEADER_WORDS > word_count) {
            return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: truncated object header");
        }
        uint32_t creation_word_count = words[offset + 8];
        offset += YMUX_RICH_SNAPSHOT_OBJECT_HEADER_WORDS;
        /* creation words + the trailing journal_count word must be present. */
        if (creation_word_count == 0 || offset + (size_t)creation_word_count + 1 > word_count) {
            return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: truncated creation");
        }
        offset += creation_word_count;
        uint32_t journal_count = words[offset];
        ++offset;
        for (uint32_t entry = 0; entry < journal_count; ++entry) {
            if (offset + 1 > word_count) {
                return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: truncated entry len");
            }
            uint32_t entry_word_count = words[offset];
            ++offset;
            if (entry_word_count == 0 || offset + (size_t)entry_word_count > word_count) {
                return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: truncated entry");
            }
            offset += entry_word_count;
        }
    }
    return YETTY_OK_VOID();
}

/* Restore a store from a snapshot: pre-validate (atomic — a malformed snapshot
 * leaves the store UNCHANGED), then HARD-reset and rebuild every object
 * (creation + journal + anchor + tombstone) plus next_rich_id and revision.
 * After validation the only possible failure is OOM, which leaves the store
 * empty (fail closed); the running object_count/journal_count always covers
 * exactly the materialized entries so the cleanup free is exact. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_restore(struct yetty_yclass_object *obj,
                                                       const uint32_t *words, size_t word_count)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_restore: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    struct yetty_ycore_void_result validate_res = rich_snapshot_validate(words, word_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, validate_res, "ymux rich_restore: validate");

    rich_free_objects(rich);
    rich->binding_count = 0;

    size_t offset = YMUX_RICH_SNAPSHOT_HEADER_WORDS;
    uint32_t object_count = words[6];
    for (uint32_t index = 0; index < object_count; ++index) {
        uint64_t rich_id = (uint64_t)words[offset] | ((uint64_t)words[offset + 1] << 32);
        int anchor_kind = (int)words[offset + 2];
        uint64_t anchor_a = (uint64_t)words[offset + 3] | ((uint64_t)words[offset + 4] << 32);
        uint32_t anchor_b = words[offset + 5];
        uint32_t span_rows = words[offset + 6];
        int tombstoned = words[offset + 7] ? 1 : 0;
        uint32_t creation_word_count = words[offset + 8];
        offset += YMUX_RICH_SNAPSHOT_OBJECT_HEADER_WORDS;
        if (rich->object_count == rich->object_capacity) {
            uint32_t new_capacity = rich->object_capacity ? rich->object_capacity * 2 : 16;
            struct rich_object *grown =
                realloc(rich->objects, (size_t)new_capacity * sizeof(struct rich_object));
            if (!grown) {
                rich_free_objects(rich);
                return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: object grow");
            }
            rich->objects = grown;
            rich->object_capacity = new_capacity;
        }
        struct rich_object *object = &rich->objects[rich->object_count];
        memset(object, 0, sizeof(*object));
        object->rich_id = rich_id;
        object->anchor_kind = anchor_kind;
        object->anchor_a = anchor_a;
        object->anchor_b = anchor_b;
        object->span_rows = span_rows;
        object->tombstoned = tombstoned;
        object->creation_word_count = creation_word_count;
        uint64_t creation_hash = 0;
        struct yetty_ycore_void_result add_res =
            yetty_ymux_resource_add(&rich->resources, (const uint8_t *)&words[offset],
                                    (size_t)creation_word_count * sizeof(uint32_t), &creation_hash);
        if (YETTY_IS_ERR(add_res)) {
            yetty_ycore_error_destroy(add_res.error);
            rich_free_objects(rich);
            return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: creation add");
        }
        object->creation_hash = creation_hash;
        object->creation_words =
            (const uint32_t *)yetty_ymux_resource_get(&rich->resources, creation_hash, NULL);
        offset += creation_word_count;
        /* Materialized enough that rich_free_objects frees it correctly
         * (journal NULL/0), so count it before touching the journal. */
        ++rich->object_count;

        uint32_t journal_count = words[offset];
        ++offset;
        if (journal_count > 0) {
            object->journal = malloc((size_t)journal_count * sizeof(struct rich_journal_entry));
            if (!object->journal) {
                rich_free_objects(rich);
                return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: journal alloc");
            }
            object->journal_capacity = journal_count;
            for (uint32_t entry = 0; entry < journal_count; ++entry) {
                uint32_t entry_word_count = words[offset];
                ++offset;
                object->journal[entry].words = malloc((size_t)entry_word_count * sizeof(uint32_t));
                if (!object->journal[entry].words) {
                    rich_free_objects(rich);
                    return YETTY_ERR(yetty_ycore_void, "ymux rich_restore: entry alloc");
                }
                memcpy(object->journal[entry].words, &words[offset],
                       (size_t)entry_word_count * sizeof(uint32_t));
                object->journal[entry].word_count = entry_word_count;
                /* Count each entry as it lands so an OOM later frees exactly the
                 * entries already copied. */
                object->journal_count = entry + 1;
                offset += entry_word_count;
            }
        }
    }
    rich->next_rich_id = (uint64_t)words[2] | ((uint64_t)words[3] << 32);
    rich->revision = (uint64_t)words[4] | ((uint64_t)words[5] << 32);
    return YETTY_OK_VOID();
}

/* Bind a producer stream ordinal to a rich id (ADD re-binds an existing
 * ordinal to the fresh object). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_map_bind(struct yetty_yclass_object *obj,
                                                        uint32_t ordinal, uint64_t rich_id)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_map_bind: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    if (!rich_find(rich, rich_id)) {
        return YETTY_ERR(yetty_ycore_void, "ymux rich_map_bind: unknown id");
    }
    for (uint32_t index = 0; index < rich->binding_count; ++index) {
        if (rich->bindings[index].ordinal == ordinal) {
            rich->bindings[index].rich_id = rich_id;
            return YETTY_OK_VOID();
        }
    }
    if (rich->binding_count == rich->binding_capacity) {
        uint32_t new_capacity = rich->binding_capacity ? rich->binding_capacity * 2 : 8;
        struct rich_stream_binding *grown =
            realloc(rich->bindings, (size_t)new_capacity * sizeof(struct rich_stream_binding));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ymux rich_map_bind: bindings grow");
        }
        rich->bindings = grown;
        rich->binding_capacity = new_capacity;
    }
    rich->bindings[rich->binding_count].ordinal = ordinal;
    rich->bindings[rich->binding_count].rich_id = rich_id;
    ++rich->binding_count;
    return YETTY_OK_VOID();
}

/* Resolve a producer ordinal to its bound rich id (0 = unbound). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_rich_map_resolve(struct yetty_yclass_object *obj,
                                                             uint32_t ordinal)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, rich_res, "ymux rich_map_resolve: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    for (uint32_t index = 0; index < rich->binding_count; ++index) {
        if (rich->bindings[index].ordinal == ordinal) {
            return YETTY_OK(yetty_ycore_uint64, rich->bindings[index].rich_id);
        }
    }
    return YETTY_OK(yetty_ycore_uint64, 0);
}

/* DELETE: tombstone the bound object and unbind the ordinal. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_map_delete(struct yetty_yclass_object *obj,
                                                          uint32_t ordinal)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_map_delete: from_obj");
    struct yetty_ymux_rich *rich = rich_res.value;
    for (uint32_t index = 0; index < rich->binding_count; ++index) {
        if (rich->bindings[index].ordinal != ordinal) {
            continue;
        }
        struct rich_object *object = rich_find(rich, rich->bindings[index].rich_id);
        if (object) {
            object->tombstoned = 1;
        }
        rich->bindings[index] = rich->bindings[--rich->binding_count];
        ++rich->revision;
        return YETTY_OK_VOID();
    }
    return YETTY_ERR(yetty_ycore_void, "ymux rich_map_delete: unbound ordinal");
}

/* Producer exit: the namespace closes — later ordinals resolve to nothing;
 * objects stay (their content remains in history). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_rich_map_close(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_rich_ptr_result rich_res = yetty_ymux_rich_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rich_res, "ymux rich_map_close: from_obj");
    rich_res.value->binding_count = 0;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ymux/rich.c"
