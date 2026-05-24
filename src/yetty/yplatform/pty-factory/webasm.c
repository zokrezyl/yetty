/* webasm.c — webasm pty factory: hvc0 console + telnet over iframe.
 *
 * Implements yetty_yplatform_pty_factory_create (the symbol the
 * platform layer looks up to build per-terminal PTYs). Mirrors the
 * desktop factory's two-tab shape (default.c) so yetty.c can request
 * a console + telnet pair regardless of platform:
 *
 *   call 1 → iframe-pty (virtio-console hvc0 of the in-iframe TinyEMU
 *                        VM — singleton, see ypty/iframepty.c).
 *   call 2+ → telnet-pty over iframe-transport, sessionId-multiplexed
 *             into the in-VM telnetd via tinyemu_session_{open,send,close}:
 *
 *               telnet-pty   ←   yetty_ytelnet_telnet_pty_create
 *                  └────  iframe-transport (port=23)   ←
 *                          └─ posts session-open / session-tx / session-close
 *                             to the tinyemu iframe via postMessage. NAWS /
 *                             IAC / option negotiation is handled by the
 *                             same telnet-pty.c that desktop uses.
 *
 *   call 3+ → another telnet sessionId = another forked in-VM shell
 *             (split-pane / Ctrl+T). No shared state across terminals
 *             beyond the single tinyemu iframe + slirp instance.
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
    /* Set to 1 once the iframe-pty (hvc0 console) singleton has been
     * handed out. Subsequent create_pty calls fall back to
     * telnet-over-iframe-transport sessions. */
    int console_dispensed;
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
    /* event_loop is the webasm event loop; iframe-transport doesn't
     * need it (it uses postMessage), but telnet-pty does for its
     * register_pty_pipe call — except we don't use it here either,
     * because telnet-pty creates its own input_pipe and only needs
     * the loop on the desktop TCP path for create_tcp_client. The
     * iframe transport delivers bytes directly to telnet's on_data
     * via the postMessage listener, no event loop involvement. */
    (void)event_loop;

    if (!f->console_dispensed) {
        struct yetty_yplatform_pty_ptr_result cr = yetty_yplatform_iframe_pty_create(f->config);
        if (!YETTY_IS_OK(cr)) {
            return cr;
        }
        f->console_dispensed = 1;
        yinfo("webasm: pty[1] = iframe-pty (hvc0 console)");
        return cr;
    }

    struct yetty_ytransport_conn_transport *transport =
        yetty_ytransport_iframe_transport_create(YETTY_VM_TELNET_PORT);
    if (!transport) {
        return YETTY_ERR(yetty_yplatform_pty_ptr,
                         "telnet-iframe factory: iframe_transport_create failed");
    }
    yinfo("webasm: pty = telnet-over-iframe-transport (port=%d)", YETTY_VM_TELNET_PORT);
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
    yinfo("webasm: pty factory = iframe-hvc0 (call 1) + telnet-over-iframe (call 2+)");
    return YETTY_OK(yetty_yplatform_pty_factory_ptr, &f->base);
}
