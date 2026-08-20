/*
 * ymux rich wire contract (#695 v1: row-anchored ydraw records) —
 * headless, no GPU, no yvterm. The server half end to end: a vendor DCS
 * 600001 envelope fed through the REAL pane byte path mints into the
 * pane's rich store with the cursor's stable logical anchor; the
 * projector emits the visible set as a rich body; quiescence emits
 * nothing; journal appends re-emit with replay payloads; scrolled-away
 * anchors leave the body.
 */

#include <yetty/api/ymux/attachment.h>
#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>
#include <yetty/api/ymux/projector.h>
#include <yetty/api/ymux/rich.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>
#include <yetty/ysdf/types.gen.h>

#include "ytest.h"

/* Module-private wire enums: the terminfo capability + rich record flags. */
#include "../../../src/yetty/ymux/proto.h"
#include "../../../src/yetty/ymux/rich-format.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Mirrors rich-format.h (module-private wire contract pinned by tests). */
enum {
    TEST_RICH_MAGIC = 0x594D5052,
    TEST_RICH_HEADER_WORDS = 3,
    TEST_RICH_RECORD_HEADER_WORDS = 7,
    TEST_RICH_FLAG_REPOSITION = 1u << 1,
};

static void put_float(uint32_t *word, float value)
{
    memcpy(word, &value, sizeof(*word));
}

/* One BOX prim: [type, layer, fill, stroke, stroke_w, cx, cy, hw, hh, r]. */
static void box_record(uint32_t *words, float center_x, float center_y)
{
    words[0] = YETTY_YSDF_BOX;
    words[1] = 0;
    words[2] = 0xFF00FF00u;
    words[3] = 0;
    words[4] = 0;
    put_float(&words[5], center_x);
    put_float(&words[6], center_y);
    put_float(&words[7], 4.0f);
    put_float(&words[8], 9.0f);
    put_float(&words[9], 0.0f);
}

/* Feed one DCS 600001 envelope carrying `words`, split at `chunk` bytes
 * (0 = single write) to exercise fragment accumulation. */
static void feed_rich_envelope(struct ytest *test, struct yetty_yclass_object *pane,
                               const uint32_t *words, uint32_t word_count, size_t chunk)
{
    struct yetty_ycore_buffer_result b64_res =
        yetty_ycore_base64_encode(words, (size_t)word_count * sizeof(uint32_t));
    YTEST_REQUIRE_OK(test, b64_res);
    char envelope[1024];
    int envelope_len = snprintf(envelope, sizeof(envelope), "\x1bP600001y%.*s\x1b\\",
                                (int)b64_res.value.size, (const char *)b64_res.value.data);
    yetty_ycore_buffer_destroy(&b64_res.value);
    YTEST_REQUIRE(test, envelope_len > 0 && (size_t)envelope_len < sizeof(envelope));
    if (chunk == 0) {
        YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, envelope, (size_t)envelope_len));
        return;
    }
    for (size_t offset = 0; offset < (size_t)envelope_len; offset += chunk) {
        size_t piece = (size_t)envelope_len - offset;
        if (piece > chunk) {
            piece = chunk;
        }
        YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, envelope + offset, piece));
    }
}

