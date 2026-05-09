/* telnet-iframe-pty-factory.c — webasm pty factory: telnet over iframe.
 *
 * Implements yetty_yplatform_pty_factory_create (the symbol the
 * platform layer looks up to build per-terminal PTYs). Each call to
 * factory->create_pty mints:
 *
 *   telnet-pty   ←   yetty_ycore_void_result yetty_ytelnet_telnet_pty_create
 *      └───────  iframe-transport (port=23)   ←
 *                  └─ posts session-open / session-tx / session-close
 *                     to the tinyemu iframe via postMessage; iframe
 *                     uses tinyemu_session_{open,send,close} to
 *                     manufacture a chr-backed slirp connection
 *                     directly into the in-VM telnetd. NAWS / IAC /
 *                     option negotiation is handled by the same
 *                     telnet-pty.c that desktop uses.
 *
 * Multiple terminals = multiple factory calls = multiple sessionIds
 * = multiple in-VM forked shells. No shared state across terminals
 * beyond the single tinyemu iframe + slirp instance.
 *
 * The legacy iframe-pty.c (virtio-console hvc0) stays compiled and
 * is reachable via yetty_yplatform_iframe_pty_create() if anyone
 * wants to attach yetty to the boot console for debugging — it just
 * isn't the default factory anymore. The boot overlay still mirrors
 * hvc0 from the iframe side via mirrorKernelOutput().
 */

#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ytransport/conn-transport.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ytransport/iframe-transport.h>
#include <yetty/ytelnet/telnet-pty.h>

#include <stdint.h>
#include <stdlib.h>

/* The guest-side telnetd port. Matches the cfg's slirp setup
 * (see assets/yemu/temu/yetty-temu-extended.cfg — telnetd is on
 * port 23 inside the VM, the cfg's hostfwd "tcp:2323-:23" is
 * irrelevant for our chr-backend path because we inject into the
 * guest port directly). */
#define YETTY_VM_TELNET_PORT 23

struct webasm_telnet_factory {
    struct yetty_yplatform_pty_factory base;
    struct yetty_yconfig_config *config;
};

static void factory_destroy(struct yetty_yplatform_pty_factory *self)
{
    struct webasm_telnet_factory *f = (struct webasm_telnet_factory *)self;
    free(f);
}

static struct yetty_yplatform_pty_ptr_result factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yevent_event_loop *event_loop)
{
    struct webasm_telnet_factory *f = (struct webasm_telnet_factory *)self;
    (void)f;
    /* event_loop is the webasm event loop; iframe-transport doesn't
     * need it (it uses postMessage), but telnet-pty does for its
     * register_pty_pipe call — except we don't use it here either,
     * because telnet-pty creates its own input_pipe and only needs
     * the loop on the desktop TCP path for create_tcp_client. The
     * iframe transport delivers bytes directly to telnet's on_data
     * via the postMessage listener, no event loop involvement. */
    (void)event_loop;

    struct yetty_ytransport_conn_transport *transport =
        yetty_ytransport_iframe_transport_create(YETTY_VM_TELNET_PORT);
    if (!transport) {
        return YETTY_ERR(yetty_yplatform_pty_ptr,
                         "telnet-iframe factory: iframe_transport_create failed");
    }
    /* telnet_pty_create takes ownership of the transport — it will
     * destroy on every failure path. */
    return yetty_ytelnet_telnet_pty_create(transport);
}

static const struct yetty_yplatform_pty_factory_ops factory_ops = {
    .destroy = factory_destroy,
    .create_pty = factory_create_pty,
};

struct yetty_yplatform_pty_factory_ptr_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific)
{
    (void)os_specific;

    struct webasm_telnet_factory *f = calloc(1, sizeof(*f));
    if (!f) {
        return YETTY_ERR(yetty_yplatform_pty_factory_ptr,
                         "failed to allocate telnet-iframe pty factory");
    }
    f->base.ops = &factory_ops;
    f->config = config;
    yinfo("webasm: pty factory = telnet-over-iframe-transport (port=%d)", YETTY_VM_TELNET_PORT);
    return YETTY_OK(yetty_yplatform_pty_factory_ptr, &f->base);
}
