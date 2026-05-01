/* Unix PTY Factory - creates PTY based on config */

#include "unix-pty.h"

#include <yetty/platform/pty.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/yqemu/qemu.h>
#include <yetty/ytelnet/telnet-pty.h>
#if defined(YETTY_HAS_SSH)
#include <yetty/yssh/ssh-pty.h>
#endif

#include <stdlib.h>

/* Unix PTY factory */
struct yetty_yplatform_unix_pty_factory {
    struct yetty_yplatform_pty_factory base;
    struct yetty_yconfig_config *config;
    struct yetty_yplatform_yprocess *qemu_proc;
};

/* Forward declarations */
static void unix_pty_factory_destroy(struct yetty_yplatform_pty_factory *self);
static struct yetty_yplatform_pty_result unix_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yplatform_event_loop *event_loop);

/* Factory ops table */
static const struct yetty_yplatform_pty_factory_ops unix_pty_factory_ops = {
    .destroy = unix_pty_factory_destroy,
    .create_pty = unix_pty_factory_create_pty,
};

static void unix_pty_factory_destroy(struct yetty_yplatform_pty_factory *self)
{
    struct yetty_yplatform_unix_pty_factory *factory = (struct yetty_yplatform_unix_pty_factory *)self;

    if (factory->qemu_proc) {
        yetty_yqemu_qemu_stop(factory->qemu_proc);
        factory->qemu_proc = NULL;
    }

    free(factory);
}

static struct yetty_yplatform_pty_result unix_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yplatform_event_loop *event_loop)
{
    struct yetty_yplatform_unix_pty_factory *factory = (struct yetty_yplatform_unix_pty_factory *)self;
    struct yetty_yconfig_config *config = factory->config;

    /* --temu: TinyEMU RISC-V VM */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_TEMU, 0)) {
        return yetty_yplatform_tinyemu_pty_create(config);
    }

#if defined(YETTY_HAS_SSH)
    /* --ssh: remote shell via libssh2 */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_SSH, 0)) {
        return yetty_yssh_ssh_pty_create(config);
    }
#endif

    /* --qemu: QEMU via telnet */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_QEMU, 0)) {
        if (!factory->qemu_proc) {
            factory->qemu_proc = yetty_yqemu_qemu_start(QEMU_TELNET_PORT);
            if (!factory->qemu_proc) {
                return YETTY_ERR(yetty_yplatform_pty, "failed to start QEMU");
            }

            struct yetty_ycore_void_result wr = yetty_yqemu_qemu_wait_ready(QEMU_TELNET_PORT, 5000);
            if (YETTY_IS_ERR(wr)) {
                yetty_yqemu_qemu_stop(factory->qemu_proc);
                factory->qemu_proc = NULL;
                return YETTY_ERR(yetty_yplatform_pty, "QEMU telnet not ready", wr);
            }
        }
        return yetty_ytelnet_telnet_pty_create("127.0.0.1", QEMU_TELNET_PORT, event_loop);
    }

    /* Default: native forkpty */
    (void)event_loop;
    return yetty_yplatform_fork_pty_create(config);
}

/* Factory creation - the public API */

struct yetty_yplatform_pty_factory_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific)
{
    struct yetty_yplatform_unix_pty_factory *factory;

    (void)os_specific;

    factory = calloc(1, sizeof(struct yetty_yplatform_unix_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory, "failed to allocate pty factory");
    }

    factory->base.ops = &unix_pty_factory_ops;
    factory->config = config;

    return YETTY_OK(yetty_yplatform_pty_factory, &factory->base);
}