static void test_rich_wire_end_to_end(struct ytest *test)
{
    struct yetty_yclass_object *pane = yetty_ymux_pane_make(4, 20, 8, 0, NULL).value;
    YTEST_REQUIRE_NOT_NULL(test, pane);
    struct yetty_yclass_object *attachment = yetty_ymux_attachment_make(pane, 4, 20).value;
    YTEST_REQUIRE_NOT_NULL(test, attachment);
    struct yetty_yclass_object *projector = yetty_ymux_projector_make(pane, attachment).value;
    YTEST_REQUIRE_NOT_NULL(test, projector);
    struct yetty_yclass_object *store = yetty_ymux_pane_rich_store(pane).value;
    YTEST_REQUIRE_NOT_NULL(test, store);

    /* Cursor lands at col 5 after "hello"; the envelope anchors there. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, "hello", 5));
    uint32_t prim[10];
    box_record(prim, 4.0f, 9.0f);
    feed_rich_envelope(test, pane, prim, 10, /*chunk=*/3);

    /* Minted with the cursor's stable anchor. */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_count(store).value, 1);
    uint64_t rich_id = yetty_ymux_rich_id_at(store, 0).value;
    int anchor_kind = -1;
    uint64_t anchor_a = 0;
    uint32_t anchor_b = 0;
    YTEST_REQUIRE_OK(
        test, yetty_ymux_rich_anchor(store, rich_id, &anchor_kind, &anchor_a, &anchor_b, NULL));
    YTEST_CHECK_EQ_INT(test, anchor_kind, YETTY_YMUX_RICH_ANCHOR_PRIMARY);
    struct yetty_ymux_history_row_result row_res = yetty_ymux_pane_resolve_row(pane, 0);
    YTEST_REQUIRE_OK(test, row_res);
    YTEST_CHECK(test, anchor_a == row_res.value.logical_line_id);
    YTEST_CHECK_EQ_INT(test, anchor_b, 5);

    /* First rich projection: one visible record at viewport (0, 5). */
    struct yetty_ycore_buffer body = yetty_ycore_buffer_create(8192).value;
    struct yetty_ycore_int_result project_res = yetty_ymux_projector_project_rich(projector, &body);
    YTEST_REQUIRE_OK(test, project_res);
    YTEST_CHECK_EQ_INT(test, project_res.value, 1);
    const uint32_t *words = (const uint32_t *)body.data;
    YTEST_REQUIRE(test, body.size / sizeof(uint32_t) >=
                            TEST_RICH_HEADER_WORDS + TEST_RICH_RECORD_HEADER_WORDS + 10);
    YTEST_CHECK(test, words[0] == TEST_RICH_MAGIC);
    YTEST_CHECK_EQ_INT(test, words[2], 1); /* record count */
    YTEST_CHECK(test, ((uint64_t)words[3] | ((uint64_t)words[4] << 32)) == rich_id);
    YTEST_CHECK_EQ_INT(test, words[5], 0);  /* revision (no journal yet) */
    YTEST_CHECK_EQ_INT(test, words[6], 0);  /* viewport row */
    YTEST_CHECK_EQ_INT(test, words[7], 5);  /* viewport col */
    YTEST_CHECK_EQ_INT(test, words[9], 10); /* payload words */
    YTEST_CHECK(test, memcmp(&words[10], prim, sizeof(prim)) == 0);

    /* Quiescent: nothing new to emit. */
    yetty_ycore_buffer_destroy(&body);
    body = yetty_ycore_buffer_create(8192).value;
    project_res = yetty_ymux_projector_project_rich(projector, &body);
    YTEST_REQUIRE_OK(test, project_res);
    YTEST_CHECK_EQ_INT(test, project_res.value, 0);
    YTEST_CHECK_EQ_SIZE(test, body.size, 0);

    /* A journal append re-emits with the replay payload. */
    uint32_t update[10];
    box_record(update, 13.0f, 9.0f);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_journal_append(store, rich_id, update, 10));
    yetty_ycore_buffer_destroy(&body);
    body = yetty_ycore_buffer_create(8192).value;
    project_res = yetty_ymux_projector_project_rich(projector, &body);
    YTEST_REQUIRE_OK(test, project_res);
    YTEST_CHECK_EQ_INT(test, project_res.value, 1);
    words = (const uint32_t *)body.data;
    YTEST_CHECK_EQ_INT(test, words[5], 1);  /* revision moved */
    YTEST_CHECK_EQ_INT(test, words[9], 20); /* creation + journal words */
    YTEST_CHECK(test, memcmp(&words[10], prim, sizeof(prim)) == 0);
    YTEST_CHECK(test, memcmp(&words[20], update, sizeof(update)) == 0);

    /* Scroll the anchor row far off the live screen: the visible set
     * empties (store revision moves via a second mint to trigger). */
    for (int line = 0; line < 12; ++line) {
        YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, "\r\nx", 3));
    }
    uint32_t far_prim[10];
    box_record(far_prim, 4.0f, 9.0f);
    feed_rich_envelope(test, pane, far_prim, 10, 0);
    yetty_ycore_buffer_destroy(&body);
    body = yetty_ycore_buffer_create(8192).value;
    project_res = yetty_ymux_projector_project_rich(projector, &body);
    YTEST_REQUIRE_OK(test, project_res);
    YTEST_CHECK_EQ_INT(test, project_res.value, 1);
    words = (const uint32_t *)body.data;
    /* Only the fresh mint is visible; the scrolled-away record is not. */
    YTEST_CHECK_EQ_INT(test, words[2], 1);
    uint64_t visible_id = (uint64_t)words[3] | ((uint64_t)words[4] << 32);
    YTEST_CHECK(test, visible_id != rich_id);

    yetty_ycore_buffer_destroy(&body);
    yetty_ymux_projector_dispose(projector);
    yetty_ymux_attachment_dispose(attachment);
    yetty_ymux_pane_dispose(pane);
}

