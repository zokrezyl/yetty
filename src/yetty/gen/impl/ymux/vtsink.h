/* GENERATED — do not edit. */
/* Public interface for regular class(es) `vtsink` (module: ymux).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YMUX_VTSINK_H
#define YETTY_YCLASSGEN_YMUX_VTSINK_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The sink — the yclass data block. */
struct yetty_yclass_ptr_result yetty_ymux_vtsink_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_vtsink;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_VTSINK_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_VTSINK_PTR_RESULT
struct yetty_ymux_vtsink_ptr_result {
    int ok;
    union {
        struct yetty_ymux_vtsink *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_vtsink_ptr_result yetty_ymux_vtsink_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_to(struct yetty_ymux_vtsink *data);

/* The ordered terminal-byte push the daemon calls over the relay. Synchronous
 * (request/response) so the reply is the application ACK the daemon's VT flow
 * control waits on — a oneway call would swallow write errors and turn a frozen
 * sink into an invisible stall. */
struct yetty_ycore_void_result yetty_ymux_feed(struct yetty_yclass_object *obj, uint64_t generation,
                                               struct yetty_ycore_buffer bytes);

typedef struct yetty_ycore_void_result (*yetty_ymux_feed_fn)(struct yetty_yclass_object *, uint64_t,
                                                             struct yetty_ycore_buffer);

struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymux_register(void);

#ifdef __cplusplus
}
#endif

#endif
