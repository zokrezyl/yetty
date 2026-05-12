/* Unix PTY Factory - creates PTY based on config */

#include <yetty/yplatform/pty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/yqemu/qemu.h>
#include <yetty/ytelnet/telnet-pty.h>
#if defined(YETTY_HAS_SSH)
#include <yetty/yssh/ssh-pty.h>
#endif

#include <stdlib.h>

/* Host-side TCP port that QEMU exposes its hvc0/virtio-console chardev on
 * (see yqemu/qemu.c). yetty.c creates two default tabs in --qemu mode:
 *   tab 1 → telnet to QEMU_CONSOLE_PORT (the guest console)
 *   tab 2 → telnet to QEMU_TELNET_PORT  (the slirp hostfwd → in-guest telnetd)
 */
#define QEMU_CONSOLE_PORT 2424

/* Unix PTY factory */
struct yetty_yplatform_unix_pty_factory {
    struct yetty_yplatform_pty_factory base;
    struct yetty_yconfig_config *config;
    struct yetty_yplatform_yprocess *qemu_proc;
    /* --temu owns the in-process TinyEMU VM lifecycle. yetty creates two
     * default tabs in --temu mode: the first attaches to the in-VM hvc0
     * console (this `tinyemu_pty` field is the parked console pty until
     * tab 1 consumes it via the first create_pty call); the second attaches
     * via telnet to the slirp hostfwd (127.0.0.1:2323 → in-guest telnetd). */
    struct yetty_platform_pty *tinyemu_pty;
    /* Set to 1 once tab 1 has consumed the console PTY (tinyemu hvc0 for
     * --temu, telnet-to-2424 for --qemu). Subsequent create_pty calls
     * dispense a telnet PTY pointing at the NAT'd guest telnetd port. */
    int console_dispensed;
    /* Set to 1 once we've successfully waited for the slirp telnet port
     * to become reachable. Avoids re-running the few-second poll on every
     * create_pty invocation past the second. */
    int nat_telnet_ready;
};

/* Forward declarations */
static void unix_pty_factory_destroy(struct yetty_yplatform_pty_factory *self);
static struct yetty_yplatform_pty_ptr_result unix_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yevent_event_loop *event_loop);

/* Factory ops table */
static const struct yetty_yplatform_pty_factory_ops unix_pty_factory_ops = {
    .destroy = unix_pty_factory_destroy,
    .create_pty = unix_pty_factory_create_pty,
};

static void unix_pty_factory_destroy(struct yetty_yplatform_pty_factory *self)
{
    struct yetty_yplatform_unix_pty_factory *factory =
        (struct yetty_yplatform_unix_pty_factory *)self;

    if (factory->qemu_proc) {
        yetty_yqemu_qemu_stop(factory->qemu_proc);
        factory->qemu_proc = NULL;
    }

    if (factory->tinyemu_pty) {
        struct yetty_ycore_void_result dr =
            factory->tinyemu_pty->ops->destroy(factory->tinyemu_pty);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        factory->tinyemu_pty = NULL;
    }

    free(factory);
}

static struct yetty_yplatform_pty_ptr_result unix_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yevent_event_loop *event_loop)
{
    struct yetty_yplatform_unix_pty_factory *factory =
        (struct yetty_yplatform_unix_pty_factory *)self;
    struct yetty_yconfig_config *config = factory->config;