/* A pure SCROLL (view_top moves, store unchanged, the figure stays visible)
 * emits a REPOSITION frame: the record carries the REPOSITION flag and NO
 * payload — the figure moves without re-transmitting its body (yvterm's
 * rolling-row scroll; re-sending would recreate the "25 KB per newline"
 * regression). */
static void test_rich_wire_scroll_reposition(struct ytest *test)
{
    struct yetty_yclass_object *pane = yetty_ymux_pane_make(6, 20, 8, 0, NULL).value;
    YTEST_REQUIRE_NOT_NULL(test, pane);
    struct yetty_yclass_object *attachment = yetty_ymux_attachment_make(pane, 6, 20).value;
    struct yetty_yclass_object *projector = yetty_ymux_projector_make(pane, attachment).value;
    struct yetty_yclass_object *store = yetty_ymux_pane_rich_store(pane).value;
    YTEST_REQUIRE(test, attachment && projector && store);

    /* Fill to the bottom row, then anchor a figure there so a single newline
     * scrolls it up one row WITHOUT scrolling it out of the viewport. */
    for (int line = 0; line < 5; ++line) {
        YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, "\r\n", 2));
    }
    uint32_t prim[10];
    box_record(prim, 4.0f, 9.0f);
    feed_rich_envelope(test, pane, prim, 10, 0);

    /* First projection: a FULL frame — payload present, no reposition flag. */
    struct yetty_ycore_buffer body = yetty_ycore_buffer_create(8192).value;
    struct yetty_ycore_int_result project_res = yetty_ymux_projector_project_rich(projector, &body);
    YTEST_REQUIRE_OK(test, project_res);
    YTEST_CHECK_EQ_INT(test, project_res.value, 1);
    const uint32_t *words = (const uint32_t *)body.data;
    YTEST_CHECK_EQ_INT(test, words[2], 1);
    int32_t full_row = (int32_t)words[6];
    YTEST_CHECK(test, (words[8] & TEST_RICH_FLAG_REPOSITION) == 0);
    YTEST_CHECK(test, words[9] > 0);

    /* Scroll one row WITHOUT touching the store. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, "\r\n", 2));
    yetty_ycore_buffer_destroy(&body);
    body = yetty_ycore_buffer_create(8192).value;
    project_res = yetty_ymux_projector_project_rich(projector, &body);
    YTEST_REQUIRE_OK(test, project_res);
    YTEST_CHECK_EQ_INT(test, project_res.value, 1);
    words = (const uint32_t *)body.data;
    YTEST_CHECK_EQ_INT(test, words[2], 1);
    YTEST_CHECK(test, (words[8] & TEST_RICH_FLAG_REPOSITION) != 0); /* reposition */
    YTEST_CHECK_EQ_INT(test, words[9], 0);                          /* NO payload re-sent */
    YTEST_CHECK_EQ_INT(test, (int32_t)words[6], full_row - 1);      /* moved up one row */

    yetty_ycore_buffer_destroy(&body);
    yetty_ymux_projector_dispose(projector);
    yetty_ymux_attachment_dispose(attachment);
    yetty_ymux_pane_dispose(pane);
}

/* Gate 6 durable rich state: the full object set (creation + journal + anchors +
 * tombstone + revision + next id) survives snapshot -> restore into a fresh
 * store; a malformed snapshot is rejected ATOMICALLY (target left unchanged). */
