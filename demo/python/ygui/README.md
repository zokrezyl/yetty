# ygui from Python

Native [ygui](../../../src/yetty/ygui) applications written entirely in Python,
rendered inside a running yetty. No new C in the apps — they drive the real
widget toolkit through the `libyetty_ffi.so` bindings.

Two demos, sharing `ygui_ffi.py` (the FFI plumbing):

| Demo | What it is |
|------|------------|
| `ygui_demo.py` | A standalone ygui app: a widget panel you interact with by mouse. |
| `ygui_term.py` | A **terminal multiplexer** with a ygui overlay: keystrokes go to a real shell, the mouse drives the GUI. |

```sh
# inside a yetty pane:
uv run demo/python/ygui/ygui_demo.py          # standalone widget app (q quits)
uv run demo/python/ygui/ygui_term.py          # shell + ygui overlay (Ctrl-] quits)
uv run demo/python/ygui/ygui_term.py htop     # multiplex a specific command
```

## Requirements

- Run it **inside a yetty pane** (so stdout / `/dev/tty` is the yetty PTY).
- The FFI shared library must be built:
  ```sh
  make build-desktop-ffi-release
  ```
  Override its location with `YETTY_FFI_LIB=/path/to/libyetty_ffi.so` if needed.

## What they show

`ygui_demo.py` builds a small widget tree — heading label, button, checkbox,
toggle, slider — and ships it to yetty's compositor. The widgets float over the
scroll buffer and react to the mouse.

`ygui_term.py` forks `$SHELL` (or the command you pass) under a PTY and sits in
the middle: you type into the shell as normal, its output passes through to the
screen, and a ygui toolbar floats on top. Clicking the toolbar updates its click
counter — proving the OSC→GUI→Python path. Quit with `Ctrl-]`; the shell still
gets `Ctrl-C` and friends.

```
   user keys ─┐                                  ┌─▶ shell stdin (PTY master)
              │  yetty_yface demux on our stdin: │
  stdin ──────┤    raw bytes ────────────────────┘
              │    OSC mouse envelopes ─▶ ygui framework ─▶ widgets (our stdout)
              └
  shell stdout (PTY master) ───────────────────────▶ our stdout (passes through)
```

That split — **any text to the terminal, OSC/DCS to the GUI** — is the whole
point: `yetty_yface_feed_bytes()` scans our stdin and routes envelopes to the
mouse handler and everything else to the shell.

### Keeping the shell typeable after a click

yetty uses a click-focus model: clicking a compositor figure (one of the
overlay widgets) gives it keyboard focus, after which yetty delivers keystrokes
to that figure as `CLIENT_INPUT_KEY` OSC envelopes **instead of** to the pane's
shell. A naive multiplexer "freezes" the shell the moment you click the overlay.
`ygui_term.py` handles those key envelopes too — it decodes them back to
terminal bytes (`key_event_to_bytes`) and forwards them to the shell — so typing
keeps working whether or not the overlay holds focus.

## How it works

```
Python ──ygui framework──▶ drawable records ──PTY──▶ yetty renders the widgets
   ▲                                                        │
   └── yetty_yface demux ◀── OSC mouse envelopes ◀── PTY ◀──┘
```

- **Output.** `yetty_yffi_fd_pty_create(fd)` (a small helper in the FFI lib)
  gives the ygui framework a `platform_pty` that writes to our stdout. Each
  `framework.emit()` lays out the tree, paints it into drawable records, and
  writes the compositor `YCOMPOSITOR_BIN` envelope down the PTY. This is the
  exact path the C tools `yless` / `yflame` / `ygreeter` use.
- **Input.** We subscribe to pane-wide mouse events; yetty forwards them as OSC
  envelopes on our stdin. `yetty_yface_feed_bytes()` scans the stream and
  splits it — OSC envelopes go to the mouse handler
  (`framework.feed_mouse_*`), raw bytes go to the keyboard handler
  (`framework.feed_input`). That split is the "client-side multiplexer":
  ordinary characters to the app, OSC sequences to the GUI.

## Bindings used

- Generated yclass result types from `bindings/python/yetty/generated/_types.py`
  (reused for every `*_ptr_result`).
- The ygui engine API (`yetty_ygui_framework_*`, `yetty_ygui_add`,
  `yetty_ygui_<widget>_set_label`, …) and `yetty_yface_*`, declared on demand
  via `yetty.runtime.cfn`. These are the engine entry points, not yclass
  methods, so they aren't in the generated per-class modules — the demo binds
  them directly against the same shared library.
