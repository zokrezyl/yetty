/* cell-ref-table.c — dense per-cell drawable-ref table.
 *
 * See cell-ref-table.h for the model. Slots hold heap pointers to per-cell
 * drawable_ref_array; a free-list recycles released handles.
 */

#include <yetty/ydraw/cell-ref-table.h>

#include <stdlib.h>
#include <string.h>

/* Slot 0 is reserved so handle 0 reads as "none". */
#define CELL_REF_TABLE_INITIAL_SLOTS 8
#define CELL_REF_TABLE_INITIAL_REFS  2

/*===========================================================================
 * Internal helpers
 *===========================================================================*/

static struct yetty_ycore_void_result ref_array_push(struct drawable_ref_array *array,
                                                     struct drawable_ref ref)
{
    if (array->count >= array->capacity) {
        uint32_t new_capacity =
            array->capacity == 0 ? CELL_REF_TABLE_INITIAL_REFS : array->capacity * 2;
        struct drawable_ref *grown =
            realloc(array->data, new_capacity * sizeof(struct drawable_ref));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "cell_ref_table: ref array realloc failed");
        }
        array->data = grown;
        array->capacity = new_capacity;
    }
    array->data[array->count++] = ref;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result slots_ensure(struct yetty_ydraw_cell_ref_table *table,
                                                   uint32_t min_capacity)
{
    if (min_capacity <= table->capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_capacity =
        table->capacity == 0 ? CELL_REF_TABLE_INITIAL_SLOTS : table->capacity * 2;
    while (new_capacity < min_capacity) {
        new_capacity *= 2;
    }
    struct drawable_ref_array **grown =
        realloc(table->slots, new_capacity * sizeof(struct drawable_ref_array *));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "cell_ref_table: slots realloc failed");
    }
    for (uint32_t slot = table->capacity; slot < new_capacity; slot++) {
        grown[slot] = NULL;
    }
    table->slots = grown;
    table->capacity = new_capacity;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result free_list_push(struct yetty_ydraw_cell_ref_table *table,
                                                     uint32_t handle)
{
    if (table->free_count >= table->free_capacity) {
        uint32_t new_capacity =
            table->free_capacity == 0 ? CELL_REF_TABLE_INITIAL_SLOTS : table->free_capacity * 2;
        uint32_t *grown = realloc(table->free_list, new_capacity * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "cell_ref_table: free-list realloc failed");
        }
        table->free_list = grown;
        table->free_capacity = new_capacity;
    }
    table->free_list[table->free_count++] = handle;
    return YETTY_OK_VOID();
}

static void slot_free(struct yetty_ydraw_cell_ref_table *table, uint32_t handle)
{
    struct drawable_ref_array *array = table->slots[handle];
    if (!array) {
        return;
    }
    free(array->data);
    free(array);
    table->slots[handle] = NULL;
}

/*===========================================================================
 * Lifecycle
 *===========================================================================*/

void yetty_ydraw_cell_ref_table_init(struct yetty_ydraw_cell_ref_table *table)
{
    table->slots = NULL;
    table->count = 0;
    table->capacity = 0;
    table->free_list = NULL;
    table->free_count = 0;
    table->free_capacity = 0;
    table->marks = NULL;
    table->marks_capacity = 0;
}

void yetty_ydraw_cell_ref_table_destroy(struct yetty_ydraw_cell_ref_table *table)
{
    if (!table) {
        return;
    }
    for (uint32_t handle = 0; handle < table->count; handle++) {
        slot_free(table, handle);
    }
    free(table->slots);
    free(table->free_list);
    free(table->marks);
    yetty_ydraw_cell_ref_table_init(table);
}

void yetty_ydraw_cell_ref_table_reset(struct yetty_ydraw_cell_ref_table *table)
{
    for (uint32_t handle = 0; handle < table->count; handle++) {
        slot_free(table, handle);
    }
    table->count = 0;
    table->free_count = 0;
}

/*===========================================================================
 * Handle management
 *===========================================================================*/

struct yetty_ydraw_cell_handle_result yetty_ydraw_cell_ref_table_alloc(
    struct yetty_ydraw_cell_ref_table *table)
{
    uint32_t handle;
    if (table->free_count > 0) {
        handle = table->free_list[--table->free_count];
    } else {
        /* count starts at 0; reserve slot 0 so the first real handle is 1. */
        if (table->count == 0) {
            table->count = 1;
        }
        handle = table->count;
        struct yetty_ycore_void_result ensure = slots_ensure(table, handle + 1);
        if (YETTY_IS_ERR(ensure)) {
            return YETTY_ERR(yetty_ydraw_cell_handle, "cell_ref_table_alloc: grow slots", ensure);
        }
        table->count = handle + 1;
    }

    struct drawable_ref_array *array = calloc(1, sizeof(struct drawable_ref_array));
    if (!array) {
        /* Hand the id back so the table stays consistent. */
        struct yetty_ycore_void_result recycle = free_list_push(table, handle);
        (void)recycle;
        return YETTY_ERR(yetty_ydraw_cell_handle, "cell_ref_table_alloc: ref array alloc failed");
    }
    table->slots[handle] = array;
    return YETTY_OK(yetty_ydraw_cell_handle, handle);
}

void yetty_ydraw_cell_ref_table_release(struct yetty_ydraw_cell_ref_table *table, uint32_t handle)
{
    if (handle == 0 || handle >= table->count || !table->slots[handle]) {
        return;
    }
    slot_free(table, handle);
    /* Best-effort recycle; if the free-list cannot grow, the id is simply
     * not reused — the slot stays NULL and remains correct. */
    struct yetty_ycore_void_result recycle = free_list_push(table, handle);
    (void)recycle;
}

struct drawable_ref_array *yetty_ydraw_cell_ref_table_get(
    const struct yetty_ydraw_cell_ref_table *table, uint32_t handle)
{
    if (handle == 0 || handle >= table->count) {
        return NULL;
    }
    return table->slots[handle];
}

struct yetty_ycore_void_result yetty_ydraw_cell_ref_table_push(
    struct yetty_ydraw_cell_ref_table *table, uint32_t handle, uint16_t lines_ahead,
    uint16_t drawable_index)
{
    struct drawable_ref_array *array = yetty_ydraw_cell_ref_table_get(table, handle);
    if (!array) {
        return YETTY_ERR(yetty_ycore_void, "cell_ref_table_push: invalid handle");
    }
    struct drawable_ref ref = {lines_ahead, drawable_index};
    return ref_array_push(array, ref);
}

/*===========================================================================
 * Garbage collection
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydraw_cell_ref_table_gc_begin(
    struct yetty_ydraw_cell_ref_table *table)
{
    if (table->marks_capacity < table->count) {
        uint8_t *grown = realloc(table->marks, table->count * sizeof(uint8_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "cell_ref_table_gc_begin: marks realloc failed");
        }
        table->marks = grown;
        table->marks_capacity = table->count;
    }
    if (table->count > 0) {
        memset(table->marks, 0, table->count * sizeof(uint8_t));
    }
    return YETTY_OK_VOID();
}

void yetty_ydraw_cell_ref_table_gc_mark(struct yetty_ydraw_cell_ref_table *table, uint32_t handle)
{
    if (handle != 0 && handle < table->count) {
        table->marks[handle] = 1;
    }
}

void yetty_ydraw_cell_ref_table_gc_end(struct yetty_ydraw_cell_ref_table *table)
{
    for (uint32_t handle = 1; handle < table->count; handle++) {
        if (table->slots[handle] && !table->marks[handle]) {
            yetty_ydraw_cell_ref_table_release(table, handle);
        }
    }
}