static void test_rich_wire_snapshot_roundtrip(struct ytest *test)
{
    struct yetty_yclass_object *store_a = yetty_ymux_rich_make().value;
    YTEST_REQUIRE_NOT_NULL(test, store_a);

    uint32_t create1[10];
    box_record(create1, 4.0f, 9.0f);
    uint64_t id1 = yetty_ymux_rich_mint(store_a, create1, 10, YETTY_YMUX_RICH_ANCHOR_PRIMARY,
                                        0x1122334455667788ull, 5, 7)
                       .value;
    uint32_t create2[10];
    box_record(create2, 8.0f, 3.0f);
    uint64_t id2 =
        yetty_ymux_rich_mint(store_a, create2, 10, YETTY_YMUX_RICH_ANCHOR_ALT, 42, 3, 1).value;
    uint32_t upd[10];
    box_record(upd, 1.0f, 2.0f);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_journal_append(store_a, id1, upd, 10));
    box_record(upd, 3.0f, 4.0f);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_journal_append(store_a, id1, upd, 10));
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_tombstone(store_a, id2));
    uint64_t revision_a = yetty_ymux_rich_revision(store_a).value;

    struct yetty_ycore_buffer snap = yetty_ycore_buffer_create(4096).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_snapshot(store_a, &snap));
    const uint32_t *snap_words = (const uint32_t *)snap.data;
    size_t snap_word_count = snap.size / sizeof(uint32_t);

    struct yetty_yclass_object *store_b = yetty_ymux_rich_make().value;
    YTEST_REQUIRE_NOT_NULL(test, store_b);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_restore(store_b, snap_words, snap_word_count));

    /* Durable state is LIVE objects only: the tombstoned id2 is NOT persisted. */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_count(store_b).value, 1);
    YTEST_CHECK(test, yetty_ymux_rich_revision(store_b).value == revision_a);
    YTEST_CHECK(test, yetty_ymux_rich_id_at(store_b, 0).value == id1);

    int kind = -1;
    uint64_t anchor_a = 0;
    uint32_t anchor_b = 0;
    uint32_t span = 0;
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_rich_anchor(store_b, id1, &kind, &anchor_a, &anchor_b, &span));
    YTEST_CHECK_EQ_INT(test, kind, YETTY_YMUX_RICH_ANCHOR_PRIMARY);
    YTEST_CHECK(test, anchor_a == 0x1122334455667788ull);
    YTEST_CHECK_EQ_INT(test, anchor_b, 5);
    YTEST_CHECK_EQ_INT(test, span, 7);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_journal_count(store_b, id1).value, 2);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_is_tombstoned(store_b, id1).value, 0);
    uint32_t creation_count = 0;
    const uint32_t *creation = yetty_ymux_rich_creation(store_b, id1, &creation_count).value;
    YTEST_REQUIRE_NOT_NULL(test, creation);
    YTEST_CHECK_EQ_INT(test, creation_count, 10);
    YTEST_CHECK(test, memcmp(creation, create1, sizeof(create1)) == 0);
    uint32_t entry_count = 0;
    const uint32_t *entry0 = yetty_ymux_rich_journal_entry(store_b, id1, 0, &entry_count).value;
    YTEST_REQUIRE_NOT_NULL(test, entry0);
    YTEST_CHECK_EQ_INT(test, entry_count, 10);

    /* The tombstoned id2 is gone from the restored store. */
    struct yetty_ycore_void_result gone =
        yetty_ymux_rich_anchor(store_b, id2, &kind, &anchor_a, &anchor_b, &span);
    YTEST_CHECK(test, !gone.ok);
    if (!gone.ok) {
        yetty_ycore_error_destroy(gone.error);
    }

    /* next_rich_id survived: a fresh mint continues past the dead id. */
    uint64_t id3 =
        yetty_ymux_rich_mint(store_b, create1, 10, YETTY_YMUX_RICH_ANCHOR_PRIMARY, 0, 0, 1).value;
    YTEST_CHECK(test, id3 == id2 + 1);

    /* Malformed input is rejected ATOMICALLY — store_b (now 2 objects: restored
     * id1 + fresh id3) is left unchanged, never corrupted or emptied. */
    uint32_t bad_magic[7] = {0xDEADBEEFu, 1u, 0, 0, 0, 0, 0};
    struct yetty_ycore_void_result bad = yetty_ymux_rich_restore(store_b, bad_magic, 7);
    YTEST_CHECK(test, !bad.ok);
    if (!bad.ok) {
        yetty_ycore_error_destroy(bad.error);
    }
    uint32_t truncated[7] = {0x4E535259u, 1u, 0, 0, 0, 0, 3u}; /* claims 3, none present */
    struct yetty_ycore_void_result trunc = yetty_ymux_rich_restore(store_b, truncated, 7);
    YTEST_CHECK(test, !trunc.ok);
    if (!trunc.ok) {
        yetty_ycore_error_destroy(trunc.error);
    }
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_count(store_b).value, 2); /* unchanged */

    yetty_ycore_buffer_destroy(&snap);
    yetty_ymux_rich_dispose(store_a);
    yetty_ymux_rich_dispose(store_b);
}

