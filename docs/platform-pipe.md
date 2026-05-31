# Platform Input Pipe

The input pipe carries input events from platform code (the OS event thread) to
the worker thread's event loop. It's a dumb byte transport plus a notification
mechanism — it does not interpret or dispatch events.

The concrete type is `struct yetty_ycore_xthread_event_pipe`
(`include/yetty/yplatform/platform-input-pipe.h`). See
[Platform Abstraction](platform.md) for the threading model and
[Platform PTY](platform-pty.md) for the sibling PTY transport.

## Interface

```c
struct yetty_platform_input_pipe_ops {
    void (*destroy)(struct yetty_ycore_xthread_event_pipe *self);
    struct yetty_ycore_size_result (*write)(struct yetty_ycore_xthread_event_pipe *self,
                                            const void *data, size_t size);
    struct yetty_ycore_size_result (*read)(struct yetty_ycore_xthread_event_pipe *self,
                                           void *data, size_t max_size);
    struct yetty_ycore_int_result  (*read_fd)(const struct yetty_ycore_xthread_event_pipe *self);
    struct yetty_ycore_void_result (*set_event_loop)(struct yetty_ycore_xthread_event_pipe *self,
                                                     struct yetty_yevent_event_loop *loop);
    struct yetty_ycore_void_result (*set_nonblocking_write)(
        struct yetty_ycore_xthread_event_pipe *self);
};

struct yetty_ycore_xthread_event_pipe {
    const struct yetty_platform_input_pipe_ops *ops;
};

/* Platform-specific factory */
struct yetty_yplatform_input_pipe_result yetty_platform_input_pipe_create(void);
```

- `write` / `read` move opaque bytes (event structs); the pipe never inspects
  them.
- `read_fd` returns the readable fd on platforms that have one, or an error on
  those that don't (WebAssembly).
- `set_event_loop` wires the pipe to the loop on fd-less platforms so the pipe
  can drive the loop directly.
- `set_nonblocking_write` switches the producer side to non-blocking — needed
  when producer and consumer share one libuv loop (e.g. a telnet PTY feeding the
  wire state machine), so a full buffer can't deadlock the loop.

## Flow

```
OS input callback (main thread)
        │  pipe->ops->write(&event, sizeof(event))
        ▼
   [ notification ]
        │  fd readable (native)  /  async callback (WebAssembly)
        ▼
event loop wakes, drains the pipe:
        while (pipe->ops->read(self, &event, sizeof(event)).value == sizeof(event))
            dispatch(event);   /* routed to the focused view / terminal */
```

`ymain` seeds the very first event this way — it writes a `YETTY_YCORE_RESIZE`
event to the pipe so the first frame uses the live framebuffer size (see
[Contexts](contexts.md)).

## Platform differences

| Platform | Notification mechanism |
|---|---|
| Linux / macOS | libuv polls the read fd |
| Windows | libuv polls the read handle |
| WebAssembly | `set_event_loop` + an async callback (no fd) |

The mechanism differs; the flow above is identical everywhere.

## Pointers

- Interface: `include/yetty/yplatform/platform-input-pipe.h`
- Event loop: `include/yetty/yevent/event-loop.h`
- First-event seeding: `src/yetty/ymain/glfw.c`
