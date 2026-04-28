#ifndef YETTY_YPAINT_CANVAS_H
#define YETTY_YPAINT_CANVAS_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yetty_ypaint_canvas;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yetty_ypaint_canvas, struct yetty_yetty_ypaint_canvas *);

/* Glyph bit marker for packed grid */
#define YETTY_YPAINT_CANVAS_GLYPH_BIT 0x80000000u

/* Canvas ops */
struct yetty_yetty_ypaint_canvas_ops {
    void (*destroy)(struct yetty_yetty_ypaint_canvas *self);

    /* Mode */
    int (*scrolling_mode)(const struct yetty_yetty_ypaint_canvas *self);

    /* Configuration */
    void (*set_scene_bounds)(struct yetty_yetty_ypaint_canvas *self, float min_x, float min_y,
                             float max_x, float max_y);
    void (*set_cell_size)(struct yetty_yetty_ypaint_canvas *self, float size_x, float size_y);
    void (*set_max_prims_per_cell)(struct yetty_yetty_ypaint_canvas *self, uint32_t max);

    /* Scene bounds accessors */
    float (*scene_min_x)(const struct yetty_yetty_ypaint_canvas *self);
    float (*scene_min_y)(const struct yetty_yetty_ypaint_canvas *self);
    float (*scene_max_x)(const struct yetty_yetty_ypaint_canvas *self);
    float (*scene_max_y)(const struct yetty_yetty_ypaint_canvas *self);

    /* Grid dimension accessors */
    float (*cell_size_x)(const struct yetty_yetty_ypaint_canvas *self);
    float (*cell_size_y)(const struct yetty_yetty_ypaint_canvas *self);
    uint32_t (*grid_width)(const struct yetty_yetty_ypaint_canvas *self);
    uint32_t (*grid_height)(const struct yetty_yetty_ypaint_canvas *self);
    size_t (*line_count)(const struct yetty_yetty_ypaint_canvas *self);

    /* Cursor (scrolling mode) */
    void (*set_cursor_position)(struct yetty_yetty_ypaint_canvas *self, uint16_t col, uint16_t row);
    uint16_t (*cursor_col)(const struct yetty_yetty_ypaint_canvas *self);
    uint16_t (*cursor_row)(const struct yetty_yetty_ypaint_canvas *self);

    /* Rolling offset (scrolling mode) */
    uint32_t (*row_origin)(const struct yetty_yetty_ypaint_canvas *self);
    uint32_t (*rolling_row)(const struct yetty_yetty_ypaint_canvas *self);

    /* Primitive management */
    void (*add_primitive)(struct yetty_yetty_ypaint_canvas *self, const float *prim_data,
                          size_t prim_data_count, float aabb_min_x, float aabb_min_y,
                          float aabb_max_x, float aabb_max_y);

    /* Scrolling */
    void (*scroll_lines)(struct yetty_yetty_ypaint_canvas *self, uint16_t num_lines);

    /* Dirty tracking */
    void (*mark_dirty)(struct yetty_yetty_ypaint_canvas *self);
    int (*is_dirty)(const struct yetty_yetty_ypaint_canvas *self);

    /* Packed GPU format */
    void (*rebuild_packed_grid)(struct yetty_yetty_ypaint_canvas *self);
    const uint32_t *(*grid_staging)(const struct yetty_yetty_ypaint_canvas *self);
    size_t (*grid_staging_size)(const struct yetty_yetty_ypaint_canvas *self);

    /* State */
    void (*clear)(struct yetty_yetty_ypaint_canvas *self);
    int (*empty)(const struct yetty_yetty_ypaint_canvas *self);
    uint32_t (*primitive_count)(const struct yetty_yetty_ypaint_canvas *self);
};

/* Canvas base */
struct yetty_yetty_ypaint_canvas {
    const struct yetty_yetty_ypaint_canvas_ops *ops;
};

/* Create canvas */
struct yetty_yetty_ypaint_canvas_result yetty_yetty_ypaint_canvas_create(int scrolling_mode);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPAINT_CANVAS_H */