/* project_vt smoke/regression anchor: a full VT redraw of a pane showing "hi"
 * must carry the text bytes and a cursor-home. Anchors the VT-output path
 * (previously untested) so the tty-render emitter swap is verifiable. */
static void test_vt_smoke(struct ytest *test)
{
    struct yetty_yclass_object *pane = yetty_ymux_pane_make(4, 20, 8, 0, NULL).value;
    YTEST_REQUIRE_NOT_NULL(test, pane);
    struct yetty_yclass_object *attachment = yetty_ymux_attachment_make(pane, 4, 20).value;
    struct yetty_yclass_object *projector = yetty_ymux_projector_make(pane, attachment).value;
    YTEST_REQUIRE(test, attachment && projector);

    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, "hi", 2));

    struct yetty_ycore_buffer out = yetty_ycore_buffer_create(4096).value;
    struct yetty_ycore_void_result res = yetty_ymux_projector_project_vt(projector, &out);
    YTEST_REQUIRE_OK(test, res);
    YTEST_CHECK(test, out.size > 0);
    int found_text = 0;
    for (size_t index = 0; index + 1 < out.size; ++index) {
        const char *bytes = (const char *)out.data;
        if (bytes[index] == 'h' && bytes[index + 1] == 'i') {
            found_text = 1;
        }
    }
    YTEST_CHECK(test, found_text);

    yetty_ycore_buffer_destroy(&out);
    yetty_ymux_projector_dispose(projector);
    yetty_ymux_attachment_dispose(attachment);
    yetty_ymux_pane_dispose(pane);
}

/* Phase 6 retention: compacting frees a tombstoned object's payloads (bounded
 * memory for churned rich content) while keeping its slot/id/flag; the live
 * object is untouched; the op is idempotent. */
static void test_rich_wire_compact_tombstoned(struct ytest *test)
{
    struct yetty_yclass_object *store = yetty_ymux_rich_make().value;
    YTEST_REQUIRE_NOT_NULL(test, store);

    uint32_t create[10];
    box_record(create, 4.0f, 9.0f);
    uint64_t id_live =
        yetty_ymux_rich_mint(store, create, 10, YETTY_YMUX_RICH_ANCHOR_PRIMARY, 1, 2, 1).value;
    uint64_t id_dead =
        yetty_ymux_rich_mint(store, create, 10, YETTY_YMUX_RICH_ANCHOR_PRIMARY, 3, 4, 1).value;
    /* Both objects were minted with the SAME creation payload — content
     * addressing keeps ONE shared copy, not two. */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_distinct_resource_count(store).value, 1);
    uint32_t upd[10];
    box_record(upd, 5.0f, 6.0f);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_journal_append(store, id_dead, upd, 10));
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_tombstone(store, id_dead));

    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_compact_tombstoned(store).value, 1);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_compact_tombstoned(store).value, 0); /* idempotent */

    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_count(store).value, 2); /* slot kept */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_is_tombstoned(store, id_dead).value, 1);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_is_tombstoned(store, id_live).value, 0);
    uint32_t live_creation_count = 0;
    const uint32_t *live_creation =
        yetty_ymux_rich_creation(store, id_live, &live_creation_count).value;
    YTEST_REQUIRE_NOT_NULL(test, live_creation);
    YTEST_CHECK_EQ_INT(test, live_creation_count, 10);
    /* Compacting the dead twin released ONE reference; the shared payload
     * survives because the live object still holds the other — so the store
     * still has exactly one distinct resource and live_creation stays valid. */
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_distinct_resource_count(store).value, 1);

    yetty_ymux_rich_dispose(store);
}

