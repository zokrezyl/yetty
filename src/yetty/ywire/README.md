# ywire — the wire state machine and its ANSI / OSC / DCS codes

`ywire` is the envelope framer, decode stack, and dispatcher that sits between
the PTY byte stream and everything that consumes it: raw text goes to the text
grid, vendor envelopes go to registered handlers. The module is small —
`wire-statemachine.c` (framer + dispatcher), `channel.c` and `connection.c`
(handler channels and per-connection state), with public headers in
`include/yetty/ywire/`.

The bulk of this README catalogs the escape sequences yetty's wire protocol
defines on top of a normal terminal byte stream — the **vendor OSC and DCS
codes** that carry rich content (plots, images, GUIs), client-input events, and
the yclass RPC channel between a child process and yetty.

It is split into two worlds:

- **Standard ANSI / xterm sequences** (cursor moves, SGR colour, alt-screen,
  titles, …) are parsed by **libvterm** inside the text grid. yetty does not
  reimplement them; a few that drive yetty-side behaviour are noted in
  [§5](#5-standard-sequences-that-drive-yetty).
- **yetty vendor envelopes** (everything `6xxxxx` / `7xxxxx` / `8xxxxx`) are
  framed and dispatched by the **wire state machine**
  (`wire-statemachine.c`). They are the subject of this catalog.

The single source of truth for the numbers is three leaf headers — keep this
doc in sync with them:

- `include/yetty/yterminal/dcs-codes.h` — DCS payload codes
- `include/yetty/yterminal/osc-codes.h` — OSC namespace note
- `include/yetty/yterminal/client-input.h` — OSC client-input codes + structs

---

## 1. The two envelope kinds

Both envelopes are conformant ECMA-48 control strings, so a pass-through
multiplexer (tmux with `allow-passthrough on`) forwards them untouched.

```
OSC:  ESC ]  <decimal-code> ; <b64-args> ; <b64-payload>  (BEL | ESC \)
DCS:  ESC P  <decimal-code> y <b64-args> ; <b64-payload>  ESC \
```

They differ only in how the code is separated from the body:

- **OSC** puts a `;` after the code — `ESC ] <number> ; …` is already a valid
  OSC string.
- **DCS** puts a single ECMA-48 *final byte* after the code instead of `;`.
  yetty's final byte is **`y`** (`YETTY_YWIRE_DCS_FINAL`), chosen from the
  `0x40`–`0x7E` final range and deliberately **not** one a standard DCS command
  claims (`q`=Sixel, `p`=ReGIS, `|`=DECUDK, `$q`=DECRQSS, `+q`=XTGETTCAP). A
  strict DCS parser frames the rest (`<b64-args> ; <b64-payload>`) as the
  opaque DCS data string and passes it through verbatim.

### Why OSC vs DCS

| Use the envelope… | …for |
|---|---|
| **OSC** | short control / metadata messages — client-input events (mouse / resize / focus / key / subscribe) |
| **DCS** | bulk drawing / figure payloads — large opaque blobs, exactly what DCS is for (cf. Sixel, ReGIS) |

### Codec & framing

- `<b64-args>` — an optional fixed-shape args struct, base64 only. Present iff
  the handler was registered with `has_args = 1` (e.g. yface envelopes); absent
  for bare codes (e.g. yrpc).
- `<b64-payload>` — the body: **base64 over an LZ4F frame** when emitted
  compressed; base64-only otherwise. Handlers read already-decoded bytes via
  `yetty_ywire_wire_statemachine_read`; they never see b64/lz4.
- Under tmux, emit paths auto-wrap envelopes in the `ESC P tmux; … ESC \`
  passthrough wrapper (see `yetty_ywire_tmux_passthrough_active`).

Full framing rationale lives in `include/yetty/ywire/wire-statemachine.h`.

---

## 2. Code numbering scheme

The decimal code carries both a direction and a namespace:

| Range | Direction | Meaning |
|---|---|---|
| `6xxxxx` | client → server | frontend / ygui / yrich emit toward yetty |
| `7xxxxx` | server → client | yetty delivers events back to the child process |
| `8xxxxx` | bidirectional   | yclass RPC control channel (one code, both directions) |

DCS carries the `6xxxxx`/`8xxxxx` bulk + RPC traffic; OSC carries the
`6xxxxx` subscribe and `7xxxxx` client-input deliveries.

---

## 3. DCS codes

`include/yetty/yterminal/dcs-codes.h`. All are framed `ESC P <code> y … ESC \`.

| Code | Name | Args | Direction | Handler | Purpose |
|---|---|---|---|---|---|
| `600000` | `YETTY_DCS_YDRAW_CLEAR` | — | client → server | ydraw canvas | Clear the ydraw canvas (empty body). |
| `600001` | `YETTY_DCS_YDRAW_BIN` | `yetty_yface_bin_meta` | client → server | ydraw canvas | Binary ydraw primitive batch (flat list). Used by ypdf, `ycat` svg, … |
| `600002` | `YETTY_DCS_YDRAW_YAML` | — | client → server | (defined; not wired in the terminal today) | YAML-text ydraw payload. |
| `600003` | `YETTY_DCS_YDRAW_OVERLAY` | `yetty_yface_bin_meta` | client → server | ydraw canvas | Overlay variant of the binary batch. |
| `600004` | `YETTY_DCS_YDRAW_SCENE_BIN` | `yetty_yface_bin_meta` | client → server | scene-canvas | Entity-scoped (ygui) batch — scene-canvas with GROUP/DELETE incremental updates, so producers re-emit only dirty widgets. |
| `630000` | `YETTY_DCS_YCOMPOSITOR_BIN` | yes | client → server | root `yfigure` container | Positioned-figure compositor wire. yface binary, framed FAM records (`u32 type | u32 payload_size | payload`); records decode into a yfigure tree. Carries ymgui / yrdawn / ygrid figures (CREATE_CHILD / GROUP / DELETE). |
| `800000` | `YETTY_DCS_YCLASS_RPC` | no | bidirectional | yclass RPC server | One code, both directions of a yrpc session: client→server carries request frames, server→client carries response frames. Proxies ygui objects over the wire. |

**Registered where:** the content layer registers `CLEAR` / `BIN` / `OVERLAY`
for its ydraw canvas and the text grid as the raw default sink
(`content-layer.c::yetty_yvterm_content_layer_register_wire`); the terminal
registers `YCOMPOSITOR_BIN` for the root container and attaches the RPC server
on `YCLASS_RPC` (`terminal.c::yetty_yterminal_terminal_create`).

> `600002` (YAML) and `600004` (SCENE_BIN) are defined wire codes but are not
> registered by the terminal in the current build — they are reserved for the
> producers that target them.

---

## 4. OSC codes (client-input channel)

`include/yetty/yterminal/client-input.h`. Short control envelopes framed
`ESC ] <code> ; … (BEL | ST)`. The terminal delivers input events to a running
child; the child subscribes for the pane-wide variants.

### 4.1 Client → server

| Code | Name | Payload | Purpose |
|---|---|---|---|
| `610010` | `YETTY_OSC_CS_CLIENT_INPUT_SUB` | `yetty_client_input_sub` | Subscribe / update / unsubscribe pane-wide input forwarding. `flags` is a bitmask (below); `flags = 0` unsubscribes from all. |

### 4.2 Server → client — figure-tagged (`figure_id != 0`, card-local pixels)

Routed to one ymgui card by the compositor's hit table.

| Code | Name | Payload |
|---|---|---|
| `700000` | `YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE`  | `yetty_client_input_mouse` |
| `700001` | `YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE` | `yetty_client_input_resize` |
| `700002` | `YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS`  | `yetty_client_input_focus` |
| `700003` | `YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY`    | `yetty_client_input_key` |

### 4.3 Server → client — pane-wide (`figure_id == 0`, pane-local pixels)

Delivered only to subscribers of `610010`. Used by non-ymgui programs that want
raw input (browser, file manager, ymesh, yjungle, ymaze, yzoo, ylexbor,
ynetsurf, …).

| Code | Name | Payload |
|---|---|---|
| `700010` | `YETTY_OSC_SC_CLIENT_INPUT_MOUSE`  | `yetty_client_input_mouse` |
| `700011` | `YETTY_OSC_SC_CLIENT_INPUT_RESIZE` | `yetty_client_input_resize` |
| `700012` | `YETTY_OSC_SC_CLIENT_INPUT_KEY`    | `yetty_client_input_key` |

Both delivery modes carry the same on-wire structs; only `figure_id` and the
coordinate space differ. Each payload begins with a magic word
(`…_MOUSE_MAGIC`, etc.) and `version` (`YMGUI_WIRE_VERSION`).

### 4.4 Pane-wide subscription bitmask (`yetty_client_input_sub.flags`)

| Bit | Name | Forwards |
|---|---|---|
| `1<<0` | `YETTY_CLIENT_INPUT_SUB_MOUSE_CLICK` | mouse button transitions |
| `1<<1` | `YETTY_CLIENT_INPUT_SUB_MOUSE_MOVE`  | mouse position / drag |
| `1<<2` | `YETTY_CLIENT_INPUT_SUB_MOUSE_WHEEL` | wheel notches |
| `1<<3` | `YETTY_CLIENT_INPUT_SUB_KEY`         | key down / up / char |

---

## 5. Standard sequences that drive yetty

These are ordinary terminal escapes parsed by libvterm; yetty reacts to a few
through libvterm's `settermprop` callback
(`text-layer.c::on_settermprop`). They are listed here because they change
yetty-side state, but they are **not** yetty extensions.

| Sequence | libvterm prop | yetty reaction |
|---|---|---|
| `CSI ?1049 h/l`, `?1047`, `?47` | `VTERM_PROP_ALTSCREEN` | Alt-screen toggle — the content layer swaps the ydraw canvas to/from its saved half. See [Layered Rendering](../../../docs/layered-rendering.md#alt-screen). |
| `CSI ?25 h/l` (DECTCEM)         | `VTERM_PROP_CURSORVISIBLE` | Show/hide the cursor in the GPU uniform. |
| `CSI <n> SP q` (DECSCUSR)       | `VTERM_PROP_CURSORSHAPE` | Cursor shape: 1=block, 2=underline, 3=bar. |
| `CSI ?1500 h/l` (card click)    | `VTERM_PROP_CARDCLICK` | Gate whether GLFW mouse-button events are forwarded to figures as `700000`/`700010`. |
| `CSI ?1501 h/l` (card move)     | `VTERM_PROP_CARDMOVE`  | Gate mouse-move forwarding. |
| `CSI ?1502 h/l` (card key)      | (keyboard subscription) | Gate key forwarding for figure-tagged delivery, parallel to the `?1500`/`?1501` mouse model. |

`?1500`/`?1501`/`?1502` are yetty-private DEC modes carried by the yetty
libvterm fork; the terminal also exposes a tmux-wrapped raw-sequence helper
(`yetty_ywire_tmux_wrap`) so they survive a multiplexer. Standard OSC titles
(`OSC 0/2`), clipboard (`OSC 52`), cwd (`OSC 7`), and hyperlinks (`OSC 8`) are
handled inside libvterm and are not part of yetty's vendor namespace.

---

## 6. Pointers

- Envelope framer + dispatcher: `include/yetty/ywire/wire-statemachine.h`,
  `src/yetty/ywire/wire-statemachine.c`
- DCS code definitions: `include/yetty/yterminal/dcs-codes.h`
- OSC namespace note: `include/yetty/yterminal/osc-codes.h`
- Client-input OSC codes + payload structs:
  `include/yetty/yterminal/client-input.h`
- DCS registration sites: `src/yetty/yvterm/content-layer.c`
  (`…_register_wire`), `src/yetty/yterminal/terminal.c` (compositor + RPC)
- ydraw payload decode: `src/yetty/yvterm/ydraw-content.c`
- Standard-sequence reactions: `src/yetty/yvterm/text-layer.c::on_settermprop`
- The layer / figure dynamic model these codes feed:
  [Layered Rendering](../../../docs/layered-rendering.md)
- The semantic emit layer used by producers: [yface](../yface/README.md)
