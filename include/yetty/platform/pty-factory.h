#ifndef YETTY_YPLATFORM_PTY_FACTORY_H
#define YETTY_YPLATFORM_PTY_FACTORY_H

#include <yetty/ycore/result.h>
#include <yetty/platform/pty.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yplatform_pty_factory;
struct yetty_yconfig;
struct yetty_ycore_event_loop;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yplatform_pty_factory, struct yetty_yplatform_pty_factory *);
YETTY_YRESULT_DECLARE(yetty_yplatform_pty, struct yetty_yplatform_pty *);

/* Pty factory ops */
struct yetty_yplatform_pty_factory_ops {
    void (*destroy)(struct yetty_yplatform_pty_factory *self);
    struct yetty_yplatform_pty_result (*create_pty)(struct yetty_yplatform_pty_factory *self,
                                                    struct yetty_ycore_event_loop *event_loop);
};

/* Pty factory base */
struct yetty_yplatform_pty_factory {
    const struct yetty_yplatform_pty_factory_ops *ops;
};

/* Platform-specific create functions (implemented per platform) */
struct yetty_yplatform_pty_factory_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig *config, void *os_specific);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_PTY_FACTORY_H */
