/* GENERATED — do not edit. */
/* Public interface for regular class(es) `plot` (module: ygui2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI2_WIDGETS_PLOT_H
#define YETTY_YCLASSGEN_YGUI2_WIDGETS_PLOT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui2_plot_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_plot;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_PLOT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_PLOT_PTR_RESULT
struct yetty_ygui2_plot_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_plot *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_plot_ptr_result yetty_ygui2_plot_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_plot_to(struct yetty_ygui2_plot *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_plot_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_register(void);

/* Figure/axis configuration — thin forwarders to the wrapped api_yplot
 * object (each appends its DSL directive; last one wins on the wire). */
struct yetty_ycore_void_result yetty_ygui2_plot_set_title(struct yetty_yclass_object *obj,
                                                          const char *title);
/* Append a raw plot-DSL fragment — curves ("sin(3*x) * 0.8"), per-curve
 * colors, axis attributes. The full expression language of yplot. */
struct yetty_ycore_void_result yetty_ygui2_plot_set_expression(struct yetty_yclass_object *obj,
                                                               const char *source);
struct yetty_ycore_void_result yetty_ygui2_plot_set_y_range(struct yetty_yclass_object *obj,
                                                            float min, float max);
struct yetty_ycore_void_result yetty_ygui2_plot_set_x_range(struct yetty_yclass_object *obj,
                                                            float min, float max);
/* Declare the STREAMING buffer: a named, SIZE-ONLY (zero-filled) slot of
 * `capacity` samples plus its reference-curve color. Exactly one such
 * buffer per plot widget — its samples arrive via plot_stream_samples. */
struct yetty_ycore_void_result yetty_ygui2_plot_add_stream_buffer(struct yetty_yclass_object *obj,
                                                                  const char *name,
                                                                  uint32_t capacity,
                                                                  const char *color);
/* Bulk-load the streamed window: ships the FULL capacity every time —
 * `count` samples plus a zeroed tail — followed by a linear ring-head
 * op, in one envelope. Cache and runtime are identical afterwards. For
 * live feeds use plot_append_samples (O(new samples) on the wire). */
struct yetty_ycore_void_result yetty_ygui2_plot_stream_samples(struct yetty_yclass_object *obj,
                                                               const float *samples,
                                                               uint32_t count);
/* APPEND samples — the low-bandwidth streaming primitive. Steady state
 * ships ONLY the new samples plus a ring-head op (~40 bytes for one
 * sample) in one envelope; the receiver's shader unwraps the ring so
 * the display scrolls with nothing re-sent. The one deliberate re-send
 * is replacement recovery: an intentional structural reopen carries the
 * cached window inside its own insertion envelope (plot_paint). */
struct yetty_ycore_void_result yetty_ygui2_plot_append_samples(struct yetty_yclass_object *obj,
                                                               const float *samples,
                                                               uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
