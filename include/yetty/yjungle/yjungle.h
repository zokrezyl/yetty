#ifndef YETTY_YJUNGLE_YJUNGLE_H
#define YETTY_YJUNGLE_YJUNGLE_H

/*
 * yjungle — incremental-update test scene for scene-canvas.
 *
 * Maintains a connected chain of "segments" through the scene. Each
 * segment is either:
 *   - a single SDF primitive (drawn between the segment's start/end), or
 *   - a group: 2..N sub-segments chained inside the parent's span; each
 *     sub-segment is itself either a primitive or a (deeper) group.
 *
 * Groups on the wire use CMD_GROUP / CMD_DELETE (see ydraw-core/cmds.h).
 * Nested groups produce nested CMD_GROUP records — exercises scene-
 * canvas's process_group_body recursion.
 *
 * The library is a pure producer. Each call to _tick() writes only the
 * INCREMENTAL wire commands for this frame into the supplied drawable_list:
 *
 *   - first ever tick: CMD_ZERO + a GROUP per initial segment.
 *   - other ticks: if it's time for an "event":
 *       * extend  : append GROUP(new_id) for a new tail segment.
 *       * replace : DELETE(old_id) + GROUP(new_id) keeping the same
 *                   (start, end) so neighbours stay connected.
 *     otherwise the drawable_list is left empty (frontend may skip emit).
 *
 * The event cadence and parameters are randomised within the bounds in
 * yjungle_config. Identity (group_id) is monotonic — the producer never
 * reuses an id, so the receiver's strict "GROUP id already exists" check
 * never fires for our re-emits.
 */

#include <stdbool.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yjungle;

YETTY_YRESULT_DECLARE(yetty_yjungle_ptr, struct yetty_yjungle *);

struct yetty_yjungle_config {
    /* Scene size in pixels. The chain's random walk is clamped to a
     * small margin (off_canvas_margin) beyond these bounds so the
     * receiver sees some fully-off-canvas segments too. */
    float scene_width;
    float scene_height;

    /* Random-walk step length in pixels. Each new tail segment's
     * (end - start) vector has magnitude drawn from [step_min, step_max]
     * and a uniformly random direction. */
    float step_min;
    float step_max;

    /* Allow segments to extend up to this many pixels beyond the scene
     * rectangle before the walk is reflected back inward. 0 = strict
     * clip. */
    float off_canvas_margin;

    /* Recursion knobs.
     *   max_depth                — hard cap on group nesting (depth=0 is
     *                              a top-level chain segment).
     *   group_prob_depth0        — probability of "this segment is a
     *                              group, not a primitive" at depth 0.
     *                              Halves at each deeper level. Set to 0
     *                              to disable groups entirely (every
     *                              segment is a single primitive).
     *   group_children_min/max   — N of sub-segments inside a group.
     */
    uint32_t max_depth;
    float group_prob_depth0;
    uint32_t group_children_min;
    uint32_t group_children_max;

    /* Chain shape over time.
     *   initial_chain_length     — number of segments to emit on the
     *                              first tick.
     *   max_chain_length         — once reached, events are forced to be
     *                              "replace" (no further extension).
     *   extend_probability       — when chain is not at the cap, this is
     *                              the chance the next event is an
     *                              extend (vs. replace).
     */
    uint32_t initial_chain_length;
    uint32_t max_chain_length;
    float extend_probability;

    /* Event cadence — interval in milliseconds between mutation events.
     * Each event picks a new random delay in [min, max]. */
    uint32_t event_interval_ms_min;
    uint32_t event_interval_ms_max;
};

struct yetty_yjungle_config yetty_yjungle_config_default(void);

/* seed=0 → seed from CLOCK_MONOTONIC. */
struct yetty_yjungle_ptr_result yetty_yjungle_create(const struct yetty_yjungle_config *config,
                                                     uint32_t seed);

void yetty_yjungle_destroy(struct yetty_yjungle *jungle);

/* Update scene bounds; the random walk respects them on subsequent
 * extend/replace events. Existing segments are not relocated. */
struct yetty_ycore_void_result yetty_yjungle_set_scene_size(struct yetty_yjungle *jungle,
                                                            float scene_width, float scene_height);

/* Drive one frame. Writes the incremental commands for this tick into
 * `buf`. The buffer is cleared at the start of the call; if nothing
 * happened this tick the buffer ends empty (size=0 after serialize) and
 * the frontend should not ship an envelope.
 *
 * `now_ms` is a monotonic millisecond clock — used to decide whether an
 * event fires this tick. */
struct yetty_ycore_void_result yetty_yjungle_tick(struct yetty_yjungle *jungle,
                                                  struct yetty_ydraw_drawable_list *buf,
                                                  uint64_t now_ms);

/* Full-redraw variant of yetty_yjungle_tick: advance the simulation to
 * `now_ms`, then write the entire current chain into `buf` as a flat
 * primitive list (no GROUP/DELETE deltas). The buffer is cleared first.
 * For consumers that repaint a full prim list every frame — e.g. the ygui
 * `ymaze`-style ydraw_embed widget — rather than accumulating deltas on a
 * persistent scene canvas. */
struct yetty_ycore_void_result yetty_yjungle_render(struct yetty_yjungle *jungle,
                                                    struct yetty_ydraw_drawable_list *buf,
                                                    uint64_t now_ms);

const struct yetty_yjungle_config *yetty_yjungle_config_get(const struct yetty_yjungle *jungle);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YJUNGLE_YJUNGLE_H */
