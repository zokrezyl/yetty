/* WebAssembly PTY implementation using JSLinux iframe */

#include "webasm-pty.h"
#include <yetty/yconfig.h>
#include <yetty/ycore/types.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/ytrace.h>
#include <emscripten/emscripten.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static struct yetty_ycore_void_result webasm_pty_destroy(struct yetty_platform_pty *self);
static struct yetty_ycore_size_result webasm_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len);
static struct yetty_ycore_size_result webasm_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len);
static struct yetty_ycore_void_result webasm_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows);
static struct yetty_ycore_void_result webasm_pty_stop(struct yetty_platform_pty *self);
static struct yetty_platform_pty_pipe_source *webasm_pty_pipe_source(
    struct yetty_platform_pty *self);

/* Ops table */
static const struct yetty_platform_pty_ops webasm_pty_ops = {
    .destroy = webasm_pty_destroy,
    .read = webasm_pty_read,
    .write = webasm_pty_write,
    .resize = webasm_pty_resize,
    .stop = webasm_pty_stop,
    .pipe_source = webasm_pty_pipe_source,
};

/* Pipe source implementation */

void yetty_yplatform_webasm_pty_pipe_source_set_callback(struct yetty_yplatform_webasm_pty_pipe_source *source,
                                         webasm_pty_notify_callback callback, void *user_data)
{
    if (!source) {
        return;
    }
    source->notify_callback = callback;
    source->notify_user_data = user_data;
}

void yetty_yplatform_webasm_pty_pipe_source_notify(struct yetty_yplatform_webasm_pty_pipe_source *source)
{
    if (source && source->notify_callback) {
        source->notify_callback(source->notify_user_data);
    }
}

/* PTY implementation */

static struct yetty_ycore_void_result webasm_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_yplatform_webasm_pty *pty = container_of(self, struct yetty_yplatform_webasm_pty, base);

    struct yetty_ycore_void_result stop_r = webasm_pty_stop(self);
    free(pty);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "webasm_pty_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result webasm_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len)
{
    struct yetty_yplatform_webasm_pty *pty = container_of(self, struct yetty_yplatform_webasm_pty, base);
    int bytes_read;

    if (!pty->running || max_len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    /* Read from JS buffer via EM_ASM */
    bytes_read = EM_ASM_INT(
        {
            var maxLen = $1;
            var chunk = window.pty_read_buffer(maxLen);
            if (chunk.length === 0) {
                return 0;
            }

            /* Encode to UTF-8 and copy to C buffer */
            var encoder = new TextEncoder();
            var bytes = encoder.encode(chunk);
            var len = Math.min(bytes.length, maxLen);
            HEAPU8.set(bytes.subarray(0, len), $0);
            return len;
        },
        buf, (int)max_len);

    return YETTY_OK(yetty_ycore_size, (size_t)(bytes_read > 0 ? bytes_read : 0));
}

static struct yetty_ycore_size_result webasm_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len)
{
    struct yetty_yplatform_webasm_pty *pty = container_of(self, struct yetty_yplatform_webasm_pty, base);

