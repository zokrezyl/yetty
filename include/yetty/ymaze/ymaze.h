#ifndef YETTY_YMAZE_YMAZE_H
#define YETTY_YMAZE_YMAZE_H

/*
 * ymaze — animated maze generator + solver, drawn as ydraw SDF prims.
 *
 * Port of yetty-poc/src/yetty/ydraw-maze (C++ → C). The renderer:
 *   1. Generates a maze with a recursive-backtracker DFS.
 *   2. Solves it with BFS from (0,0) to (cols-1, rows-1).
 *   3. Each render(time) emits walls, start/end markers, an animated trail,
 *      and an actor circle interpolated along the path.
 *   4. When auto_regen is on, finishing the path triggers a fresh maze.
 *
 * The renderer is a pure producer of ydraw primitives — no IO, no GPU,
 * no terminal interaction. The frontend tool drives time, owns the buffer,
 * and ships it out via OSC.
 */

#include <stdbool.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ymaze;

YETTY_YRESULT_DECLARE(yetty_ymaze_ptr, struct yetty_ymaze *);

struct yetty_ymaze_config {
    uint32_t cols;
    uint32_t rows;
    float actor_speed; /* cells per second */
    float wall_width;
    uint32_t wall_color; /* 0xAARRGGBB */
    uint32_t actor_color;
    uint32_t start_color;
    uint32_t end_color;
    uint32_t bg_color;
    float scene_width;
    float scene_height;
    bool auto_regen; /* regenerate when actor finishes */
};

/* Defaults equivalent to the C++ MazeConfig. */
struct yetty_ymaze_config yetty_ymaze_config_default(void);

/* `seed = 0` → seed from CLOCK_MONOTONIC. */
struct yetty_ymaze_ptr_result yetty_ymaze_create(const struct yetty_ymaze_config *config,
                                                 uint32_t seed);

void yetty_ymaze_destroy(struct yetty_ymaze *maze);

/* Update the scene-size config (call when the host pane resizes). Triggers
 * a re-layout of cell sizes on the next render. */
struct yetty_ycore_void_result yetty_ymaze_set_scene_size(struct yetty_ymaze *maze,
                                                          float scene_width, float scene_height);

/* Force a fresh generate + solve on the next render. */
struct yetty_ycore_void_result yetty_ymaze_regenerate(struct yetty_ymaze *maze);

/* Generate (if first call) or auto-regenerate (if finished + auto_regen),
 * then write all primitives for the current time into `buf`. The buffer is
 * cleared and its scene bounds are set to (0, 0, scene_width, scene_height).
 * `*out_regenerated` (may be NULL) is set to true iff a new maze was generated
 * during this call. */
struct yetty_ycore_void_result yetty_ymaze_render(struct yetty_ymaze *maze,
                                                  struct yetty_ydraw_core_buffer *buf, float time,
                                                  bool *out_regenerated);

bool yetty_ymaze_is_finished(const struct yetty_ymaze *maze, float time);

const struct yetty_ymaze_config *yetty_ymaze_config_get(const struct yetty_ymaze *maze);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMAZE_YMAZE_H */
