#ifndef YETTY_YPLATFORM_PTY_H
#define YETTY_YPLATFORM_PTY_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/yplatform/pty-pipe-source.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_platform_pty;
YETTY_YRESULT_DECLARE(yetty_yplatform_pty_ptr, struct yetty_platform_pty *);

/* Pty ops */
struct yetty_platform_pty_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_platform_pty *self);
    struct yetty_ycore_size_result (*read)(struct yetty_platform_pty *self, char *buf,
                                           size_t max_len);
    struct yetty_ycore_size_result (*write)(struct yetty_platform_pty *self, const char *data,
                                            size_t len);
    /* `pixel_w` / `pixel_h` carry the full pane pixel size so the impl
     * can populate `ws_xpixel` / `ws_ypixel` for TIOCGWINSZ. Without it,
     * client apps that need the actual pane pixel area (the ymgui demo,
     * any GPU-rendering client) can only multiply ws_col/ws_row by a
     * default 8×16 cell — wrong on every non-default cell size. Zero is
     * acceptable when the impl truly doesn't know yet (initial create
     * before the first grid is laid out); yetty pushes a real value as
     * soon as the layer cell size is known. */
    struct yetty_ycore_void_result (*resize)(struct yetty_platform_pty *self, uint32_t cols,
                                             uint32_t rows, uint32_t pixel_w, uint32_t pixel_h);
    struct yetty_ycore_void_result (*stop)(struct yetty_platform_pty *self);
    struct yetty_platform_pty_pipe_source *(*pipe_source)(struct yetty_platform_pty *self);
};

/* Pty base */
struct yetty_platform_pty {
    const struct yetty_platform_pty_ops *ops;
};
struct yetty_yconfig_config;

struct yetty_yplatform_pty_ptr_result yetty_yplatform_fork_pty_create(
    struct yetty_yconfig_config *config);

struct yetty_yplatform_pty_ptr_result yetty_yplatform_tinyemu_pty_create(
    struct yetty_yconfig_config *config);

/* Webasm-only: hvc0 PTY backed by the in-iframe TinyEMU VM (singleton —
 * exactly one console exists per page). Default factory call 1 returns
 * this; call 2+ falls back to telnet-over-iframe-transport. */
struct yetty_yplatform_pty_ptr_result yetty_yplatform_iframe_pty_create(
    struct yetty_yconfig_config *config);

/* In-memory PTY pair — two endpoints sharing two fixed-cap ring buffers.
 * Endpoint a's read drains what endpoint b wrote and vice versa. No fd
 * (pipe_source returns NULL), no compression. Used for in-process bridges
 * (yui ↔ render thread today, same thread; eventual thread split later).
 * `buf_size` is the per-direction byte capacity, rounded up to a power of 2.
 * Pass 0 to use the default (16 MiB). NOT thread-safe — single-thread use
 * only until a locking variant lands. */
struct yetty_yplatform_memory_pty_pair {
    struct yetty_platform_pty *a;
    struct yetty_platform_pty *b;
};
YETTY_YRESULT_DECLARE(yetty_yplatform_memory_pty_pair, struct yetty_yplatform_memory_pty_pair);
struct yetty_yplatform_memory_pty_pair_result yetty_yplatform_memory_pty_pair_create(
    size_t buf_size);

/* Memory-pty has no fd so libuv has no readiness signal. Caller installs a
 * wake callback per endpoint; it fires whenever the OTHER endpoint writes
 * bytes into this endpoint's read ring. Wire it to the event loop's
 * `request_render` (or `post_to_loop` once threading splits) so the reader
 * side gets a chance to drain. Pass NULL to disarm. The memory-pty does
 * not own `userdata`. */
typedef void (*yetty_yplatform_memory_pty_wake_fn)(void *userdata);
void yetty_yplatform_memory_pty_set_wake(struct yetty_platform_pty *endpoint,
                                         yetty_yplatform_memory_pty_wake_fn wake, void *userdata);

/* Window-size propagation across the pair — the in-process analog of TIOCSWINSZ
 * + SIGWINCH + TIOCGWINSZ. A resize on one endpoint records the size on the
 * shared pair and fires the OTHER endpoint's resize callback, so the peer's
 * owner can re-layout (e.g. call ygui_framework_set_viewport). Install the
 * callback on the endpoint that needs to learn size changes; pass NULL to
 * disarm. The memory-pty does not own `userdata`. */
typedef void (*yetty_yplatform_memory_pty_resize_fn)(void *userdata, uint32_t cols, uint32_t rows,
                                                     uint32_t pixel_w, uint32_t pixel_h);
void yetty_yplatform_memory_pty_set_resize(struct yetty_platform_pty *endpoint,
                                           yetty_yplatform_memory_pty_resize_fn resize,
                                           void *userdata);

/* Read back the window size recorded by the most recent resize on either
 * endpoint (the TIOCGWINSZ analog). Any out-param may be NULL; all are zero
 * until the first resize. */
void yetty_yplatform_memory_pty_get_winsize(struct yetty_platform_pty *endpoint, uint32_t *cols,
                                            uint32_t *rows, uint32_t *pixel_w, uint32_t *pixel_h);

/* Host-side TCP port that the TinyEMU slirp hostfwd maps to the in-guest
 * telnetd (tcp/23). Keep in sync with assets/yemu/temu/yetty-temu-extended.cfg's
 * `hostfwd: ["tcp:2323-:23"]`. Used by --temu in pty-factory/default.c. */
#define TINYEMU_TELNET_PORT 2323

struct yetty_yplatform_pty_factory;
struct yetty_yevent_event_loop;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yplatform_pty_factory_ptr, struct yetty_yplatform_pty_factory *);

/* Pty factory ops */
struct yetty_yplatform_pty_factory_ops {
    void (*destroy)(struct yetty_yplatform_pty_factory *self);
    struct yetty_yplatform_pty_ptr_result (*create_pty)(struct yetty_yplatform_pty_factory *self,
                                                        struct yetty_yevent_event_loop *event_loop);
};

/* Pty factory base */
struct yetty_yplatform_pty_factory {
    const struct yetty_yplatform_pty_factory_ops *ops;
};

/* Platform-specific create functions (implemented per platform) */
struct yetty_yplatform_pty_factory_ptr_result yetty_yplatform_pty_factory_create(
    struct yetty_yconfig_config *config, void *os_specific);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_PTY_FACTORY_H */
