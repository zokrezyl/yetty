#ifndef YETTY_YPLATFORM_INPUT_PIPE_H
#define YETTY_YPLATFORM_INPUT_PIPE_H

#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_input_pipe;
struct yetty_yplatform_event_loop;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yplatform_input_pipe, struct yetty_ycore_input_pipe *);

/* Platform input pipe ops */
struct yetty_platform_input_pipe_ops {
    void (*destroy)(struct yetty_ycore_input_pipe *self);
    struct yetty_ycore_size_result (*write)(struct yetty_ycore_input_pipe *self,
                                            const void *data, size_t size);
    struct yetty_ycore_size_result (*read)(struct yetty_ycore_input_pipe *self, void *data,
                                           size_t max_size);
    struct yetty_ycore_int_result (*read_fd)(const struct yetty_ycore_input_pipe *self);
    struct yetty_ycore_void_result (*set_event_loop)(struct yetty_ycore_input_pipe *self,
                                                     struct yetty_yplatform_event_loop *loop);
};

/* Platform input pipe base */
struct yetty_ycore_input_pipe {
    const struct yetty_platform_input_pipe_ops *ops;
};

/* Platform-specific create */
struct yetty_yplatform_input_pipe_result yetty_platform_input_pipe_create(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_INPUT_PIPE_H */