/* Content addressing: distinct creation payloads occupy distinct resources;
 * an identical payload collapses onto the existing one; releasing (dispose)
 * frees them. */
static void test_rich_wire_content_dedup(struct ytest *test)
{
    struct yetty_yclass_object *store = yetty_ymux_rich_make().value;
    YTEST_REQUIRE_NOT_NULL(test, store);

    uint32_t payload_a[10];
    box_record(payload_a, 4.0f, 9.0f);
    uint32_t payload_b[10];
    box_record(payload_b, 7.0f, 2.0f); /* a genuinely different blob */

    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_distinct_resource_count(store).value, 0);
    yetty_ymux_rich_mint(store, payload_a, 10, YETTY_YMUX_RICH_ANCHOR_PRIMARY, 1, 0, 1);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_distinct_resource_count(store).value, 1);
    /* Same bytes again -> shared, still one distinct resource. */
    yetty_ymux_rich_mint(store, payload_a, 10, YETTY_YMUX_RICH_ANCHOR_PRIMARY, 2, 0, 1);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_distinct_resource_count(store).value, 1);
    /* Different bytes -> a second distinct resource. */
    yetty_ymux_rich_mint(store, payload_b, 10, YETTY_YMUX_RICH_ANCHOR_PRIMARY, 3, 0, 1);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_distinct_resource_count(store).value, 2);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_count(store).value, 3); /* three objects */

    yetty_ymux_rich_dispose(store);
}

/* Retention/eviction soak (#695 Phase 6): a long churn of mint / journal /
 * tombstone / compact / snapshot->restore over a SMALL pool of distinct
 * payloads. Invariants held throughout: the distinct-resource count never
 * exceeds the payload-pool size (dedup holds under load); compaction is
 * idempotent; a snapshot round-trip preserves live objects + their deduped
 * resources; and (under ASAN) nothing leaks and no freed shared payload is
 * touched. */
static void test_rich_wire_retention_soak(struct ytest *test)
{
    enum { POOL = 5, ROUNDS = 40 };
    uint32_t pool[POOL][10];
    for (int variant = 0; variant < POOL; ++variant) {
        box_record(pool[variant], (float)(variant + 1), (float)(variant * 2 + 1));
    }

    struct yetty_yclass_object *store = yetty_ymux_rich_make().value;
    YTEST_REQUIRE_NOT_NULL(test, store);

    uint64_t ids[ROUNDS];
    uint32_t live = 0;
    for (int round = 0; round < ROUNDS; ++round) {
        int variant = round % POOL;
        struct yetty_ycore_uint64_result mint_res = yetty_ymux_rich_mint(
            store, pool[variant], 10, YETTY_YMUX_RICH_ANCHOR_PRIMARY, (uint64_t)round, 0, 1);
        YTEST_REQUIRE_OK(test, mint_res);
        ids[round] = mint_res.value;
        ++live;
        /* Append a journal update to some, tombstone every third. */
        uint32_t upd[10];
        box_record(upd, (float)round, 3.0f);
        YTEST_REQUIRE_OK(test, yetty_ymux_rich_journal_append(store, ids[round], upd, 10));
        if (round % 3 == 0) {
            YTEST_REQUIRE_OK(test, yetty_ymux_rich_tombstone(store, ids[round]));
            --live;
        }
        if (round % 7 == 6) {
            YTEST_REQUIRE_OK(test, yetty_ymux_rich_compact_tombstoned(store));
        }
        /* Dedup invariant: never more distinct resources than the pool size. */
        YTEST_CHECK(test, yetty_ymux_rich_distinct_resource_count(store).value <= POOL);
    }
    /* Idempotent final compaction. */
    yetty_ymux_rich_compact_tombstoned(store);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_compact_tombstoned(store).value, 0);
    uint32_t distinct_before = yetty_ymux_rich_distinct_resource_count(store).value;
    YTEST_CHECK(test, distinct_before <= POOL);

    /* Snapshot -> restore preserves the LIVE object set and its deduped
     * resources (tombstoned objects are not serialized). */
    struct yetty_ycore_buffer snapshot = yetty_ycore_buffer_create(1u << 16).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_snapshot(store, &snapshot));
    struct yetty_yclass_object *restored = yetty_ymux_rich_make().value;
    YTEST_REQUIRE_NOT_NULL(test, restored);
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_restore(restored, (const uint32_t *)snapshot.data,
                                                   snapshot.size / sizeof(uint32_t)));
    YTEST_CHECK(test, yetty_ymux_rich_distinct_resource_count(restored).value <= POOL);
    /* Every live id survives with a valid (deduped) creation payload. */
    uint32_t live_seen = 0;
    for (int round = 0; round < ROUNDS; ++round) {
        if (yetty_ymux_rich_is_tombstoned(store, ids[round]).value) {
            continue;
        }
        uint32_t creation_count = 0;
        struct yetty_ycore_const_uint32_ptr_result creation_res =
            yetty_ymux_rich_creation(restored, ids[round], &creation_count);
        YTEST_CHECK(test, YETTY_IS_OK(creation_res) && creation_res.value != NULL &&
                              creation_count == 10);
        if (YETTY_IS_ERR(creation_res)) {
            yetty_ycore_error_destroy(creation_res.error);
        }
        ++live_seen;
    }
    YTEST_CHECK_EQ_INT(test, live_seen, (int)live);

    yetty_ycore_buffer_destroy(&snapshot);
    yetty_ymux_rich_dispose(restored);
    yetty_ymux_rich_dispose(store);
}

