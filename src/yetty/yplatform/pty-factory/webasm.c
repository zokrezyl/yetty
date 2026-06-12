/* webasm.c — webasm pty factory: TinyEMU iframe console OR websocket
 * transports, selected by config.
 *
 * Implements yetty_yplatform_pty_factory_create (the symbol the
 * platform layer looks up to build per-terminal PTYs). Session modes
 * (set by index.html via Module.arguments, parsed by yconfig):
 *
 *   --temu (and the no-flag default, preserving the original webasm
 *   behaviour) → in-iframe TinyEMU VM:
 *     call 1 → iframe-pty (virtio-console hvc0 of the in-iframe TinyEMU
 *                          VM — singleton, see ypty/iframepty.c).
 *     call 2+ → telnet-pty over iframe-transport, sessionId-multiplexed
 *               into the in-VM telnetd via tinyemu_session_{open,send,close}:
 *
 *                 telnet-pty   ←   yetty_ytelnet_telnet_pty_create
 *                    └────  iframe-transport (port=23)   ←
 *                            └─ posts session-open / session-tx / session-close
 *                               to the tinyemu iframe via postMessage. NAWS /
 *                               IAC / option negotiation is handled by the
 *                               same telnet-pty.c that desktop uses.
 *
 *     call 3+ → another telnet sessionId = another forked in-VM shell
 *               (split-pane / Ctrl+T). No shared state across terminals
 *               beyond the single tinyemu iframe + slirp instance.
 *
 *   --websocket [URL] → every call returns a websocket-pty: raw shell
 *     bytes to a websocket PTY server
 *     (tools/websocket-pty-server/websocket-pty-server.py), one
 *     WebSocket connection — and thus one server-side shell — per
 *     terminal. Resize travels as the 0x01 control message.
 *
 *   --telnet --websocket-url URL → every call returns a telnet-pty
 *     over websocket-transport. The server side is a plain
 *     websocket↔TCP bridge to a telnetd (tools/telnet-websocket.sh).
 *     The browser sandbox has no raw TCP, so telnet/host+port are
 *     ignored on webasm — the bridge owns the TCP endpoint.
 *
 *   --ssh USER@HOST --ssh-password PASS --websocket-url URL → every
 *     call returns an ssh-websocket-pty: libssh2 (non-blocking,
 *     mbedTLS) terminates the SSH crypto in the browser; the bridge
 *     at URL relays the encrypted stream to sshd. The desktop
 *     ssh-pty.c (threads + blocking sockets) is not used here.
 */

#include <yetty/yplatform/pty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ytransport/conn-transport.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ytransport/iframe-transport.h>
#include <yetty/ytransport/websocket-transport.h>
#include <yetty/ytransport/lwip-transport.h>
#include <yetty/ytelnet/telnet-pty.h>
#include <yetty/ypty/websocket-pty.h>
#include <yetty/yssh/ssh-websocket-pty.h>

#include <stdint.h>
#include <stdlib.h>

/* The guest-side telnetd port. Matches the cfg's slirp setup
 * (see assets/yemu/temu/yetty-temu-extended.cfg — telnetd is on
 * port 23 inside the VM, the cfg's hostfwd "tcp:2323-:23" is
 * irrelevant for our chr-backend path because we inject into the
 * guest port directly). */
#define YETTY_VM_TELNET_PORT 23

/* Default websocket endpoints when --websocket / --telnet are given
 * without a URL. Match the server tools' default listen ports:
 * websocket-pty-server.py listens on 8090, telnet-websocket.sh
 * bridges on 8024. */
#define YETTY_WEBSOCKET_PTY_DEFAULT_URL "ws://127.0.0.1:8090"
#define YETTY_WEBSOCKET_TELNET_DEFAULT_URL "ws://127.0.0.1:8024"
#define YETTY_WEBSOCKET_SSH_DEFAULT_URL "ws://127.0.0.1:8025"

struct webasm_pty_factory {
    struct yetty_yplatform_pty_factory base;
    struct yetty_yconfig_config *config;
    /* Set to 1 once the iframe-pty (hvc0 console) singleton has been
     * handed out (--temu path). Subsequent create_pty calls fall back
     * to telnet-over-iframe-transport sessions. */
    int console_dispensed;
};

static void factory_destroy(struct yetty_yplatform_pty_factory *self)
{
    struct webasm_pty_factory *factory = (struct webasm_pty_factory *)self;
    free(factory);
}

/* --temu (and default): hvc0 console first, then in-VM telnet sessions. */
static struct yetty_yplatform_pty_ptr_result factory_create_temu_pty(
    struct webasm_pty_factory *factory)
{
    if (!factory->console_dispensed) {
        struct yetty_yplatform_pty_ptr_result console_res =
            yetty_yplatform_iframe_pty_create(factory->config);
        if (!YETTY_IS_OK(console_res)) {
            return console_res;
        }
        factory->console_dispensed = 1;
        yinfo("webasm: pty[1] = iframe-pty (hvc0 console)");
        return console_res;
    }

    struct yetty_ytransport_conn_transport *transport =
        yetty_ytransport_iframe_transport_create(YETTY_VM_TELNET_PORT);
    if (!transport) {
        return YETTY_ERR(yetty_yplatform_pty_ptr,
                         "webasm factory: iframe_transport_create failed");
    }
    yinfo("webasm: pty = telnet-over-iframe-transport (port=%d)", YETTY_VM_TELNET_PORT);
    /* telnet_pty_create takes ownership of the transport — it will
     * destroy on every failure path. */
    return yetty_ytelnet_telnet_pty_create(transport);
}

