# ynotify — thread-safe user-facing notifications

`ynotify` is a tiny leaf primitive: any subsystem, on any thread, can call
`ynotify(severity, "fmt", ...)` to surface a message to the user without
depending on yui, ygui, or the event loop. The call always logs through
[ytrace](../ytrace/README.md) (`yinfo`/`ywarn`/`yerror` by severity) and, if
one is installed, forwards the formatted text to a single global handler.
Its only dependencies are ytrace and the
[yplatform](../yplatform/README.md) mutex.

## How it works

- One global handler slot plus a lazily-created mutex (same pattern as
  ytrace, so no init entry point every binary must remember to call).
- `vynotify` formats into a stack buffer, logs unconditionally, then
  snapshots the handler under the lock and calls it **outside** the lock —
  a handler that re-enters ynotify (e.g. from its own error path) cannot
  deadlock, and a slow handler does not stall other callers.
- No handler registered (headless tools, unit tests) → log-only. Producers
  never need to know whether anyone is listening.

## Wiring at startup

`yetty_yui_create()` installs the handler: it posts the notification onto
the event-loop thread, where the ygui framework's notify renders it as an
on-screen toast on yui's engine. That hop is what makes `ynotify` safe from
any thread. yui clears the handler again on destroy. Severity selects both
the log level and the toast accent stripe (INFO mint, WARN amber, ERROR
crimson).

## Public API

```c
enum yetty_ynotify_severity { YETTY_YNOTIFY_INFO, YETTY_YNOTIFY_WARN, YETTY_YNOTIFY_ERROR };

void ynotify(int severity, const char *fmt, ...);          /* printf-style, thread-safe */
void vynotify(int severity, const char *fmt, va_list ap);  /* for wrappers */
void ynotify_set_handler(yetty_ynotify_handler_fn handler, void *userdata); /* NULL clears */
```

The `msg` pointer passed to a handler is borrowed for the duration of the
call — copy it to defer.

## File map

| file | role |
|------|------|
| `ynotify.c` | handler slot, lazy mutex, format + log + forward |

No per-module CMakeLists — `ynotify.c` is compiled directly into the
application source set (`YETTY_SOURCES` in `src/yetty/CMakeLists.txt`).

## Consumers

- [yui](../yui/README.md) — installs/clears the on-screen handler.
- The [yevent](../yevent/README.md) loop implementations
  (`yplatform/libuv-event-loop/*`) — `post_fatal_error` renders a
  callback-boundary error chain through `ynotify` so the loop keeps running.
- `yetty.c` and other subsystems emit ad-hoc warnings/errors directly.
