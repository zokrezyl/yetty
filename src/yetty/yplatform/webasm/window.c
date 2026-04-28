/* WebAssembly window.c - Canvas element setup and management
 *
 * On WebAssembly, the "window" is an HTML canvas element. This file handles:
 * - Reading the canvas container dimensions
 * - Setting the canvas element size
 * - Providing dimension/scale queries
 *
 * Single-threaded: all operations happen on the main (only) thread.
 */

#include <yetty/yconfig.h>
#include <yetty/ytrace.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <string.h>

/* Canvas state */
static int g_canvas_width = 0;
static int g_canvas_height = 0;

int yetty_yplatform_webasm_create_window(struct yetty_yconfig *config)
{
    int default_width;
    int default_height;
    const char *title;
    int container_w;
    int container_h;

    default_width = config->ops->get_int(config, "window/width", 1280);
    default_height = config->ops->get_int(config, "window/height", 720);
    title = config->ops->get_string(config, "window/title", "yetty");

    /* Use actual canvas container size if available, fall back to config defaults */
    container_w = EM_ASM_INT(
        {
            var c = document.getElementById('canvas-container');
            return c ? Math.floor(c.getBoundingClientRect().width) : $0;
        },
        default_width);

    container_h = EM_ASM_INT(
        {
            var c = document.getElementById('canvas-container');
            return c ? Math.floor(c.getBoundingClientRect().height) : $0;
        },
        default_height);

    g_canvas_width = (container_w > 0) ? container_w : default_width;
    g_canvas_height = (container_h > 0) ? container_h : default_height;

    /* Set canvas element size */
    emscripten_set_canvas_element_size("#canvas", g_canvas_width, g_canvas_height);

    /* Set document title */
    EM_ASM({ document.title = UTF8ToString($0); }, title);

    ydebug("window: Canvas created %dx%d \"%s\"", g_canvas_width, g_canvas_height, title);
    return 1;
}

void yetty_yplatform_webasm_destroy_window(void)
{
    /* Nothing to destroy on web */
    ydebug("window: Destroyed (no-op on web)");
}

void yetty_yplatform_webasm_get_window_size(int *width, int *height)
{
    *width = g_canvas_width;
    *height = g_canvas_height;
}

void yetty_yplatform_webasm_get_framebuffer_size(int *width, int *height)
{
    /* On web, framebuffer size equals canvas size (device pixel ratio is separate) */
    *width = g_canvas_width;
    *height = g_canvas_height;
}

void yetty_yplatform_webasm_get_content_scale(float *xscale, float *yscale)
{
    double ratio = emscripten_get_device_pixel_ratio();
    *xscale = (float)ratio;
    *yscale = (float)ratio;
}

int yetty_yplatform_webasm_should_close(void)
{
    return 0; /* Web apps don't "close" */
}

void yetty_yplatform_webasm_set_title(const char *title)
{
    EM_ASM({ document.title = UTF8ToString($0); }, title);
}

/* Called when canvas container is resized (browser window resize, devtools open/close)
 * Updates internal dimensions and canvas element size.
 * Returns 1 if size actually changed, 0 otherwise.
 */
int yetty_yplatform_webasm_update_canvas_size(void)
{
    int width;
    int height;

    width = EM_ASM_INT({
        var container = document.getElementById('canvas-container');
        return container ? Math.floor(container.getBoundingClientRect().width) : window.innerWidth;
    });

    height = EM_ASM_INT({
        var container = document.getElementById('canvas-container');
        return container ? Math.floor(container.getBoundingClientRect().height)
                         : window.innerHeight;
    });

    if (width <= 0 || height <= 0) {
        return 0;
    }
    if (width == g_canvas_width && height == g_canvas_height) {
        return 0;
    }

    g_canvas_width = width;
    g_canvas_height = height;
    emscripten_set_canvas_element_size("#canvas", width, height);

    ydebug("window: Canvas resized to %dx%d", width, height);
    return 1;
}
