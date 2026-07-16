# ywasmnet — in-browser userspace TCP/IP stack (lwIP over an L2 relay)

`ywasmnet` gives the webasm build real TCP connectivity with no VM: lwIP runs
in `NO_SYS` mode inside the wasm process, and its "wire" is a single
WebSocket to an L2 frame relay (jor1k/jslinux-style — wsnic, libslirp, or
a public relay) where each binary message is one raw ethernet frame. lwIP
does ARP, DHCP, DNS and TCP on top; the relay bridges the frames onto a
real network with NAT egress. The consumer is
[`ytransport`](../ytransport/README.md)'s `lwip-transport`, which dials
outbound TCP connections over this stack for telnet and ssh sessions.

```
telnet-pty / ssh-websocket-pty
   └ lwip-transport (conn_transport)
        └ lwIP tcp_pcb  (DNS resolve, tcp_connect)
             └ IP / TCP / ARP        ← this module
                  └ ethernet frames ⇄ relay WebSocket
                       └ relay (wsnic / libslirp / public)
```

## How it works

- `yetty_ywasmnet_netstack_create(relay_url)` runs `lwip_init()`, adds an
  ethernet `netif` with a locally-administered random MAC, opens the relay
  WebSocket, and starts a 50 ms `emscripten_set_interval` tick that drives
  `sys_check_timeouts()`. It returns immediately.
- WebSocket `onopen` → `netif_set_link_up` + `dhcp_start`; each inbound
  binary message becomes a `pbuf` fed to `netif->input`
  (`ethernet_input`); `netif->linkoutput` flattens the pbuf chain and
  sends one binary message (frames capped at 1600 bytes).
- The netif status callback logs the DHCP lease (ip/gw/mask/dns) when it
  binds; `yetty_ywasmnet_netstack_is_ready()` reports the bound state.
- Everything runs on the browser main thread (single-threaded build), so
  lwIP's raw API is never entered re-entrantly and no locking exists.
- One netstack per wasm instance — lwIP itself is a global singleton, and
  `lwip-transport` reaches the stack through lwIP globals
  (`netif_default`, `tcp_*`, `dns_*`) rather than through this handle.

## Public API

```c
YETTY_YRESULT_DECLARE(yetty_ywasmnet_netstack_ptr, struct yetty_ywasmnet_netstack *);

struct yetty_ywasmnet_netstack_ptr_result yetty_ywasmnet_netstack_create(const char *relay_url);
int  yetty_ywasmnet_netstack_is_ready(const struct yetty_ywasmnet_netstack *netstack);
void yetty_ywasmnet_netstack_destroy(struct yetty_ywasmnet_netstack *netstack);
```

## File map

| file | role |
|---|---|
| `netstack.c` / `netstack.h` | netif glue, relay WebSocket callbacks, DHCP bring-up, timer tick |
| `lwip-port.c` | `sys_now()` (ms clock) and `lwip_port_rand()` from emscripten intrinsics |
| `lwip-port/lwipopts.h` | lwIP feature configuration for the `NO_SYS` build |
| `lwip-port/arch/cc.h` | compiler/arch shims for the port |

There is no `CMakeLists.txt` here: the sources are compiled directly into
the webasm yetty target and lwIP itself is a FetchContent build — both
wired in `build-tools/yetty/platform/webasm/cmake.cmake` and
`build-tools/yetty/libs/lwip-webasm.cmake`. The module builds on no other
platform.

## Status

`yetty_ywasmnet_netstack_create` currently has **no in-tree caller**. The
bring-up call lived in the deleted `yinit/webasm.c` bootstrap (replaced by
the yplatform app-injection entry, `../yplatform/ymain/webasm.c`) and was
not re-homed. The `--net-relay <ws-url>` flag is still parsed
(`net/relay` config key), and `../yplatform/pty-factory/webasm.c` still
selects lwip transports when it is set — but until a call site recreates
the netstack, those transports wait forever for DHCP readiness. The rest
of the stack is complete and was previously exercised end to end.

## Cross-references

- [`../ytransport/README.md`](../ytransport/README.md) — the `lwip-transport` consumer.
- [`../yssh/README.md`](../yssh/README.md), [`../ytelnet/README.md`](../ytelnet/README.md) — the protocol PTYs that ride it.
- [`../yplatform/README.md`](../yplatform/README.md) — webasm PTY factory and platform bootstrap.
