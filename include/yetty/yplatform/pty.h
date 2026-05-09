#ifndef YETTY_YPLATFORM_PTY_H
#define YETTY_YPLATFORM_PTY_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/yplatform/pty-pipe-source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_platform_pty;
YETTY_YRESULT_DECLARE(yetty_yplatform_pty_ptr, struct yetty_platform_pty *);

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

struct yetty_yplatform_pty_factory;
struct yetty_yconfig_config;
struct yetty_yevent_event_loop;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yplatform_pty_factory_ptr, struct yetty_yplatform_pty_factory *);

/* Pty factory ops */
struct yetty_yplatform_pty_factory_ops {
    void (*destroy)(struct yetty_yplatform_pty_factory *self);
    struct yetty_yplatform_pty_ptr_result (*create_pty)(struct yetty_yplatform_pty_factory *self,
                                                        struct yetty_yevent_event_loop *event_loop);
};

/* Pty factory base */
struct yetty_yplatform_pty_factory {
    const struct yetty_yplatform_pty_factory_ops *ops;
};

/* Platform-specific create functions (implemented per platform) */
struct yetty_yplatform_pty_factory_ptr_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_PTY_FACTORY_H */
