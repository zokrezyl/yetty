/* WebASM iframe PTY backend
 *
 * Replaces the in-process pthread+TinyEMU PTY (shared/tinyemu-pty.c) on
 * the webasm target. The Linux VM runs in a separate, hidden iframe (its
 * own wasm instance, single-threaded) and exchanges bytes with yetty via
 * postMessage. This eliminates the per-batch libc-syscall overhead the
 * pthread variant pays on emscripten — see ../webasm.cmake for the
 * ALLOW_MEMORY_GROWTH=0 / -pthread comment that motivated this rework.
 *
 * Wire protocol (mirrors the legacy POC):
 *   parent  → iframe : { type: 'term-input',  ptyId, data: <utf8 string> }
 *   parent  → iframe : { type: 'term-resize', ptyId, cols, rows }
 *   iframe  → parent : { type: 'term-output', ptyId, data: <utf8 string> }
 *
 * Output flows iframe → parent JS → wasm via Module._iframe_pty_on_data,
 * which writes into a same-thread pipe(2). The webasm event loop drains
 * that pipe in its rAF tick, exactly like the previous backend, so the
 * downstream code path is unchanged.
 */

#include <yetty/yplatform/pty.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <emscripten/emscripten.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct yetty_yplatform_iframe_pty {
    struct yetty_platform_pty base;
    struct yetty_platform_pty_pipe_source pipe_source;

    /* vm_output_pipe: JS message handler writes [1], event loop reads [0].
     * Single-threaded — no atomics, no pthread shim cost. */
    int vm_output_pipe[2];

    uint32_t pty_id;
    uint32_t cols;
    uint32_t rows;
    int running;
};

/* Singleton — webasm currently runs exactly one PTY. The JS-side message
 * listener uses pty_id to route, but in practice everything maps to this
 * one instance. Mirrors g_pty in shared/tinyemu-pty.c. */
static struct yetty_yplatform_iframe_pty *g_iframe_pty = NULL;
static uint32_t g_next_pty_id = 1;

/* Forward decls */
static struct yetty_ycore_void_result iframe_pty_destroy(struct yetty_platform_pty *self);
static struct yetty_ycore_size_result iframe_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len);
static struct yetty_ycore_size_result iframe_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len);
static struct yetty_ycore_void_result iframe_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows);
static struct yetty_ycore_void_result iframe_pty_stop(struct yetty_platform_pty *self);
static struct yetty_platform_pty_pipe_source *iframe_pty_pipe_source(
    struct yetty_platform_pty *self);

static const struct yetty_platform_pty_ops iframe_pty_ops = {
    .destroy = iframe_pty_destroy,
    .read = iframe_pty_read,
    .write = iframe_pty_write,
    .resize = iframe_pty_resize,
    .stop = iframe_pty_stop,
    .pipe_source = iframe_pty_pipe_source,
};

/* Called from JS when the iframe posts a 'term-output' message. The data
 * is heap-allocated by JS via Module._malloc and freed there after the
 * call returns. We just push it into the pipe; the event loop will drain
 * it on its next tick. */
EMSCRIPTEN_KEEPALIVE
void iframe_pty_on_data(uint32_t pty_id, const char *data, int len)
{
    struct yetty_yplatform_iframe_pty *pty = g_iframe_pty;

    if (!pty || !pty->running || len <= 0) {
        return;
    }
    if (pty_id != pty->pty_id) {
        ywarn("iframe_pty_on_data: unknown pty_id=%u (have %u)", pty_id, pty->pty_id);
        return;
    }

    /* Best-effort write. The pipe is non-blocking; if full (renderer
     * stalled) we drop — preferable to blocking the JS message handler. */
    ssize_t written = write(pty->vm_output_pipe[1], data, (size_t)len);
    if (written < 0) {
        ywarn("iframe_pty_on_data: pipe write failed (%d bytes)", len);
    } else if (written != len) {
        ywarn("iframe_pty_on_data: short pipe write %zd/%d (renderer slow?)", written, len);
    }
}

/* Pty operations */

