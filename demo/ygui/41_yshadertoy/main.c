/*
 * Demo 41_yshadertoy: yshadertoy — a tabbar of Shadertoy-style WGSL
 * shaders rendered via the ygui yshadertoy widget →
 * YETTY_YFIGURE_KIND_YSHADERTOY figure.
 *
 * Each tab swaps the widget's WGSL source; the host-side figure injects
 * the usual Shadertoy inputs (iResolution / iTime / iTimeDelta / iFrame /
 * iMouse) and runs mainImage() on a full-rect quad, animating itself.
 *
 * The shader gallery (Mandelbrot / Julia / Voronoi / Plasma / Spiral) is
 * the shared set in <yetty/yshadertoy/demo-shaders.h>, adapted from
 * yetty's shader-glyph fractal/complex shaders.
 *
 * Standalone-mode ygui demo. The runner brings up window + GPU +
 * receiver-side container; this file only populates the widget tree.
 * Press 'q' (or Ctrl-C / Ctrl-D) to quit.
 */

#include <string.h>

#include <yetty/yguiapp/app.h>
#include <yetty/yguiapp/run.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/widgets/yshadertoy.h>
#include <yetty/yshadertoy/demo-shaders.h>

/* Single demo instance — the tab handler reaches the shader widget here. */
static struct yetty_yclass_object *g_shader_widget;

static void apply_shader(int idx)
{
    if (idx < 0 || idx >= yetty_yshadertoy_demo_shader_count || !g_shader_widget) {
        return;
    }
    const char *wgsl = yetty_yshadertoy_demo_shaders[idx].wgsl;
    struct yetty_ycore_void_result r =
        yetty_ygui_yshadertoy_set_source(g_shader_widget, wgsl, strlen(wgsl));
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

static struct yetty_ycore_void_result on_tab_change(struct yetty_yclass_object *obj,
                                                    const struct yetty_ygui_event *event,
                                                    void *userdata)
{
    (void)obj;
    (void)userdata;
    apply_shader(event->i0);
    return YETTY_OK_VOID();
}

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct [[clang::annotate("class@demoygui:41_yshadertoy")]] [[clang::annotate("parent@yguiapp:app")]]
yetty_demoygui_41_yshadertoy {
    int unused;
};

/* Result wrapper + class accessor forward-decls (this TU does not include its
 * own generated header; main.gen.c is #included at the foot). */
YETTY_YRESULT_DECLARE(yetty_demoygui_41_yshadertoy_ptr, struct yetty_demoygui_41_yshadertoy *);
struct yetty_yclass_ptr_result yetty_demoygui_41_yshadertoy_class_get(void);

[[clang::annotate("override@yguiapp:app:build")]]
static struct yetty_ycore_void_result build(struct yetty_yclass_object *app,
                                            struct yetty_yclass_object *root)
{
    (void)app;

    /* Vertical stack: tabbar on top, shader fills the rest. */
    struct yetty_yclass_object_ptr_result vr =
        yetty_ygui_widget_add(root, yetty_ygui_vbox_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vr, "vbox");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(vr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "41_yshadertoy: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        l.gap = 6.0f;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(vr.value, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "vbox layout");
    }

    /* Tabbar — one tab per shader. */
    struct yetty_yclass_object_ptr_result tr =
        yetty_ygui_widget_add(vr.value, yetty_ygui_tabbar_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "tabbar");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(tr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "41_yshadertoy: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.height = 34.0f;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(tr.value, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "tabbar layout");
    }
    for (int i = 0; i < yetty_yshadertoy_demo_shader_count; i++) {
        struct yetty_yclass_object_ptr_result hr =
            yetty_ygui_tabbar_add_tab(tr.value, yetty_yshadertoy_demo_shaders[i].label);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "tabbar add_tab");
    }

    /* Shader widget — fills the remaining space. */
    struct yetty_yclass_object_ptr_result sr =
        yetty_ygui_widget_add(vr.value, yetty_ygui_yshadertoy_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "yshadertoy");
    {
        struct yetty_ygui_layout_const_ptr_result layout_res =
            yetty_ygui_widget_layout_get(sr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "41_yshadertoy: layout_get");
        struct yetty_ygui_layout l = *layout_res.value;
        l.flex_grow = 1.0f;
        l.min_height = 300.0f;
        struct yetty_ycore_void_result r = yetty_ygui_widget_layout_set(sr.value, &l);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "yshadertoy layout");
    }
    g_shader_widget = sr.value;

    /* Switch the shader source whenever the active tab changes, and seed
     * with the first shader. */
    struct yetty_ycore_void_result subr =
        yetty_ygui_widget_subscribe(tr.value, YETTY_YGUI_EVENT_VALUE_CHANGED, on_tab_change, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, subr, "tabbar subscribe");
    apply_shader(0);
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    return yetty_yguiapp_run_main(argc, argv, yetty_demoygui_41_yshadertoy_class_get().value);
}

#include "main.gen.c"