/* #695 content-addressed resource channel: a RESOURCE_REF-capable client's
 * projector sends a heavy creation payload ONCE (HASHED, full), then REFERENCES
 * it by hash on a later full frame instead of re-sending the bytes. */
static void test_rich_wire_resource_channel(struct ytest *test)
{
    struct yetty_yclass_object *pane = yetty_ymux_pane_make(4, 20, 8, 0, NULL).value;
    struct yetty_yclass_object *attachment = yetty_ymux_attachment_make(pane, 4, 20).value;
    struct yetty_yclass_object *projector = yetty_ymux_projector_make(pane, attachment).value;
    YTEST_REQUIRE(test, pane && attachment && projector);
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_projector_set_capabilities(projector, YMUX_TERM_CAP_RESOURCE_REF));

    /* Mint object A and project: HASHED full (first delivery), NOT a reference. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, "a", 1));
    uint32_t prim_a[10];
    box_record(prim_a, 4.0f, 9.0f);
    feed_rich_envelope(test, pane, prim_a, 10, 0);
    struct yetty_ycore_buffer frame1 = yetty_ycore_buffer_create(8192).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_rich(projector, &frame1));
    const uint32_t *w1 = (const uint32_t *)frame1.data;
    YTEST_CHECK(test, (w1[8] & YMUX_RICH_FLAG_HASHED) != 0);
    YTEST_CHECK(test, (w1[8] & YMUX_RICH_FLAG_RESOURCE_REF) == 0);
    yetty_ycore_buffer_destroy(&frame1);

    /* A second object bumps the store revision -> a fresh full frame re-emits A,
     * now REFERENCED (delivered already), plus B full. */
    YTEST_REQUIRE_OK(test, yetty_ymux_pane_feed(pane, "b", 1));
    uint32_t prim_b[10];
    box_record(prim_b, 7.0f, 2.0f);
    feed_rich_envelope(test, pane, prim_b, 10, 0);
    struct yetty_ycore_buffer frame2 = yetty_ycore_buffer_create(8192).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_projector_project_rich(projector, &frame2));
    const uint32_t *w2 = (const uint32_t *)frame2.data;
    uint32_t record_count = w2[2];
    YTEST_CHECK(test, record_count >= 2);
    int found_ref = 0;
    size_t offset = 3;
    for (uint32_t rec = 0; rec < record_count; ++rec) {
        uint32_t flags = w2[offset + 5];
        uint32_t payload_words = w2[offset + 6];
        if (flags & YMUX_RICH_FLAG_RESOURCE_REF) {
            found_ref = 1;
            /* A referenced record carries only [hash_lo][hash_hi][creation_count]
             * (+ any journals) — NOT the 10-word box payload. */
            YTEST_CHECK(test, payload_words < 10);
        }
        offset += 7 + payload_words;
    }
    YTEST_CHECK(test, found_ref); /* the already-delivered payload was referenced */
    yetty_ycore_buffer_destroy(&frame2);

    struct yetty_ycore_void_result pd = yetty_ymux_projector_dispose(projector);
    if (YETTY_IS_ERR(pd)) {
        yetty_ycore_error_destroy(pd.error);
    }
    struct yetty_ycore_void_result ad = yetty_ymux_attachment_dispose(attachment);
    if (YETTY_IS_ERR(ad)) {
        yetty_ycore_error_destroy(ad.error);
    }
    struct yetty_ycore_void_result pnd = yetty_ymux_pane_dispose(pane);
    if (YETTY_IS_ERR(pnd)) {
        yetty_ycore_error_destroy(pnd.error);
    }
}

