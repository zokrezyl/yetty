# ydvnc — desktop VNC client as a yui view

`ydvnc` is an RFB 3.8 (RFC 6143) client that renders a remote desktop into
a yetty pane: the framebuffer lives in a WGPU texture and is drawn as a
fullscreen quad inside the tile, and keyboard/mouse events are translated
to RFB input messages. It is consumed by [`yui`](../yui/README.md)
(`workspace.c` / `tile.c` mount it via `--ydvnc-client=HOST[:PORT]`) and
depends on `ycore`, the [`yevent`](../yevent/README.md) TCP client API and
`webgpu`. It is the opposite direction from
[`yvnc`](../yvnc/README.md), which makes *yetty itself* a VNC **server**
— the distinct `ydvnc`/`yvnc` config prefixes exist so the flags can't be
confused.

## How it works

- **Protocol** (`rfb-client.c`): a receive-buffer state machine —
  `PROTO_VERSION → SECURITY_TYPES → (SECURITY_REASON | AUTH_RESULT) →
  SERVER_INIT → MAIN`. After ServerInit it sends SetEncodings,
  SetPixelFormat (32-bit) and the first FramebufferUpdateRequest, then
  processes FramebufferUpdate / Bell / ServerCutText messages.
- **Security**: type None, or legacy VNC authentication (type 2) when a
  password was set — the 16-byte challenge is encrypted with DES-ECB using
  the bit-reversed, 8-byte-truncated password as key (`vnc-auth.c`,
  `des.c`). Note the header comment in `rfb-client.c` still claims
  "None only"; the code negotiates both.
- **Encodings**: Raw is fully decoded; CopyRect is advertised and parsed
  but not blitted (WebGPU has no synchronous texture read and no CPU
  shadow buffer is kept — the code logs a warning and waits for the
  server's Raw redraw of that region). The DesktopSize pseudo-encoding
  resizes framebuffer + texture. Tight/ZRLE/RRE/Hextile constants exist
  in `rfb-protocol.h` but are not implemented — a server that sends them
  disconnects with "unsupported encoding".
- **Transport** (`transport.h`, `transport-tcp.c`): a small vtable
  (`connect / send / disconnect / destroy`) over the event loop's TCP
  client, isolated so SSH-tunnel or Unix-socket transports can slot in
  under `rfb-client.c` later. Today only TCP exists.
- **Rendering**: the client always requests 32-bpp little-endian BGRA so
  Raw rect bytes upload straight into a `BGRA8Unorm` texture with
  `wgpuQueueWriteTexture` (per rect, no CPU shadow copy), and
  `yetty_ydvnc_rfb_client_render()` samples the texture as a quad on the
  caller's render pass (alpha forced opaque — RFB's 4th byte is padding).
- **Input** (`ydvnc-viewer.c`, `keysyms.c`): the viewer keeps the latched
  RFB button mask, maps pane-relative pointer coordinates into
  framebuffer-pixel space, and translates keys layout-correctly: modifier
  and non-printable keys from GLFW keycodes, printable characters from
  CHAR events (`keysym = codepoint` for ASCII, X.org Unicode keysyms
  otherwise) — so a Dvorak user's RFB KeyEvents carry the intended glyphs.

## Public API sketch

```c
/* include/yetty/ydvnc/ydvnc-viewer.h — password may be NULL (None-auth only). */
struct yetty_ydvnc_viewer_ptr_result yetty_ydvnc_viewer_create(
    const char *host, uint16_t port, const char *password,
    const struct yetty_context *yetty_ctx);
struct yetty_ycore_void_result yetty_ydvnc_viewer_destroy(struct yetty_ydvnc_viewer *viewer);
struct yetty_yui_view *yetty_ydvnc_viewer_as_view(struct yetty_ydvnc_viewer *viewer);
```

The viewer embeds a `yetty_yui_view` as its first member and fills the
view ops (`destroy` / `render` / `set_bounds` / `on_event`); yui pushes it
onto a tile like any other view (see
[`../yui-core/README.md`](../yui-core/README.md)). The internal
`rfb-client.h` API (connect/disconnect, frame/connected/disconnected
callbacks, `send_pointer` / `send_key`) stays private to the module.

## File map

| file | role |
|---|---|
| `ydvnc-viewer.c` | yui view wrapper: view ops, input translation, repaint requests |
| `rfb-client.{c,h}` | protocol state machine, framebuffer, GPU upload + quad render |
| `rfb-protocol.h` | wire structs, message types, encoding ids (RFC 6143) |
| `transport.h` / `transport-tcp.c` | byte-stream vtable + TCP implementation |
| `vnc-auth.{c,h}` / `des.{c,h}` | legacy VNC auth challenge/response |
| `keysyms.{c,h}` | GLFW keycodes / Unicode codepoints → X11 keysyms |
| `CMakeLists.txt` | `yetty_ydvnc` static lib, gated on `YETTY_ENABLE_FEATURE_YDVNC` |

## Configuration

`--ydvnc-client=HOST[:PORT]` (port defaults to 5900 at the yui call site)
and `--ydvnc-password=PASSWORD` (falls back to the `YDVNC_PASSWORD`
environment variable). The flags land in yconfig as `vnc/desktop-client`
and `vnc/ydvnc-password`.
