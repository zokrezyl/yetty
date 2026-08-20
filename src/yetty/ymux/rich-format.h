/*
 * rich-format.h — the ymux rich-body wire format: the rich half of one
 * apply_content_transaction (#695). Module-private (the paint-format.h
 * pattern), shared between the server-side projector (producer) and the
 * client-side scene rich world (consumer).
 *
 * V1 scope per the issue: row-anchored ydraw records. A rich body is the
 * COMPLETE visible set for the viewport generation it rides with —
 * reconciliation is replace-the-world (tombstones exist for the phase-4
 * journal path). Everything little-endian u32 words:
 *
 *   YMUX_RICH_MAGIC, YMUX_RICH_VERSION, record_count,
 *   record_count × record:
 *     rich_id lo, rich_id hi,      (stable server id)
 *     rich_state_revision,
 *     anchor_row, anchor_col,      (viewport cell the payload is
 *                                   anchored to — the projector resolves
 *                                   stable anchors before sending)
 *     flags,                       (bit0 tombstone, bit1 reposition)
 *     prim_word_count,
 *     prim_word_count × payload words — a sequence of ysdf primitive
 *       records ([type, layer, fill, stroke, stroke_w] + geometry) in
 *       pixel coordinates RELATIVE to the anchor cell origin.
 *
 * SCROLL is a REPOSITION frame, never a re-send (yvterm's apply_scroll_anchor
 * / rolling-row model): when only the viewport scroll moved — the figure's
 * content and the visible SET are unchanged — the projector emits records with
 * flags=REPOSITION and prim_word_count=0 (no payload). The consumer moves the
 * existing figure nodes to the new anchor_row WITHOUT rebuilding the world, so
 * scrolling past a 20 KB figure costs one small position frame per step, not a
 * re-transmit. A frame is all-full or all-reposition (never mixed); the
 * consumer keys off the FIRST record's REPOSITION flag. A content change or a
 * visible-set change (a figure entering/leaving the viewport, or a tombstone)
 * forces a full frame that rebuilds the world.
 */
#ifndef YETTY_YMUX_RICH_FORMAT_H
#define YETTY_YMUX_RICH_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    YMUX_RICH_MAGIC = 0x594D5052, /* "YMPR" */
    YMUX_RICH_VERSION = 1,
    YMUX_RICH_HEADER_WORDS = 3,
    YMUX_RICH_RECORD_HEADER_WORDS = 7,

    YMUX_RICH_FLAG_TOMBSTONE = 1u << 0,
    YMUX_RICH_FLAG_REPOSITION = 1u << 1,   /* position-only record; no payload */
    YMUX_RICH_FLAG_HASHED = 1u << 2,       /* the payload is prefixed with the
                                            * creation payload's [hash_lo][hash_hi]
                                            * (content addressing enabled for this
                                            * client). Self-describing so the
                                            * client parses the prefix without
                                            * knowing the capability out of band. */
    YMUX_RICH_FLAG_RESOURCE_REF = 1u << 3, /* with HASHED: the creation bytes are
                                            * OMITTED (payload = [hash][journals]);
                                            * the client resolves them from its
                                            * by-hash cache instead of the wire
                                            * re-sending the heavy payload (#695).
                                            * Without it: [hash][creation][journals]
                                            * and the client caches under hash. */
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMUX_RICH_FORMAT_H */