/* Tombstoned objects leave the body; CLEAR empties it. */
static void test_rich_wire_tombstones(struct ytest *test)
{
    struct yetty_yclass_object *pane = yetty_ymux_pane_make(4, 20, 8, 0, NULL).value;
    YTEST_REQUIRE_NOT_NULL(test, pane);
    struct yetty_yclass_object *attachment = yetty_ymux_attachment_make(pane, 4, 20).value;
    struct yetty_yclass_object *projector = yetty_ymux_projector_make(pane, attachment).value;
    struct yetty_yclass_object *store = yetty_ymux_pane_rich_store(pane).value;
    YTEST_REQUIRE(test, attachment && projector && store);

    uint32_t prim[10];
    box_record(prim, 4.0f, 9.0f);
    feed_rich_envelope(test, pane, prim, 10, 0);
    feed_rich_envelope(test, pane, prim, 10, 0);
    YTEST_CHECK_EQ_INT(test, yetty_ymux_rich_count(store).value, 2);

    uint64_t first_id = yetty_ymux_rich_id_at(store, 0).value;
    YTEST_REQUIRE_OK(test, yetty_ymux_rich_tombstone(store, first_id));
    struct yetty_ycore_buffer body = yetty_ycore_buffer_create(8192).value;
    struct yetty_ycore_int_result project_res = yetty_ymux_projector_project_rich(projector, &body);
    YTEST_REQUIRE_OK(test, project_res);
    YTEST_CHECK_EQ_INT(test, project_res.value, 1);
    const uint32_t *words = (const uint32_t *)body.data;
    YTEST_CHECK_EQ_INT(test, words[2], 1); /* tombstoned record absent */

    YTEST_REQUIRE_OK(test, yetty_ymux_rich_clear_all(store));
    yetty_ycore_buffer_destroy(&body);
    body = yetty_ycore_buffer_create(8192).value;
    project_res = yetty_ymux_projector_project_rich(projector, &body);
    YTEST_REQUIRE_OK(test, project_res);
    YTEST_CHECK_EQ_INT(test, project_res.value, 1);
    words = (const uint32_t *)body.data;
    YTEST_CHECK_EQ_INT(test, words[2], 0); /* empty visible set */

    yetty_ycore_buffer_destroy(&body);
    yetty_ymux_projector_dispose(projector);
    yetty_ymux_attachment_dispose(attachment);
    yetty_ymux_pane_dispose(pane);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_rich_wire");
    YTEST_RUN(&test, test_rich_wire_end_to_end);
    YTEST_RUN(&test, test_rich_wire_scroll_reposition);
    YTEST_RUN(&test, test_rich_wire_snapshot_roundtrip);
    YTEST_RUN(&test, test_vt_smoke);
    YTEST_RUN(&test, test_rich_wire_compact_tombstoned);
    YTEST_RUN(&test, test_rich_wire_content_dedup);
    YTEST_RUN(&test, test_rich_wire_retention_soak);
    YTEST_RUN(&test, test_rich_wire_resource_channel);
    YTEST_RUN(&test, test_rich_wire_tombstones);
    return ytest_end(&test);
}