    /* --telnet: connect to an already-running telnet server (qemu chardev,
     * a tinyemu+slirp hostfwd to in-guest telnetd, an external telnet
     * daemon, etc.). Pure client — owns no server lifecycle. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_TELNET, 0)) {
        const char *host =
            config->ops->get_string(config, YETTY_YCONFIG_KEY_TELNET_HOST, "127.0.0.1");
        int port = config->ops->get_int(config, YETTY_YCONFIG_KEY_TELNET_PORT, 0);
        if (port <= 0 || port > 65535) {
            return YETTY_ERR(yetty_yplatform_pty_ptr, "--telnet requires telnet/port (1..65535)");
        }
        return yetty_ytelnet_telnet_pty_create_tcp(host, (uint16_t)port, event_loop);
    }

    /* --temu: in-process TinyEMU RISC-V VM. yetty.c asks for two PTYs
     * up front (tab 1 + tab 2). The first call hands back the hvc0
     * console pty (tinyemu_pty_create starts the VM); the second hands
     * back a telnet client to the slirp hostfwd (127.0.0.1:2323 →
     * in-guest telnetd). Any further calls (split-pane, Ctrl+T) keep
     * yielding telnet sessions to the NAT'd telnetd — the hvc0 console
     * is unique to the VM and can't be multiplexed. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_TEMU, 0)) {
        if (!factory->console_dispensed) {
            if (!factory->tinyemu_pty) {
                struct yetty_yplatform_pty_ptr_result tr =
                    yetty_yplatform_tinyemu_pty_create(config);
                if (!YETTY_IS_OK(tr)) {
                    return tr;
                }
                factory->tinyemu_pty = tr.value;
            }
            struct yetty_platform_pty *console = factory->tinyemu_pty;
            factory->tinyemu_pty = NULL;
            factory->console_dispensed = 1;
            return YETTY_OK(yetty_yplatform_pty_ptr, console);
        }
        if (!factory->nat_telnet_ready) {
            struct yetty_ycore_void_result wr =
                yetty_yqemu_qemu_wait_ready(TINYEMU_TELNET_PORT, 30000);
            if (YETTY_IS_ERR(wr)) {
                return YETTY_ERR(yetty_yplatform_pty_ptr,
                                 "TinyEMU slirp telnet not ready", wr);
            }
            factory->nat_telnet_ready = 1;
        }
        return yetty_ytelnet_telnet_pty_create_tcp("127.0.0.1", TINYEMU_TELNET_PORT,
                                                   event_loop);
    }

#if defined(YETTY_HAS_SSH)
    /* --ssh: remote shell via libssh2 */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_SSH, 0)) {
        return yetty_yssh_ssh_pty_create(config);
    }
#endif

    /* --qemu: QEMU RISC-V VM. yetty.c asks for two PTYs up front.
     * The first call connects to the qemu virtio-console chardev
     * exposed on 127.0.0.1:2424 (boot log + hvc0 console); the second
     * connects via telnet to the slirp hostfwd (127.0.0.1:2423 →
     * in-guest telnetd). Any further calls keep yielding NAT'd telnet
     * sessions. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_QEMU, 0)) {
        if (!factory->qemu_proc) {
            factory->qemu_proc = yetty_yqemu_qemu_start(QEMU_TELNET_PORT);
            if (!factory->qemu_proc) {
                return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to start QEMU");
            }
        }
        if (!factory->console_dispensed) {
            struct yetty_ycore_void_result wr =
                yetty_yqemu_qemu_wait_ready(QEMU_CONSOLE_PORT, 30000);
            if (YETTY_IS_ERR(wr)) {
                yetty_yqemu_qemu_stop(factory->qemu_proc);
                factory->qemu_proc = NULL;
                return YETTY_ERR(yetty_yplatform_pty_ptr,
                                 "QEMU console chardev not ready", wr);
            }
            factory->console_dispensed = 1;
            return yetty_ytelnet_telnet_pty_create_tcp("127.0.0.1", QEMU_CONSOLE_PORT,
                                                       event_loop);
        }
        if (!factory->nat_telnet_ready) {
            struct yetty_ycore_void_result wr =
                yetty_yqemu_qemu_wait_ready(QEMU_TELNET_PORT, 30000);
            if (YETTY_IS_ERR(wr)) {
                return YETTY_ERR(yetty_yplatform_pty_ptr, "QEMU telnet not ready", wr);
            }
            factory->nat_telnet_ready = 1;
        }
        return yetty_ytelnet_telnet_pty_create_tcp("127.0.0.1", QEMU_TELNET_PORT, event_loop);
    }

    /* Default: native forkpty */
    (void)event_loop;
    return yetty_yplatform_fork_pty_create(config);
}

/* Factory creation - the public API */

struct yetty_yplatform_pty_factory_ptr_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific)
{
    struct yetty_yplatform_unix_pty_factory *factory;

    (void)os_specific;

    factory = calloc(1, sizeof(struct yetty_yplatform_unix_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory_ptr, "failed to allocate pty factory");
    }

    factory->base.ops = &unix_pty_factory_ops;
    factory->config = config;

    return YETTY_OK(yetty_yplatform_pty_factory_ptr, &factory->base);
}
