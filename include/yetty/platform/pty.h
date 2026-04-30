#ifndef YETTY_YPLATFORM_PTY_H
#define YETTY_YPLATFORM_PTY_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/platform/pty-pipe-source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_platform_pty;

/* Pty ops */
struct yetty_platform_pty_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_platform_pty *self);
    struct yetty_ycore_size_result (*read)(struct yetty_platform_pty *self, char *buf,
                                           size_t max_len);
    struct yetty_ycore_size_result (*write)(struct yetty_platform_pty *self, const char *data,
                                            size_t len);
    struct yetty_ycore_void_result (*resize)(struct yetty_platform_pty *self, uint32_t cols,
                                             uint32_t rows);
    struct yetty_ycore_void_result (*stop)(struct yetty_platform_pty *self);
    struct yetty_platform_pty_pipe_source *(*pipe_source)(struct yetty_platform_pty *self);
};

/* Pty base */
struct yetty_platform_pty {
    const struct yetty_platform_pty_ops *ops;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_PTY_H */
