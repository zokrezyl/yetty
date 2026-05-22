#ifndef YETTY_YPLATFORM_INPUT_PIPE_H
#define YETTY_YPLATFORM_INPUT_PIPE_H

#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_xthread_event_pipe;
struct yetty_yevent_event_loop;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yplatform_input_pipe, struct yetty_ycore_xthread_event_pipe *);

/* Platform input pipe ops */
struct yetty_platform_input_pipe_ops {
    void (*destroy)(struct yetty_ycore_xthread_event_pipe *self);
    struct yetty_ycore_size_result (*write)(struct yetty_ycore_xthread_event_pipe *self,
                                            const void *data, size_t size);
    struct yetty_ycore_size_result (*read)(struct yetty_ycore_xthread_event_pipe *self, void *data,
                                           size_t max_size);
    struct yetty_ycore_int_result (*read_fd)(const struct yetty_ycore_xthread_event_pipe *self);
    struct yetty_ycore_void_result (*set_event_loop)(struct yetty_ycore_xthread_event_pipe *self,
                                                     struct yetty_yevent_event_loop *loop);
    /* Switch the producer side to non-blocking. After this, write() may
     * return a short count (or 0) instead of blocking when the kernel
     * buffer is full — the caller is responsible for retrying later.
     * Required by single-threaded producer/consumer setups where blocking
     * the writer would also park the consumer (e.g. telnet_pty feeding
     * the wire-statemachine: both live on the same libuv loop, so a
     * write that waits for the reader deadlocks the whole loop). */
    struct yetty_ycore_void_result (*set_nonblocking_write)(
        struct yetty_ycore_xthread_event_pipe *self);
};

/* Platform input pipe base */
struct yetty_ycore_xthread_event_pipe {
    const struct yetty_platform_input_pipe_ops *ops;
};

/* Platform-specific create */
struct yetty_yplatform_input_pipe_result yetty_platform_input_pipe_create(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_INPUT_PIPE_H */
