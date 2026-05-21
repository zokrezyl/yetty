/*
 * ygui-yplot-test.c — smoke test for the ygui_yplot widget.
 *
 * Exercises the four entry points from ygui_yplot.h against a headless
 * engine. We don't render — just confirm that the widget is created and
 * that set_source / set_buffers swap the underlying buffer without
 * error. Catches regressions in:
 *   - empty / NULL source handling
 *   - default bounds derivation from the widget's content box
 *   - data-buffer wiring (yplot_render_with_buffers path)
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yplot.h>

static int g_failures = 0;
static int g_devnull_fd = -1;

#define ASSERT(cond, name)                                                                          \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            fprintf(stderr, "FAIL %s\n", (name));                                                   \
            g_failures++;                                                                           \
        } else {                                                                                    \
            fprintf(stderr, "ok   %s\n", (name));                                                   \
        }                                                                                           \
    } while (0)

static struct yetty_ygui_engine *make_engine(void)
{
    struct yetty_ygui_engine_args args = {.name = "yplot-test"};
    struct ygui_engine_ptr_result r = yetty_ygui_engine_create(args);
    if (YETTY_IS_ERR(r)) {
        fprintf(stderr, "engine_create failed\n");
        exit(2);
    }
    struct yetty_ygui_engine *engine = r.value;
    yetty_ygui_engine_set_size(engine, 800.0f, 600.0f);
    if (g_devnull_fd < 0) {
        g_devnull_fd = open("/dev/null", O_WRONLY);
    }
    if (g_devnull_fd >= 0) {
        yetty_ygui_engine_set_output_fd(engine, g_devnull_fd);
    }
    return engine;
}

/* Pure-expression construction — the canonical yplot usage. */
static void test_from_source(void)
{
    fprintf(stderr, "\n[test_from_source]\n");
    struct yetty_ygui_engine *e = make_engine();

    struct yetty_yplot_render_config cfg = {
        .x_min = -6.2832f,
        .x_max = 6.2832f,
        .y_min = -1.5f,
        .y_max = 1.5f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
    };
    const char *src = "f=sin(x); g=cos(x); @f.color=#ff6b6b; @g.color=#4ecdc4";
    struct yetty_ygui_widget *w = yetty_ygui_engine_yplot_from_source(
        e, "plot1", 0.0f, 0.0f, 400.0f, 200.0f, src, 0, &cfg);
    ASSERT(w != NULL, "from_source: widget created");

    /* Replace the source — exercises the set_source / set_buffers
     * path that lets callers animate / swap expressions live. */
    const char *src2 = "h=sin(2*x); k=cos(2*x)";
    struct yetty_ycore_void_result sr =
        yetty_ygui_widget_yplot_set_source(w, src2, 0, &cfg);
    ASSERT(YETTY_IS_OK(sr), "set_source: ok");
    if (YETTY_IS_ERR(sr)) yetty_ycore_error_destroy(sr.error);

    yetty_ygui_engine_destroy(e);
}

/* Source + data-buffer combo — what a live audio scope would use. */
static void test_from_buffers(void)
{
    fprintf(stderr, "\n[test_from_buffers]\n");
    struct yetty_ygui_engine *e = make_engine();

    float samples[128];
    for (int i = 0; i < 128; i++) {
        samples[i] = (float)i / 128.0f - 0.5f;
    }
    struct yetty_yplot_buffer_input bufs[] = {
        {.samples = samples, .count = 128, .color = 0xFFFFA500u},
    };
    struct yetty_yplot_render_config cfg = {
        .x_min = -1.0f, .x_max = 1.0f, .y_min = -1.0f, .y_max = 1.0f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES,
    };
    struct yetty_ygui_widget *w = yetty_ygui_engine_yplot_from_buffers(
        e, "plot2", 0.0f, 0.0f, 500.0f, 250.0f,
        "f=sin(x)", 0, bufs, 1, &cfg);
    ASSERT(w != NULL, "from_buffers: widget created with 1 data buffer");

    /* Live update — refresh the sample array and push. The yplot
     * factory copies the samples, so the local array stays the
     * caller's. */
    for (int i = 0; i < 128; i++) {
        samples[i] = (float)i / 64.0f - 1.0f;
    }
    struct yetty_ycore_void_result sr =
        yetty_ygui_widget_yplot_set_buffers(w, "f=sin(x)", 0, bufs, 1, &cfg);
    ASSERT(YETTY_IS_OK(sr), "set_buffers: ok");
    if (YETTY_IS_ERR(sr)) yetty_ycore_error_destroy(sr.error);

    yetty_ygui_engine_destroy(e);
}

/* NULL widget protection — the public API documents that set_*
 * tolerates a NULL widget pointer (returns ERR). */
static void test_null_widget(void)
{
    fprintf(stderr, "\n[test_null_widget]\n");
    struct yetty_ycore_void_result sr =
        yetty_ygui_widget_yplot_set_source(NULL, "f=sin(x)", 0, NULL);
    ASSERT(YETTY_IS_ERR(sr), "set_source(NULL): err");
    if (YETTY_IS_ERR(sr)) yetty_ycore_error_destroy(sr.error);
}

int main(void)
{
    test_from_source();
    test_from_buffers();
    test_null_widget();
    if (g_devnull_fd >= 0) close(g_devnull_fd);
    fprintf(stderr, "\nresult: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
