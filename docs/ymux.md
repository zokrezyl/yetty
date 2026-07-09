# ymux — tmux-style server/client for yetty

**Status:** Draft, revised — core model is a **two-process server/client**, like
tmux · **Scope:** design only, no implementation yet

> "ymux" is the **feature name** (yetty's tmux-analog). The mechanism is a
> **persistent headless server** that holds the sessions, and one or more
> **clients** (a window/GPU frontend = a render target) that attach and detach.

> Line/symbol references were captured during a code investigation and reflect the
> tree at that time; verify against current source. They are anchors, not
> contracts.

---

## 1. The model, in one paragraph

Starting `yetty` for the first time — **exactly like tmux — starts a server AND a
client.** The **server** is a persistent, **headless, GPU-less** yetty core: it owns
the event loop, the PTYs, and the terminal models (`yvterm:grid`), and it survives
independently of any window. The **client** is the frontend: a window + GPU + render
target that attaches to the server over a local socket, shows the session, and drives
input. **Detach** tears down the *client* (the frontend — window/GPU/render target) —
the **server keeps running headless, no GPU**, pumping PTYs and holding all state.
**Attach** binds a new client — which owns the window/GPU/render target, **local or
remote** — and the session resumes. (The client owns the render target; the server
owns the model; ymux connects them with a model stream.)

Two processes, always, from the first launch. This is the tmux topology; the yetty
twist is that the client is a full GPU frontend, so the session's rich content
(plots, images, GUIs) renders with a real render target rather than as terminal text.

---

## 2. Roles, not platform locks: server, client (local **or remote**), legacy

The single most important framing: ymux splits into **roles that are separable over a
network**, and the capability that varies by platform is **hosting a server** — never
**being a client.**

- **ymux server** — persistent, headless, GPU-less; owns the PTYs/sessions. Runs
  wherever you can daemonize + listen on a socket: **desktop and remote/cloud boxes.** A
  fork-less platform can't *host a local* server.
- **ymux client** — a window/GPU frontend that **connects to a server**, gets the model,
  renders natively, sends input. The connection is a socket that is **UNIX-domain for a
  local server or TCP/WebSocket for a remote one.** A client runs on **any networked
  platform — desktop, Android, iOS, WebASM.**
- **Legacy (standalone monolithic)** — one process, window+PTY+GPU+renderer, **no
  detach**: the fallback when you are **not connecting to any server** (and the desktop
  opt-out). Runs everywhere.

**A fork-less mobile device is a first-class ymux client.** An Android/iOS yetty
connects to a **remote** server on a desktop/cloud box, renders the session on **its own
GPU**, and detaches/reattaches — this *is* the "connect my Android to my remote session,
or vice-versa" use case. The only thing a phone can't do is *run the server locally*,
and it doesn't need to. Legacy mode is for the standalone case (no server at all),
**not** a cage that fork-less platforms are stuck in.

**All three roles share one logical-terminal core — `ymux_pane` (§5.2).** The server
holds it bare; legacy wraps it with an in-process renderer + view; the client has only
the renderer + a sink grid (no `ymux_pane` — the truth is on the server). One terminal
implementation, so Phase 1a's extraction (§10) is the foundation for **all** of it.

Why the server/client split earns its keep: detach is trivial and robust — the server
was never windowed, so "detach" = the client process/connection drops (no in-process
window/GPU teardown, no window-optional main loop; a client crash or a dropped network
link can't take the shells down). The cost is a server↔client wire on every session
(local or remote, like tmux) — not exotic, and absent in legacy mode.

---

## 3. Goals / Non-goals

### Goals

- **First launch on a host that can run a server = server + client** (tmux model).
  Subsequent `yetty`/`yetty attach` reuse it.
- **A client attaches to a local OR a remote server.** Fork-less/mobile platforms
  (Android/iOS/WebASM) are **first-class remote clients** — they connect to a server on
  a desktop/cloud box and render natively. Hosting a *local* server is the only
  platform-gated capability.
- **Legacy (standalone monolithic) mode is preserved** — the no-server fallback and
  desktop opt-out. All roles share the `ymux_pane` core (§2, §5.2), so there is no
  second terminal implementation.
- **Detach any time:** the client exits; the server persists headless, no GPU, with
  shells/panes/scrollback/figures alive.
- **Attach any time:** a new client (local or remote render target) binds and resumes.
- **Server is headless and GPU-less.** The GPU lives with the client (render target).
- **The client owns the render target; the server owns the model** — ymux connects
  them with a model stream. Local and remote clients differ only in location.

### Non-goals (v1)

- **Remote is not a scope cut — it's a transport stage.** The client↔server wire is the
  same whether the socket is local (UNIX) or remote (TCP/WebSocket); the local transport
  is built first (simplest to validate), the remote transport immediately after (§10
  Phase 3). Remote/mobile clients are a **v1 goal**, not someday.
- **Multiple concurrent clients.** One attached client in v1 (§5.5).
- **Named multi-session.** One server == one session tree (its whole tab tree).
  Deferred.
- **`ymux_pane`/render-target as yclass.** `ymux_pane` starts plain C (§5.2); the
  render-target→yclass conversion is deferred (§12).

---

## 4. Terminology

| Term | Meaning |
|---|---|
| **legacy (standalone monolithic)** | Single-process yetty — window + PTY + GPU, no detach. The no-server fallback / desktop opt-out. Runs everywhere. |
| **server** | Persistent, headless, GPU-less host of PTYs/sessions (event loop + `yvterm:grid` + layout). Runs where you can daemonize + listen (desktop / remote box); **not hostable on fork-less mobile**. Survives every client. |
| **client** | Window/GPU frontend. Connects to a server over a **local (UNIX) or remote (TCP/WebSocket)** socket, renders the session, sends input. Runs on **any networked platform incl. Android/iOS/WebASM**. Ephemeral. |
| **render target** | The client's rendering surface (`struct yetty_ydraw_target`). "Detach the frontend" = tear down the client/render target. Local or remote. |
| **`ymux_pane`** | The server-side logical-terminal owner: `{pty, wire statemachine, yvterm:grid, terminal-level state}` — no view, no GPU, no renderer figure. Plain C in v1. |
| **grid model** | `class@yvterm:grid` — libvterm + tiered scrollback + retained row-anchored envelopes. The authoritative content, held by the server. |

---

## 5. Design

### 5.1 The server (headless, GPU-less core)

Launched as a headless yetty (`yetty --ymux-server NAME`, spawned by the first client
if none exists). It brings up **only** the logical stack — no window, no WebGPU
device, no render target:

- Reuse the GPU-free path: `yetty_yframework_create` (`yframework.c` ~633) must be
  able to come up **without** a device (today it hard-errors with no adapter/device
  at ~294/406 and creates the render target once at ~600-621) — gate `init_gpu` and
  render-target creation behind a `--server`/no-gpu flag, or provide a minimal server
  context that builds only `config` + `event_loop` (~690) + `memtag_registry` (~716)
  + `pty_factory`.
- The event loop (`libuv-event-loop/default.c`) already pumps PTYs/timers/TCP with no
  GPU; `request_render` is inert with no render listener (§ background). So the server
  loops, feeds PTY bytes into grid models, and holds state — no rendering.
- The server owns the **session/layout tree** and one `ymux_pane` per pane (§5.2). An
  **auto-spawned server starts empty** (no session) and listens; the **first client**
  defines the initial session via `attach_or_create` (§5.4) — this is what makes
  `yetty -e cmd` / initial cwd/env/profile work. A `--foreground` server may stay empty
  until first attach or create a default pane from server-side config.

### 5.2 `ymux_pane` — the logical-terminal owner (first code target, plain C)

Extract, from today's `terminal.c`, a GPU-free owner of one terminal:

```c
/* Server-side logical terminal. No view, no GPU, no renderer figure.
 * A later class@ymux:pane facade may wrap it for control addressing. */
struct yetty_ymux_pane {
    struct yetty_platform_pty            *pty;
    struct yetty_ywire_wire_statemachine *sm;
    struct yetty_yclass_object           *grid;   /* yvterm:grid — the truth */
    /* terminal-level state: content rect/inset, mouse/card subs, screen mode,
       positioned-figure scene log (§5.6) */
};
```

`yetty_yterminal_terminal_create` (`terminal.c` ~1855) currently welds renderer /
compositor concerns (composite factory, ydraw registry, figure registry, root
container) around the grid; the extraction pulls the model-only owner out so the
**server** can hold live terminals with no renderer. On the **client**, the existing
`yterminal`/`yvterm:vterm` renders a grid the client holds (§5.4). Phase 1a is a
**no-behavior-change** extraction (`terminal.c` still behaves identically locally);
plain C — no yclass in the core.

**Both modes use this owner.** In **legacy** mode `terminal.c` keeps a renderer figure
+ yui view over an in-process `ymux_pane`. In the **ymux server** the `ymux_pane` is
bare (no renderer). In the **ymux client** there is *no* `ymux_pane` at all — only the
renderer + a client-local sink grid (the pty/sm/grid truth lives on the server). One
terminal implementation, rendered locally or driven headless.

### 5.3 First-launch bootstrap (server + client)

> **Decide by target, not by platform.** This §5.3 flow is the **local** bootstrap:
> connect-or-spawn a server on *this* machine. It needs host server-hosting capability
> (fork/spawn + local IPC) — desktop, not fork-less mobile. Two other cases skip it:
> - **Remote server:** the client is pointed at a remote `host:port` (TCP/WebSocket) and
>   just **connects — no spawn** — so it works on **any** platform, including
>   Android/iOS/WebASM. This is the mobile→remote-session case (§2).
> - **Standalone:** no server target at all → **legacy monolithic mode** (in-process
>   `ymux_pane` + renderer, no detach). Also the desktop `--no-ymux` opt-out, and the
>   default for a fork-less device used purely on its own.

`yetty` with no running server for `NAME`:

1. Client starts and creates **enough local display state to know its cell/pixel
   geometry** (window + GPU + render-target shell), so `attach` can carry correct dims.
2. Resolve `$XDG_RUNTIME_DIR/yetty/<NAME>.sock`; try `connect()`. Success → attach
   (§5.4).
3. Fail → acquire a per-session **bootstrap lock** (`$XDG_RUNTIME_DIR/yetty/<NAME>.lock`,
   `flock`), then **retry `connect()`** — another launcher may have won the race while
   we waited on the lock.
4. Still absent → **spawn a child** (fork+exec / posix_spawn) running
   `yetty --ymux-server NAME` (detached, setsid). **Not** exec-in-place (that would
   replace the client, leaving nothing to attach); and never fork-without-exec after
   GPU/window/client state has initialized.
5. **Wait for readiness by a successful `connect()`** — not by the socket path
   existing, which can appear before `listen()` or be stale — with a **bounded
   deadline** (≈5s default, configurable). Release the lock; attach.
6. **Server side of the race:** if `bind()` collides (another server won), the server
   **exits cleanly** and the client reconnects to the winner. Stale-socket removal
   happens **only while holding the bootstrap lock**, and only after proving no server
   responds.

`yetty attach [NAME]` with a running server skips 3–5. **Server modes are distinct:**
`yetty --ymux-server NAME --foreground` (systemd/debug) vs. the detached/background
mode used by client auto-spawn. This is tmux's spawn-if-absent-then-attach flow with a
lock + bind-race safety net.

### 5.4 Attach / detach

**Attach is ordered — resize *before* snapshot, deltas *after*** (so the client never
ships a mid-resize model nor renders the old detached size), keyed on a generation /
base sequence:

1. Client (window/GPU shell already up, §5.3.1) sends
   `attach_or_create { name, cols, rows, pixel_size, cmd/cwd/env/profile }`.
2. Server creates the initial session **if it is empty**, and puts the client in a
   **pending** state — not yet receiving normal deltas.
3. Server applies the requested size as a **model mutation** (PTY resize + grid resize
   + a resize-state record), ordered on its loop.
4. Server stamps a **snapshot generation / base sequence** *after* the resize is
   ordered, serializes the model at that generation, and sends the snapshot.
5. Client shows a **blank/loading frame** until the snapshot arrives, then builds its
   yui tree + per-pane **sink grids** + `yvterm:vterm` renderers, applies the snapshot,
   and renders frame 1. (It does not build pane renderers before the snapshot — pane
   count/layout come from the server.)
6. Server enables `YMUX_PANE_DELTA` streaming **strictly after** that generation. A
   SIGWINCH repaint triggered by the resize simply arrives as normal deltas and
   converges — **no unbounded "drain until repaint done" wait.**

- **Detach:** the **client process exits** (clean socket close). The server drops that
  client's subscriptions and **keeps every PTY/grid alive** — no SHUTDOWN, no PTY reap.
  (Window-close/SIGHUP dispatch `YETTY_YCORE_SHUTDOWN` which reaps PTYs, `yetty.c`
  ~816-874 — but that lives in the *client*; the *server* never had a window, treats
  client-disconnect as "keep running," and survives SIGHUP.)
- **Server shutdown** is explicit (`shutdown-server` verb, or configurable last-pane
  exit). Killing a client never kills the server.
- **`yetty -e cmd` against an already-running session:** create a new window/session if
  named sessions exist, otherwise reject with an explanation — **never silently
  replace** the running session's command.

### 5.5 One client at a time (v1)

Exactly one attached client in v1; a second attach either replaces the first or is
rejected. Multi-client mirroring (and its resize/focus/mouse-mode arbitration) is a
later feature.

### 5.6 Rich content — capability tiers

Whatever the wire (§6), a figure's restore quality on (re)attach is a per-figure-class
capability declared at composite-factory registration:

1. **Replayable** — create + mutation envelopes reconstruct current state (ymgui-style
   widgets; row-anchored content, already envelope-retained in the grid).
2. **Snapshot-capable** — needs create + a latest per-figure snapshot + subsequent
   mutations (class exposes `snapshot`/`apply_snapshot`).
3. **Live-only** — attach recreates the shell; mid-session visual state best-effort
   until the producer redraws (yrdawn's live GPU canvas).

Positioned figures (ymgui/yrdawn) are created via yclass-RPC into `root_container_obj`
(`terminal.c` ~205-208), **not** the grid, so they need a **container-scoped
scene-replay log** in addition to the grid's row-anchored envelopes.

### 5.7 Threading & ownership

- **Server:** one libuv loop multiplexes PTYs + the control socket + data queues +
  timers, and **all grid mutation happens on that loop.** Any lower-level PTY reader
  threads post bytes into the loop rather than mutating grids directly.
- **Client:** the GLFW main thread owns native window events (as today); socket IO can
  live on the event-loop thread — but a **single owner** applies model frames to the
  sink grids and drives rendering. Pattern: the socket side decodes frames into owned
  buffers and posts them to the UI/render queue; the UI/render side applies deltas to
  the sink grids, then requests render. **No concurrent grid mutation/read without a
  formal handoff** — a single owner thread for the client sink model is the simpler v1.

---

## 6. The wire: model-shipping (decided)

The server ships the terminal **model**, not pixels. It stays **fully GPU-less**; per
pane it sends an initial snapshot then ordered deltas
(`YMUX_PANE_DELTA { pane_id, seq, base_seq, flags, records[…] }`) plus figure envelopes
(capability-tiered, §5.6). The **client renders natively** on its own GPU, to its own
local render target.

Why, not pixel-shipping:
- **Server truly GPU-less** — it can run on a headless/GPU-less box or over SSH; a
  pixel-rendering server would need a GPU while attached and kill that use case.
- **Cheap wire** — cells/deltas, not a 4K/60fps pixel stream; keeps a local client
  "super-fast" (tmux is cheap for exactly this reason).
- **Native rich rendering** — crisp plots/images/GUIs, local reflow, native scrollback
  on the client, not a video mirror.
- Reuses the `ymux_pane` / grid / tier machinery already designed.

**Reconciling "yetty renders to a render target / present → ymux":** the local native
client renders with its **own** render target (`present() → surface`) fed by the model
stream — so that phrasing holds **client-side**. The server-side `present() → ymux`
**pixel** path (`render-target.h:34,51`; the VNC target generalized) is **not dropped**
— it is repositioned as the **remote thin-client transport** (phone / browser /
GPU-less viewer), a second render-target flavor, **later** (Phase 5). v1 builds only
the model-shipping path.

---

## 7. Control + data over the socket

- **Control plane:** msgpack request/response + notifications over the UNIX socket
  (`ipc-socket` module, unused today; `rpc/socket-path` key; reuse the yctl handler
  registry): `attach_or_create`, `attach`, `detach`, `list`, `split`, `resize`,
  `new-window`, `kill-pane`, `input`, `shutdown-server`.
- **Data plane (model-shipping):** per-pane snapshot + ordered `YMUX_PANE_DELTA`
  frames + figure envelopes, server→client; input client→server. Bounded per-client
  queues, drop-to-resnapshot on overflow, and no head-of-line blocking of control
  frames (`detach`/`kill-pane`). The pixel-frame data plane belongs to the later
  remote thin-client transport (Phase 5).

---

## 8. Reuse vs net-new

| Concern | Exists (reuse) | Net-new |
|---|---|---|
| GPU-free terminal model | `yvterm:grid` | — |
| Headless server core | libuv loop (GPU-free), inert `request_render` | GPU-less `yframework`/server bring-up (no mandatory device) |
| Logical-terminal owner | (entangled in `terminal.c`) | extract plain-C `ymux_pane`; refactor `terminal.c` |
| First-launch spawn | `setsid`/exec spawn (`process/default.c`) | client spawns `--ymux-server` if socket absent |
| Local socket | `ipc-socket`, `rpc/socket-path` | UDS in the event loop (or `uv_poll`) |
| Control server | yctl framing + registry | attach/detach/list/…; server→client push |
| Detach ≠ shutdown | SHUTDOWN handling (client-side) | server treats disconnect as keep-alive; survive SIGHUP |
| `[A]` data plane | tier codec, retained envelopes, `materialize_fn` | whole-grid snapshot + `YMUX_PANE_DELTA`; client sink-grid ingest |
| `[B]` data plane | `render-target-vnc.c` (frame ship), multi-client slots | attachable `present→ymux` target; keyframe/resync |
| Rich content | row-anchored envelope retention | positioned-figure scene log; capability flags |

---

## 9. Risks

1. **Model-shipping replication completeness** (the main protocol risk) — deltas must
   carry *all* terminal-level state (cursor, screen mode, resize, content-rect,
   mouse/card subs, effects), not just dirty cells (`_is_dirty` is a repaint hint, not
   the contract); `RESET_TO_SNAPSHOT_REQUIRED` is the resync escape.
2. **Detach correctness on the server** — client disconnect must never reap PTYs; the
   server must survive SIGHUP. (Far simpler than the single-process design's
   window-optional loop, which no longer exists.)
3. **Server headless bring-up** — `yframework` must come up with no device (it
   hard-errors today at `yframework.c` ~294/406); validate the GPU-less path.
4. **Positioned/live figures** — capability tiers bound the promise; tier-2 needs
   per-class `snapshot`/`apply_snapshot`; per-figure-class audit required.
5. **Later (remote):** pixel bandwidth/latency (the yvnc lag) for the remote
   thin-client transport — a Phase 5 concern, not v1.

---

## 10. Phases

- **Phase 1a — extract `ymux_pane`** (plain C); `terminal.c` refactored, **identical
  behavior**. Clean bisect point. **This delivers legacy mode** — it keeps working
  unchanged on *every* platform, including fork-less ones. ymux mode (2a/2b) is added
  on top, gated to platforms that can fork/spawn + do local IPC.
- **Phase 1b — headless server bring-up.** GPU-less `yframework`/server context;
  `yetty --ymux-server NAME --foreground` pumps shells, observable via a debug "dump
  text" verb.
- **Phase 2a — manual attach/detach, one local client** (model-shipping wire). A
  client attaches to an **already-running** server, shows a text session, detaches,
  the server survives, reattach resumes. *Engineering MVP.*
- **Phase 2b — default first launch (user-facing MVP).** Plain `yetty` auto-spawns a
  server if none exists and attaches a client — tmux-style. The owner's "default like
  tmux" requirement is **not met until this ships**, so it is part of the MVP, not
  later polish.
- **Phase 3 — remote client transport (TCP/WebSocket).** The **same** model-shipping
  wire over a network socket instead of UNIX-domain → **Android/iOS/desktop clients
  attaching to a remote server** and rendering natively. The motivating
  mobile→remote-session case; a first-class v1 goal, not polish.
- **Phase 4 — layout / multi-pane.**
- **Phase 5 — rich content** by capability tier + positioned-figure scene log.
- **Phase 6 — thin-client (pixel) fallback** for clients that can't render natively
  (e.g. a browser without WebGPU), multi-client mirroring, named sessions,
  prefix/command keybindings.

The MVP has two milestones: **2a** (engineering — server holds the shells, manual
attach/detach) and **2b** (user-facing — first `yetty` behaves like tmux).

---

## 11. Resolved decisions

- **Topology:** two processes from first launch — a **headless GPU-less server** + a
  **client** (frontend/render target), like tmux. **Not** single-process. Detach =
  client exits, server survives; attach = new client (local/remote).
- **Server is headless-born** — no window-optional main loop, no in-process display
  teardown (those were single-process artifacts, now gone).
- **`ymux_pane`** extracted plain-C first; `terminal.c` refactor is no-behavior-change.
- **Detach ≠ shutdown:** server treats client disconnect as keep-alive; SIGHUP
  survivable; exits only on explicit verb.
- **One client, local, v1;** remote/multi-client later.
- **Rich content** via capability tiers; positioned figures need a scene-replay log.
- **render-target → yclass:** deferred (§12).

**Wire (decided, §6):** the server ships the **model**, GPU-less; the client renders
natively. The `present() → ymux` **pixel** path is the later **remote thin-client**
transport (Phase 5), not v1.

---

## 12. Out of scope / related

- **render-target → yclass** — deferred by decision; related (esp. under Option B,
  where an attachable `present→ymux` target is the seam) but its own isolated refactor.
- **Old C++ `tools/ymux`** (raw libvterm + `ygrid` OSC cards) is **retired**, not the
  base.
- **Remote clients**, **named multi-session**, **prefix/command keybindings** —
  follow-ups.
```
