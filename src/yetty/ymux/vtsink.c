/*
 * vtsink.c — the client-side ordered terminal-byte sink: class@ymux:vtsink
 * (#699.2).
 *
 * A GPU-free relay endpoint hosted by the attach bridge. The daemon's
 * projector originates ordered `feed` calls over the yclass RPC (tunnelled
 * through the bridge's RPC_RELAY channel); the bridge hosts this object and
 * routes each applied feed to the pane's yscene terminal grid through a plain
 * callback — so vtsink itself pulls in neither yscene nor the GPU. `feed` is a
 * request/response slot: its reply is the application ACK, replacing the
 * bespoke YMUX_PROTO_VT frame and its generation-prefix ACK. The applied
 * generation is tracked so a duplicate or stale feed is observable.
 */
#include <stddef.h>
#include <stdint.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* struct yetty_ycore_buffer */

/* Routes an applied feed to the embedder's terminal grid. Set by the bridge
 * via yetty_ymux_vtsink_set_emit; NULL on a freshly created sink (a feed then
 * only advances the tracked generation). */
typedef void (*yetty_ymux_vtsink_emit_fn)(uint64_t generation, const uint8_t *bytes, size_t len,
                                          void *userdata);

/* The sink — the yclass data block. */
struct YETTY_ANNOTATE("class@ymux:vtsink") yetty_ymux_vtsink {
    yetty_ymux_vtsink_emit_fn emit;
    void *emit_userdata;
    uint64_t applied_generation; /* 0 = nothing fed yet */
};

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_vtsink_class_get(void);
struct yetty_ymux_vtsink_ptr_result yetty_ymux_vtsink_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_vtsink_ptr, struct yetty_ymux_vtsink *);

/* The ordered terminal-byte push the daemon calls over the relay. Synchronous
 * (request/response) so the reply is the application ACK the daemon's VT flow
 * control waits on — a oneway call would swallow write errors and turn a frozen
 * sink into an invisible stall. */
YETTY_ANNOTATE("virtual@ymux:vtsink:feed")
static struct yetty_ycore_void_result vtsink_feed_impl(struct yetty_yclass_object *obj,
                                                       uint64_t generation,
                                                       struct yetty_ycore_buffer bytes)
{
    struct yetty_ymux_vtsink_ptr_result self = yetty_ymux_vtsink_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "vtsink_feed: from object");
    if (bytes.size && self.value->emit) {
        self.value->emit(generation, bytes.data, bytes.size, self.value->emit_userdata);
    }
    self.value->applied_generation = generation;
    return YETTY_OK_VOID();
}

/* Route applied feeds to `emit` (the bridge points this at its pane's scene
 * grid). Hand-written setter — no RPC surface, mirrors the client's raw-sink
 * seam. Best-effort: a bad object is ignored (the local wiring site has no
 * Result to receive). */
void yetty_ymux_vtsink_set_emit(struct yetty_yclass_object *obj, yetty_ymux_vtsink_emit_fn emit,
                                void *userdata)
{
    struct yetty_ymux_vtsink_ptr_result self = yetty_ymux_vtsink_from(obj);
    if (YETTY_IS_ERR(self)) {
        yetty_ycore_error_destroy(self.error);
        return;
    }
    self.value->emit = emit;
    self.value->emit_userdata = userdata;
}

/* The highest generation feed() has applied. The lane host reads this after
 * dispatching a batch to ACK the daemon with the applied (not merely received)
 * generation. */
struct yetty_ycore_uint64_result yetty_ymux_vtsink_applied(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_vtsink_ptr_result self = yetty_ymux_vtsink_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, self, "vtsink_applied: from object");
    return YETTY_OK(yetty_ycore_uint64, self.value->applied_generation);
}

/* Allocate a local (in-process) vtsink object. The bridge that hosts the sink
 * server and the contract test both create one this way, then point its emit at
 * the pane grid with yetty_ymux_vtsink_set_emit. A split-mode class rejects a
 * session-bound create, so this is the local-only path (ctx->session NULL). */
struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_make(void)
{
    struct yetty_yclass_ptr_result class_res = yetty_ymux_vtsink_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux vtsink_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux vtsink_make: alloc");
    return object_res;
}

#include "yetty/gen/impl/ymux/vtsink.c"
