/* cell-ref-table.h — dense per-cell drawable-ref table.
 *
 * Bridges the text grid (libvterm cells) and the ydraw drawable model. Each
 * VTermScreenCell carries a 32-bit `rich_handle`; handle 0 means "no rich
 * content", any other value is a dense index into this table. Each live slot
 * holds a 64-bit pointer to that cell's `drawable_ref_array` — the list of
 * {lines_ahead, drawable_index} refs naming the drawables that cover the cell.
 *
 * The handle lives inside the cell struct, so it rides along for free through
 * libvterm scroll (root_row bump) and reflow (whole-struct copy). The table
 * owns the ref arrays; a free-list recycles released handles so the index
 * space stays dense and bounded by the number of simultaneously-rich cells.
 *
 * The `drawable_ref` / `drawable_ref_array` types live here (rather than
 * private to scrolling-grid.c) so the producer (scrolling-canvas), the
 * consumer (scrolling-grid), and the owner (text-layer) all speak one type.
 */
#ifndef YETTY_YDRAW_CELL_REF_TABLE_H
#define YETTY_YDRAW_CELL_REF_TABLE_H

#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One reference from a covered cell to a drawable: the drawable is stored
 * `lines_ahead` canvas rows below this cell, at local index `drawable_index`
 * within that anchor line's prim array. */
struct drawable_ref {
    uint16_t lines_ahead;
    uint16_t drawable_index;
};

struct drawable_ref_array {
    struct drawable_ref *data;
    uint32_t count;
    uint32_t capacity;
};

/* slots[0] is reserved (always NULL) so a handle of 0 reads as "none".
 * slots[handle] owns one heap drawable_ref_array for a rich cell. */
struct yetty_ydraw_cell_ref_table {
    struct drawable_ref_array **slots;
    uint32_t count;    /* high-water mark: slots[0 .. count-1] are addressable */
    uint32_t capacity; /* allocated length of slots[] */

    uint32_t *free_list; /* recycled handle ids available for reuse */
    uint32_t free_count;
    uint32_t free_capacity;

    uint8_t *marks; /* GC reachability bitmap, indexed by handle */
    uint32_t marks_capacity;
};

/* Handle allocation result — value >= 1 on success. */
YETTY_YRESULT_DECLARE(yetty_ydraw_cell_handle, uint32_t);

/* A canvas's view onto the text grid's per-cell rich handles.
 *
 * The text-layer (which owns both the libvterm cells and the ref table)
 * supplies `handle_at`; the ydraw canvas calls it to read/stamp a cell's
 * rich_handle without depending on libvterm directly. The dependency stays
 * one-way: the text-layer reaches down into ydraw, never the reverse. */
struct yetty_ydraw_cell_source {
    /* Returns a writable pointer to the 32-bit rich_handle of the live-screen
     * cell at (row, col), or NULL if the position is off the live screen. The
     * pointer is into the cell buffer and is only valid for the duration of
     * the current canvas operation (no resize/scroll in between). */
    uint32_t *(*handle_at)(void *user, uint32_t row, uint32_t col);
    void *user;                               /* opaque, passed back to handle_at */
    struct yetty_ydraw_cell_ref_table *table; /* borrowed; owned by the text-layer */
};

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

void yetty_ydraw_cell_ref_table_init(struct yetty_ydraw_cell_ref_table *table);

/* Frees every live slot's ref array, the slots vector, and the free-list. */
void yetty_ydraw_cell_ref_table_destroy(struct yetty_ydraw_cell_ref_table *table);

/* Releases every live handle but keeps the slots/free-list allocations, so
 * the next allocations reuse the buffers. Handle ids restart from 1. */
void yetty_ydraw_cell_ref_table_reset(struct yetty_ydraw_cell_ref_table *table);

/*===========================================================================
 * Handle management
 *===========================================================================*/

/* Allocate a fresh handle (>= 1) backed by an empty ref array. Reuses a
 * released id from the free-list when one is available. */
struct yetty_ydraw_cell_handle_result yetty_ydraw_cell_ref_table_alloc(
    struct yetty_ydraw_cell_ref_table *table);

/* Free the ref array for `handle`, NULL its slot, and recycle the id.
 * A handle of 0 (or out of range / already free) is a no-op. Errors only
 * if the free-list cannot grow to take the recycled id. */
struct yetty_ycore_void_result yetty_ydraw_cell_ref_table_release(
    struct yetty_ydraw_cell_ref_table *table, uint32_t handle);

/* Returns the mutable ref array for `handle`, or NULL for 0 / out of range /
 * released. */
struct drawable_ref_array *yetty_ydraw_cell_ref_table_get(
    const struct yetty_ydraw_cell_ref_table *table, uint32_t handle);

/* Append a {lines_ahead, drawable_index} ref to the array for `handle`. */
struct yetty_ycore_void_result yetty_ydraw_cell_ref_table_push(
    struct yetty_ydraw_cell_ref_table *table, uint32_t handle, uint16_t lines_ahead,
    uint16_t drawable_index);

/*===========================================================================
 * Garbage collection
 *
 * Slots are never freed on the producer/consumer side; instead the owner runs
 * a mark-sweep over the cell buffers that carry the handles. Nothing else
 * releases a slot, so the many cell-duplicating memmove/scroll/reflow paths
 * can never double-free. Usage:
 *     gc_begin(table);
 *     for each physical cell:  gc_mark(table, cell.rich_handle);
 *     gc_end(table);   // frees every live slot that went unmarked
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydraw_cell_ref_table_gc_begin(
    struct yetty_ydraw_cell_ref_table *table);
void yetty_ydraw_cell_ref_table_gc_mark(struct yetty_ydraw_cell_ref_table *table, uint32_t handle);
/* Frees every live slot that went unmarked. Best-effort: surfaces the first
 * id-recycle failure (free-list grow) but always finishes the sweep. */
struct yetty_ycore_void_result yetty_ydraw_cell_ref_table_gc_end(
    struct yetty_ydraw_cell_ref_table *table);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CELL_REF_TABLE_H */