static struct yetty_ycore_void_result iframe_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_yplatform_iframe_pty *pty =
        container_of(self, struct yetty_yplatform_iframe_pty, base);
    struct yetty_ycore_void_result stop_r = iframe_pty_stop(self);

    if (pty->vm_output_pipe[0] >= 0) {
        close(pty->vm_output_pipe[0]);
    }
    if (pty->vm_output_pipe[1] >= 0) {
        close(pty->vm_output_pipe[1]);
    }

    if (g_iframe_pty == pty) {
        g_iframe_pty = NULL;
    }
    free(pty);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "iframe_pty_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result iframe_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len)
{
    struct yetty_yplatform_iframe_pty *pty =
        container_of(self, struct yetty_yplatform_iframe_pty, base);

    if (!pty->running || max_len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    ssize_t n = read(pty->vm_output_pipe[0], buf, max_len);
    if (n < 0) {
        n = 0;
    }
    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_size_result iframe_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len)
{
    struct yetty_yplatform_iframe_pty *pty =
        container_of(self, struct yetty_yplatform_iframe_pty, base);

    if (!pty->running || len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    // clang-format off
    /* Forward keystrokes / pasted text to the iframe. Treat as raw bytes —
     * the iframe's term-bridge decodes UTF-8 and feeds the VM console. */
    EM_ASM({
        var ptyId = $0;
        var ptr = $1;
        var len = $2;
        var bytes = HEAPU8.subarray(ptr, ptr + len);
        var data = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
        var iframe = document.getElementById('yetty-vm-pty-' + ptyId);
        if (iframe && iframe.contentWindow) {
            iframe.contentWindow.postMessage({
                type: 'term-input',
                ptyId: ptyId,
                data: data
            }, '*');
        }
    }, pty->pty_id, data, (int)len);

    // clang-format on
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result iframe_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows)
{
    struct yetty_yplatform_iframe_pty *pty =
        container_of(self, struct yetty_yplatform_iframe_pty, base);

    pty->cols = cols;
    pty->rows = rows;

    // clang-format off
    EM_ASM({
        var ptyId = $0;
        var cols = $1;
        var rows = $2;
        var iframe = document.getElementById('yetty-vm-pty-' + ptyId);
        if (iframe && iframe.contentWindow) {
            iframe.contentWindow.postMessage({
                type: 'term-resize',
                ptyId: ptyId,
                cols: cols,
                rows: rows
            }, '*');
        }
    }, pty->pty_id, (int)cols, (int)rows);

    // clang-format on
    yinfo("iframe_pty: resize %ux%u (pty_id=%u)", cols, rows, pty->pty_id);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result iframe_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_yplatform_iframe_pty *pty =
        container_of(self, struct yetty_yplatform_iframe_pty, base);

    if (!pty->running) {
        return YETTY_OK_VOID();
    }
    pty->running = 0;

    // clang-format off
    EM_ASM({
        var ptyId = $0;
        var iframe = document.getElementById('yetty-vm-pty-' + ptyId);
        if (iframe) {
            iframe.remove();
        }
    }, pty->pty_id);

    // clang-format on
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *iframe_pty_pipe_source(
    struct yetty_platform_pty *self)
{
    struct yetty_yplatform_iframe_pty *pty =
        container_of(self, struct yetty_yplatform_iframe_pty, base);
    return &pty->pipe_source;
}

/* Constructor — registers the postMessage listener at module init time so
 * we never miss an early 'term-output' from the iframe. The C handler
 * (iframe_pty_on_data) tolerates being called before the pty struct
 * exists by checking g_iframe_pty. */
__attribute__((constructor)) static void iframe_pty_install_message_listener(void)
{
    // clang-format off

    EM_ASM({
        if (window.__yettyIframePtyListener) {
            return;
        }
        window.__yettyIframePtyListener = function(e) {
            if (!e.data || e.data.type !== 'term-output' || e.data.ptyId === undefined) {
                return;
            }
            var data = e.data.data;
            if (typeof data !== 'string' || data.length === 0) {
                return;
            }
            var ptyId = parseInt(e.data.ptyId, 10);
            if (isNaN(ptyId)) {
                return;
            }
            var bytes = new TextEncoder().encode(data);
            if (bytes.length === 0) {
                return;
            }
            var ptr = Module._malloc(bytes.length);
            if (ptr === 0) {
                return;
            }
            Module.HEAPU8.set(bytes, ptr);
            Module._iframe_pty_on_data(ptyId, ptr, bytes.length);
            Module._free(ptr);
        };
        window.addEventListener('message', window.__yettyIframePtyListener);
    });

    // clang-format on
}

/* Pty creation */

struct yetty_yplatform_pty_ptr_result yetty_yplatform_iframe_pty_create(
    struct yetty_yconfig_config *config)
{
    struct yetty_yplatform_iframe_pty *pty;

    (void)config;

    if (g_iframe_pty) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "iframe pty already created (singleton)");
    }

    pty = calloc(1, sizeof(*pty));
    if (!pty) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to allocate iframe pty");
    }

    pty->base.ops = &iframe_pty_ops;
    pty->vm_output_pipe[0] = -1;
    pty->vm_output_pipe[1] = -1;
    pty->cols = 80;
    pty->rows = 24;
    pty->pty_id = g_next_pty_id++;
    pty->running = 1;

    if (pipe(pty->vm_output_pipe) < 0) {
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to create vm_output_pipe");
    }
    fcntl(pty->vm_output_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(pty->vm_output_pipe[1], F_SETFL, O_NONBLOCK);

    pty->pipe_source.abstract = pty->vm_output_pipe[0];

    /* Publish the singleton BEFORE creating the iframe — the iframe may
     * post output as soon as it loads, and the message handler needs to
     * find us. */
    g_iframe_pty = pty;

    /* Spawn the iframe. It loads tinyemu.js inside its own wasm context
     * (no -pthread, separate heap) and starts the VM. The cfg / cols /
     * rows are passed via query string; the iframe's bridge JS reads
     * them and configures TinyEMU before kicking the VM. */
    /* Index.html spawns the iframe BEFORE loading yetty.js (when mode
     * is "vm" — see launchYetty in index.html). By the time we reach
     * here, the iframe element should already exist and the VM should
     * already be running (vm-ready was the gate that unblocked yetty
     * loading in the first place). Just hook up to it.
     *
     * Fallback: if no iframe exists (dev usage / non-vm mode flowing
     * here unexpectedly), spawn one — yetty still works, the boot
     * messages just won't be ordered visibly in the overlay.
     *
     * After the iframe is found / created, we post 'yetty-ready' to
     * tell its tinyemu-iframe.html script that yetty is now listening
     * for term-output messages — it flushes its internal output
     * buffer so the kernel boot lines (which arrived BEFORE yetty
     * loaded) are still delivered to the terminal. */

    // clang-format off
    EM_ASM({
        var ptyId = $0;
        var cols = $1;
        var rows = $2;
        var iframe = document.getElementById('yetty-vm-pty-' + ptyId);
        var spawned = false;
        if (!iframe) {
            if (window.yettyStatus) {
                window.yettyStatus.append(
                    'spawning tinyemu iframe (pty ' + ptyId + ', ' +
                    cols + 'x' + rows + ') — fallback path', 'phase');
            }
            iframe = document.createElement('iframe');
            iframe.id = 'yetty-vm-pty-' + ptyId;
            /* Off-screen, not display:none — Chrome throttles
             * timers in display:none iframes, slowing the
             * wasm-interpreted VM enough that boot/telnetd
             * doesn't finish in any reasonable time. */
            iframe.style.cssText =
                'position:absolute; left:-99999px; top:-99999px;' +
                ' width:1px; height:1px; border:0;' +
                ' visibility:hidden; pointer-events:none;';
            iframe.src = 'tinyemu-iframe.html?ptyId=' + ptyId +
                         '&cols=' + cols + '&rows=' + rows;
            document.body.appendChild(iframe);
            spawned = true;
        } else {
            if (window.yettyStatus) {
                window.yettyStatus.append(
                    'connected to existing VM iframe (pty ' + ptyId + ')', 'ok');
            }
        }
        // Tell the iframe yetty is now attached so it can flush any
        // term-output it buffered while yetty was still loading.
        // contentWindow is null until the iframe's first navigation
        // commits — when we just spawned it, defer the post until
        // 'load' fires.
        var attach = function () {
            try {
                iframe.contentWindow.postMessage({
                    type: 'yetty-ready',
                    ptyId: ptyId
                }, '*');
            } catch (_) {}
        };
        if (spawned) {
            iframe.addEventListener('load', attach);
        } else {
            attach();
        }
        // No auto-hide. The boot console is full-screen and
        // scrollable — the user reads it (kernel boot, openrc,
        // telnetd bringup) and dismisses manually via the × button.
        // Hiding it under their cursor mid-scroll would be hostile.
    }, pty->pty_id, (int)pty->cols, (int)pty->rows);

    // clang-format on

    yinfo("iframe_pty: created pty_id=%u", pty->pty_id);
    return YETTY_OK(yetty_yplatform_pty_ptr, &pty->base);
}

/* The yetty_yplatform_pty_factory_create symbol is now owned by
 * webasm/telnet-iframe-pty-factory.c (default = telnet-over-iframe).
 * The iframe-pty (virtio-console hvc0) backend in this file stays
 * compiled and reachable via yetty_yplatform_iframe_pty_create() if
 * anyone wants to attach yetty to the boot console for debugging,
 * but it's not the default factory anymore. */
