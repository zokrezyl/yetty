/*
 * ygui_rich_yaml.c — YAML convenience layer for the RICH widget.
 *
 * Split out of ygui_rich.c so the libygui.so variant (FFI-friendly,
 * minimal deps) can skip linking yetty_ypaint_yaml (which transitively
 * pulls libyaml and fontconfig). The static libygui.a always includes
 * this TU; the shared variant excludes it via CMakeLists.txt.
 *
 * Implements:
 *   yetty_ygui_widget_rich_set_yaml — parse YAML via ypaint-yaml and hand
 *                                      the resulting buffer to the
 *                                      widget (drops any previous buffer)
 *   yetty_ygui_engine_rich_from_yaml — sugar: rich() + set_yaml()
 */

#include "ygui_internal.h"

#include <yetty/ypaint-core/buffer.h>
#include <yetty/ypaint-yaml/ypaint-yaml.h>

struct yetty_ycore_void_result yetty_ygui_widget_rich_set_yaml(struct yetty_ygui_widget *widget,
                                                               const char *yaml, size_t yaml_len)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_RICH) {
        return YETTY_ERR(yetty_ycore_void, "rich_set_yaml: not a rich widget");
    }
    if (!yaml || yaml_len == 0) {
        yetty_ygui_widget_rich_clear(widget);
        return YETTY_OK_VOID();
    }
    struct yetty_ypaint_core_buffer_result pr = yetty_ypaint_yaml_parse(yaml, yaml_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "rich_set_yaml: yaml parse failed");
    yetty_ygui_widget_rich_set_buffer(widget, pr.value);
    return YETTY_OK_VOID();
}

struct yetty_ygui_widget *yetty_ygui_engine_rich_from_yaml(struct yetty_ygui_engine *engine,
                                                           const char *id, float x, float y, float w,
                                                           float h, const char *yaml,
                                                           size_t yaml_len)
{
    struct yetty_ygui_widget *r = yetty_ygui_engine_rich(engine, id, x, y, w, h);
    if (!r) {
        return NULL;
    }
    struct yetty_ycore_void_result sr = yetty_ygui_widget_rich_set_yaml(r, yaml, yaml_len);
    if (YETTY_IS_ERR(sr)) {
        yetty_ygui_set_error(sr.error.msg ? sr.error.msg : "rich_from_yaml: parse failed");
        yetty_ycore_error_destroy(sr.error);
    }
    return r;
}
