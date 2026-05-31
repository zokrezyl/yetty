# Context Pattern

Yetty avoids threading many arguments through factory functions. Instead of:

```c
/* NO — long, fragile, hard to extend */
thing_create(device, queue, allocator, config, event_loop, pty_factory, ...);
```

each level of the system bundles what its children need into a small **context
struct** and passes that by pointer:

```c
/* YES — one borrowed context */
struct yetty_yterm_terminal_result yetty_yterm_terminal_create(
    struct yetty_ycore_grid_size grid_size, const struct yetty_context *yetty_context);
```

Contexts are plain C structs (POD). They hold **copies** of the parent slice and
**borrowed pointers** to objects the parent owns. There is no global state — a
component reaches everything it needs through the context it was handed.

See [Design Overview](design.md) for where this sits among the other core
decisions.

---

## The build chain

Three layers bring a process up, in strict order. Each is a separate module so
that non-terminal apps (analyzers, diagnostics, visualizers) can reuse the lower
two and supply only their own body.

```
yinit  ──────────►  yframework  ──────────►  yetty
(platform bootstrap) (GPU/event/RPC services) (terminal app)

build:    yinit → yframework → app
teardown: app → yframework → yinit   (strictly nested)
```

- **yinit** (`include/yetty/yinit/yinit.h`) — paths, asset extraction, config
  parsing, window + WebGPU surface, event pipes, OS event loop. It calls a
  worker function on a dedicated thread and hands it a `yetty_yinit_runtime`.
- **yframework** (`include/yetty/yframework/yframework.h`) — requests the WebGPU
  adapter/device/queue, builds the GPU allocator, MSDF generator, render target,
  event loop, and the optional VNC and RPC servers.
- **yetty** (`include/yetty/yetty/yetty.h`) — the terminal application: tabs,
  panes, terminals.

The real entry point (`src/yetty/ymain/glfw.c`) is a thin wrapper:

```c
int main(int argc, char **argv)
{
    struct yetty_yinit_app_config cfg = { .extract_assets_fn = yetty_platform_extract_assets };
    return yetty_yinit_run(argc, argv, &cfg, yetty_worker, NULL);
}

/* worker runs on the render thread once the platform is up */
static struct yetty_ycore_void_result yetty_worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct yetty_yplatform_pty_factory *pty_factory = /* ...create from rt->config... */;
    struct yetty_yframework        *yframework   = yetty_yframework_create(rt).value;
    struct yetty_yetty_yetty       *yetty        = yetty_create(yframework, pty_factory).value;
    struct yetty_ycore_void_result  res          = yetty_run(yetty);
    /* teardown is the exact reverse: yetty → yframework → pty_factory */
    yetty_destroy(yetty);
    yetty_yframework_destroy(yframework);
    pty_factory->ops->destroy(pty_factory);
    return res;
}
```

---

## The context structs

### `yetty_yinit_runtime` — platform slice (owned by yinit)

Everything the platform produced before the app runs. Borrowed by the worker;
yinit frees it after the worker returns.

```c
struct yetty_yinit_runtime {
    int argc; char **argv;                 /* CLI passthrough (NULL on android) */
    struct yetty_yconfig_config *config;   /* parsed config (owned by yinit)    */

    void *instance;                        /* WGPUInstance                       */
    void *surface;                         /* WGPUSurface — NULL in headless mode */
    uint32_t surface_width, surface_height;
    float content_scale;                   /* framebuffer/window (HiDPI)          */
    void *x11_display; unsigned long x11_window; /* X11 only; NULL/0 elsewhere    */
    void *window;                          /* opaque native window handle         */

    struct yetty_ycore_xthread_event_pipe *platform_input_pipe; /* main → worker */
    struct yetty_ycore_xthread_event_pipe *output_pipe;         /* worker → main  */
    struct yetty_platform_clipboard_manager *clipboard_manager;
    struct yetty_yplatform_window_manager   *window_manager;
};
```

`surface == NULL` is the headless case (`vnc/headless=true` in config): the
worker still runs, just without a presentable surface.

### `yetty_yinit_gpu_context` — platform GPU slice

The GPU-relevant subset of the above, embedded by value so a consumer can pass a
pointer to just this slice.

```c
struct yetty_yinit_gpu_context {
    WGPUInstance instance;
    WGPUSurface  surface;
    uint32_t     surface_width, surface_height;
    float        content_scale;
    void *x11_display; unsigned long x11_window;
};
```

### `yetty_yframework_gpu_context` — runtime GPU objects

Built on top of the platform slice. Lives on `struct yetty_yframework`.

```c
struct yetty_yframework_gpu_context {
    struct yetty_yinit_gpu_context app_gpu_context; /* copy of the platform slice */
    WGPUAdapter         adapter;
    WGPUDevice          device;
    WGPUQueue           queue;
    WGPUTextureFormat   surface_format;
    struct yetty_ydraw_gpu_allocator *allocator;
    struct yetty_ymsdf_generator     *msdf_generator; /* cpu | gpu, from config   */
};
```

### `yetty_context` — what the terminal hierarchy receives

The compact context propagated down through tabs, panes, and terminals.

```c
struct yetty_context {
    struct yetty_yframework            *runtime;     /* source of truth for GPU/  */
                                                     /* event/RPC/render-target   */
    struct yetty_yplatform_pty_factory *pty_factory; /* yetty-specific            */
    struct yetty_yevent_event_loop     *event_loop;  /* alias of runtime->event_loop */
};
```

`runtime` is the one borrowed object that owns the WebGPU device, queue,
allocator, MSDF generator, render target, and the optional VNC/RPC servers.
`event_loop` is a convenience alias so hot paths don't dig through `runtime`
each time.

### `yetty_yterm_terminal_context` — terminal leaf

The terminal adds only its PTY to the context it received.

```c
struct yetty_yterm_terminal_context {
    struct yetty_context        yetty_context; /* copy of parent */
    struct yetty_platform_pty  *pty;
};
```

---

## Ownership rules

- A context holds a **copy** of its parent's context (cheap, POD).
- A context holds **borrowed pointers** to objects owned by a specific level;
  the level that creates an object destroys it.
- Lifetimes are strictly nested: a child must be torn down before the parent
  whose context it borrowed. The `ymain` worker enforces this explicitly
  (`yetty_destroy` before `yetty_yframework_destroy`) because pending GPU
  readback callbacks dereference the framework's render target and event loop.

## Pointers

- Bootstrap: `include/yetty/yinit/yinit.h`, `src/yetty/ymain/glfw.c`
- Services: `include/yetty/yframework/yframework.h`
- App + context: `include/yetty/yetty/yetty.h`
- Terminal leaf: `include/yetty/yterm/terminal.h`