    if (!pty->running || len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    EM_ASM(
        {
            var data = UTF8ToString($0, $1);
            var iframe = document.getElementById('jslinux-pty');
            if (iframe && iframe.contentWindow) {
                iframe.contentWindow.postMessage({type : 'term-input', data : data}, '*');
            }
        },
        data, (int)len);

    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result webasm_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows)
{
    struct yetty_yplatform_webasm_pty *pty = container_of(self, struct yetty_yplatform_webasm_pty, base);

    pty->cols = cols;
    pty->rows = rows;

    EM_ASM(
        {
            var cols = $0;
            var rows = $1;
            window.ptyPendingCols = cols;
            window.ptyPendingRows = rows;

            if (window.ptyReady) {
                var iframe = document.getElementById('jslinux-pty');
                if (iframe && iframe.contentWindow) {
                    iframe.contentWindow.postMessage(
                        {type : 'term-resize', cols : cols, rows : rows}, '*');
                }
            }
        },
        cols, rows);

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result webasm_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_yplatform_webasm_pty *pty = container_of(self, struct yetty_yplatform_webasm_pty, base);

    if (!pty->running) {
        return YETTY_OK_VOID();
    }

    pty->running = 0;
    ydebug("webasm_pty: Stopping");

    EM_ASM({
        var iframe = document.getElementById('jslinux-pty');
        if (iframe) {
            iframe.remove();
        }
        window.ptyBuffer = "";
    });

    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *webasm_pty_pipe_source(
    struct yetty_platform_pty *self)
{
    struct yetty_yplatform_webasm_pty *pty = container_of(self, struct yetty_yplatform_webasm_pty, base);
    return &pty->pipe_source.base;
}

/* Initialize PTY */

struct yetty_ycore_void_result yetty_yplatform_webasm_pty_init(struct yetty_yplatform_webasm_pty *pty, struct yetty_yconfig *config)
{
    pty->cols = (uint32_t)config->ops->get_int(config, "terminal/cols", 80);
    pty->rows = (uint32_t)config->ops->get_int(config, "terminal/rows", 24);
    pty->running = 1;
    pty->pipe_source.base.abstract = 0; /* No fd on webasm */
    pty->pipe_source.notify_callback = NULL;
    pty->pipe_source.notify_user_data = NULL;

    ydebug("webasm_pty: Starting (%ux%u)", pty->cols, pty->rows);

    /* Set up JS buffer and message listener in parent window,
	 * then create iframe for JSLinux VM */
    EM_ASM(
        {
            var pipeSourcePointer = $0;
            var cols = $1;
            var rows = $2;

            /* Buffer in parent window (like kernel buffer on Unix) */
            window.ptyBuffer = "";
            window.ptyReady = false;
            window.ptyPendingCols = cols;
            window.ptyPendingRows = rows;

            /* Read from buffer - called by C via EM_ASM */
            window.pty_read_buffer = function(maxLen)
            {
                var chunk = window.ptyBuffer.substring(0, maxLen);
                window.ptyBuffer = window.ptyBuffer.substring(chunk.length);
                return chunk;
            };

            /* Message listener - receives data from iframe, buffers it, notifies PipeSource */
            window.addEventListener(
                'message', function(e) {
                    if (e.data &&e.data.type === 'term-output') {
                        var data = e.data.data;
                        if (!data || data.length === 0) {
                            return;
                        }
                        window.ptyBuffer += data;
                        Module._webpty_pipe_source_notify(pipeSourcePointer);
                    }
                    /* Handle term-ready: iframe is loaded, send pending resize */
                    if (e.data &&e.data.type === 'term-ready') {
                        window.ptyReady = true;
                        var iframe = document.getElementById('jslinux-pty');
                        if (iframe && iframe.contentWindow) {
                            iframe.contentWindow.postMessage({
                                type : 'term-resize',
                                cols : window.ptyPendingCols,
                                rows : window.ptyPendingRows
                            },
                                                             '*');
                        }
                    }
                });

            /* Create iframe for JSLinux VM */
            var iframe = document.createElement('iframe');
            iframe.id = 'jslinux-pty';
            iframe.style.cssText = 'display:none;';
            iframe.src = 'jslinux/vm-bridge.html?' + 'cols=' + cols + '&rows=' + rows +
                         '&cpu=x86_64&mem=256';
            document.body.appendChild(iframe);
        },
        &pty->pipe_source, pty->cols, pty->rows);

    return YETTY_OK_VOID();
}

/* Create PTY */

static struct yetty_yplatform_pty_result webasm_pty_create(struct yetty_yconfig *config)
{
    struct yetty_yplatform_webasm_pty *pty;
    struct yetty_ycore_void_result res;

    pty = calloc(1, sizeof(struct yetty_yplatform_webasm_pty));
    if (!pty) {
        return YETTY_ERR(yetty_yplatform_pty, "failed to allocate webasm pty");
    }

    pty->base.ops = &webasm_pty_ops;

    res = yetty_yplatform_webasm_pty_init(pty, config);
    if (!YETTY_IS_OK(res)) {
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to init webasm pty");
    }

    return YETTY_OK(yetty_yplatform_pty, &pty->base);
}

/* Factory implementation */

struct yetty_yplatform_webasm_pty_factory {
    struct yetty_platform_pty_factory base;
    struct yetty_yconfig *config;
};

static void webasm_pty_factory_destroy(struct yetty_platform_pty_factory *self)
{
    struct yetty_yplatform_webasm_pty_factory *factory = container_of(self, struct yetty_yplatform_webasm_pty_factory, base);
    free(factory);
}

static struct yetty_yplatform_pty_result webasm_pty_factory_create_pty(
    struct yetty_platform_pty_factory *self, struct yetty_yplatform_event_loop *event_loop)
{
    struct yetty_yplatform_webasm_pty_factory *factory = container_of(self, struct yetty_yplatform_webasm_pty_factory, base);
    (void)event_loop;
    return webasm_pty_create(factory->config);
}

static const struct yetty_platform_pty_factory_ops webasm_pty_factory_ops = {
    .destroy = webasm_pty_factory_destroy,
    .create_pty = webasm_pty_factory_create_pty,
};

/* Factory creation - the public API */

struct yetty_yplatform_pty_factory_result yetty_platform_pty_factory_create(
    struct yetty_yconfig *config, void *os_specific)
{
    struct yetty_yplatform_webasm_pty_factory *factory;

    (void)os_specific;

    factory = calloc(1, sizeof(struct yetty_yplatform_webasm_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory, "failed to allocate webasm pty factory");
    }

    factory->base.ops = &webasm_pty_factory_ops;
    factory->config = config;

    return YETTY_OK(yetty_yplatform_pty_factory, &factory->base);
}

/* C export - called by JS when data arrives in buffer */

EMSCRIPTEN_KEEPALIVE
void yetty_yplatform_webasm_pty_pipe_source_webpty_pipe_source_notify(struct yetty_yplatform_webasm_pty_pipe_source *pipe_source)
{
    if (pipe_source) {
        yetty_yplatform_webasm_pty_pipe_source_notify(pipe_source);
    }
}
