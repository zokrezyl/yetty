# ymux — the session server (#695 / #699)

One `ymux` process (in its daemon/server role) owns every pane's terminal state (PTYs, the libvterm
engine, scrollback); clients render a bounded projection. The module map,
protocol, and renderer are documented in the sources they belong to
(`proto.h`, `projector.c`, `tty-render.h`); this file records the
cross-cutting contracts that are not obvious from any single file.

## Terminal replies — the authority contract

Every terminal query (DSR, DA, DECRQSS, color reports, …) is answered by the
**daemon-side engine at the pane**: `engine.c` drains libvterm's reply output
to `host.output`, which writes to the pane's PTY — the application receives
exactly one answer, from the terminal that actually owns the state.

There are TWO query owners, never conflated (review #16):

- Queries the pane APPLICATION emits are answered exactly once by the
  authoritative daemon-side engine at the pane (engine host.output → the
  application PTY).
- Queries the per-attachment RENDERER emits are answered by that renderer's
  terminal endpoint (the client grid), and those response bytes belong to
  THAT ATTACHMENT's response parser — tmux's tty response handling analog —
  never to the application. The route: the grid accumulates its libvterm's
  reply bytes; the scene exposes a scalar-word drain (buffer returns are not
  wire-marshallable); the bridge forwards the RAW bytes over
  `YMUX_PROTO_TTY_RESPONSE`; the daemon routes them to the originating
  attachment's projector (`consume_tty_response` — DA/CPR state machine,
  bytes preserved exactly). Per-attachment by construction: two attached
  renderers each consume their own replies; the application receives zero.
  `reply_discarded_bytes` counts only overflow past the bounded buffer.

Consumed OVERLAY input takes the same shape in reverse ownership: the bridge
drains the overlay scene's input queue (`input_event_head` / `_word` /
`_pop`) and forwards each event to the daemon (`OVERLAY_INPUT`) — the daemon
publishes the chrome, so its interaction logic consumes at that seat.

## Slow-client / failed-enqueue recovery

The projected VT stream is stateful; bytes can never be skipped. Whenever a
frame cannot be queued for a client — capacity pre-check failure OR an actual
lane enqueue failure (`daemon_recover_slow_client`) — the daemon drops that
client's obsolete queued terminal frames, invalidates its projector (the next
projection is a fresh complete redraw), and resyncs the ACK window. Projector
state committed for bytes that never reached the queue is therefore always
superseded by the recovery redraw.