static struct yetty_yplatform_pty_ptr_result factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yevent_event_loop *event_loop)
{
    struct webasm_pty_factory *factory = (struct webasm_pty_factory *)self;
    struct yetty_yconfig_config *config = factory->config;
    /* The webasm event loop isn't needed here: both transports deliver
     * bytes via browser callbacks (postMessage / WebSocket events),
     * and the PTYs own their output pipes. */
    (void)event_loop;

    /* When --net-relay is set, the in-browser lwIP netstack is up, so
     * telnet/ssh connect to the *real* host:port over it (no per-host
     * ws↔TCP bridge) — yetty's own TCP stack does the dialling. Without
     * --net-relay we fall back to the websocket-bridge transports. */
    int via_relay = config && config->ops->get_string(config, YETTY_YCONFIG_KEY_NET_RELAY, "")[0];

    /* --telnet: a telnet-pty (NAWS/IAC) over either the lwIP netstack
     * (real host:port via the relay) or a ws↔TCP bridge. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_TELNET, 0)) {
        struct yetty_ytransport_conn_transport *transport;
        if (via_relay) {
            const char *host =
                config->ops->get_string(config, YETTY_YCONFIG_KEY_TELNET_HOST, "127.0.0.1");
            int port = config->ops->get_int(config, YETTY_YCONFIG_KEY_TELNET_PORT, 23);
            transport = yetty_ytransport_lwip_transport_create(host, (uint16_t)port);
            yinfo("webasm: pty = telnet-over-lwip (%s:%d via relay)", host, port);
        } else {
            const char *url = config->ops->get_string(config, YETTY_YCONFIG_KEY_WEBSOCKET_URL,
                                                      YETTY_WEBSOCKET_TELNET_DEFAULT_URL);
            transport = yetty_ytransport_websocket_transport_create(url);
            yinfo("webasm: pty = telnet-over-websocket (%s)", url);
        }
        if (!transport) {
            return YETTY_ERR(yetty_yplatform_pty_ptr, "webasm factory: telnet transport create "
                                                      "failed");
        }
        return yetty_ytelnet_telnet_pty_create(transport);
    }

    /* --ssh: libssh2 (non-blocking, mbedTLS) terminates the crypto in
     * the browser; the transport carries the *encrypted* stream over
     * either the lwIP netstack (real sshd:port via the relay) or a
     * ws↔TCP bridge. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_SSH, 0)) {
        struct yetty_ytransport_conn_transport *transport;
        if (via_relay) {
            const char *host = config->ops->get_string(config, "ssh/host", "127.0.0.1");
            int port = config->ops->get_int(config, "ssh/port", 22);
            transport = yetty_ytransport_lwip_transport_create(host, (uint16_t)port);
            yinfo("webasm: pty = ssh-over-lwip (%s:%d via relay)", host, port);
        } else {
            const char *url = config->ops->get_string(config, YETTY_YCONFIG_KEY_WEBSOCKET_URL,
                                                      YETTY_WEBSOCKET_SSH_DEFAULT_URL);
            transport = yetty_ytransport_websocket_transport_create(url);
            yinfo("webasm: pty = ssh-over-websocket (%s)", url);
        }
        if (!transport) {
            return YETTY_ERR(yetty_yplatform_pty_ptr,
                             "webasm factory: ssh transport create failed");
        }
        /* ssh_websocket_pty_create takes ownership of the transport. */
        return yetty_yssh_ssh_websocket_pty_create(config, transport);
    }

    /* --websocket: raw shell bytes to a websocket PTY server. */
    if (config && config->ops->get_bool(config, YETTY_YCONFIG_KEY_WEBSOCKET, 0)) {
        const char *url = config->ops->get_string(config, YETTY_YCONFIG_KEY_WEBSOCKET_URL,
                                                  YETTY_WEBSOCKET_PTY_DEFAULT_URL);
        struct yetty_ytransport_conn_transport *transport =
            yetty_ytransport_websocket_transport_create(url);
        if (!transport) {
            return YETTY_ERR(yetty_yplatform_pty_ptr,
                             "webasm factory: websocket_transport_create failed");
        }
        yinfo("webasm: pty = websocket-pty (%s)", url);
        /* websocket_pty_create takes ownership of the transport. */
        return yetty_ypty_websocket_pty_create(transport);
    }

    /* --temu, and the default when no session mode was given — the
     * in-iframe VM is the only mode that works with no server side. */
    return factory_create_temu_pty(factory);
}

struct yetty_yplatform_pty_factory_ptr_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific)
{
    (void)os_specific;

    struct webasm_pty_factory *factory = calloc(1, sizeof(*factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory_ptr,
                         "failed to allocate webasm pty factory");
    }
    static const struct yetty_yplatform_pty_factory_ops factory_ops = {
        .destroy = factory_destroy,
        .create_pty = factory_create_pty,
    };
    factory->base.ops = &factory_ops;
    factory->config = config;
    yinfo("webasm: pty factory ready (temu | websocket | telnet-over-websocket)");
    return YETTY_OK(yetty_yplatform_pty_factory_ptr, &factory->base);
}
