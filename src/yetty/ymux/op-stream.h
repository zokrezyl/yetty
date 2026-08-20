/* op-stream.h — the engine's screen-write OPERATION stream (#699.1).
 *
 * The tmux model renders from the actual screen-write operations, not from
 * settled grids: the engine records every canonical mutation its libvterm
 * state callbacks deliver (putglyph, movecursor, scrollrect, moverect, erase,
 * plus an explicit INVALIDATE for resize/screen-switch) into a per-engine ring
 * with a monotonic sequence. A consumer (the per-attachment projector) tracks
 * the last sequence it consumed and replays the operations since then to
 * drive its tty emitter directly; when the window has been evicted (the
 * consumer fell behind the ring capacity) or an INVALIDATE intervenes, it
 * falls back to a full redraw — the settled-grid diff is the FALLBACK, not
 * the renderer.
 *
 * Module-private (the proto.h/tty-render.h pattern): shared between engine.c
 * (producer) and projector.c (consumer), never a public API. */

#ifndef YETTY_YMUX_OP_STREAM_H
#define YETTY_YMUX_OP_STREAM_H

/* NOTE: this header does NOT include the generated api/ymux/engine.h — the
 * annotated engine.c defines `struct yetty_ymux_cell` itself, and including
 * the generated copy into that TU would collide (annotated vs generated
 * definitions, -Wodr). Every includer must make `struct yetty_ymux_cell`
 * visible FIRST: engine.c via its own definition, consumer TUs (projector.c)
 * via <yetty/api/ymux/engine.h>. */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <stdint.h>

enum yetty_ymux_engine_op_type {
    /* One written cell (pen already applied — the cell snapshot is the
     * canonical result, marks included). a=row, b=col. */
    YMUX_ENGINE_OP_PUTGLYPH = 1,
    /* Cursor moved. a=row, b=col, c=visible. */
    YMUX_ENGINE_OP_MOVECURSOR = 2,
    /* Semantic scroll of a rectangle (recorded BEFORE the fork's
     * move/erase decomposition — this is the operation tmux receives).
     * rect[]=start_row,end_row,start_col,end_col; a=downward, b=rightward
     * (signed, cast). */
    YMUX_ENGINE_OP_SCROLLRECT = 3,
    /* Direct rectangle move (non-scroll path). rect[]=dest rows/cols as
     * start_row,end_row,start_col,end_col; a..=src start_row,start_col. */
    YMUX_ENGINE_OP_MOVERECT = 4,
    /* Rectangle erase to the current pen background. rect[] as above;
     * a=selective; b=the erase background color (packed). */
    YMUX_ENGINE_OP_ERASE = 5,
    /* Everything since is unusable (resize, alt-screen switch): consumers
     * MUST fall back to a full redraw. */
    YMUX_ENGINE_OP_INVALIDATE = 6,
};

struct yetty_ymux_engine_op {
    uint32_t type; /* enum yetty_ymux_engine_op_type */
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t rect[4];             /* start_row, end_row, start_col, end_col */
    struct yetty_ymux_cell cell; /* PUTGLYPH only */
};

enum { YMUX_ENGINE_OP_RING_CAPACITY = 1024 };

/* The next sequence to be written (== number of ops ever recorded). The
 * window [head - min(head, CAPACITY), head) is readable. */
struct yetty_ycore_uint64_result yetty_ymux_engine_op_head(struct yetty_yclass_object *obj);

/* The op at `sequence`, or NULL when the sequence is >= head or already
 * evicted from the ring (consumer fell behind → full-redraw fallback). The
 * pointer is valid until the next engine feed. */
int yetty_ymux_engine_cursor_phantom(struct yetty_yclass_object *obj);
int yetty_ymux_engine_mouse_mode(struct yetty_yclass_object *obj);
const struct yetty_ymux_engine_op *yetty_ymux_engine_op_at(struct yetty_yclass_object *obj,
                                                           uint64_t sequence);

#endif /* YETTY_YMUX_OP_STREAM_H */
