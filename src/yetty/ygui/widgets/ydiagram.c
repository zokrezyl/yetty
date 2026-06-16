/*
 * ydiagram.c — wraps the Mermaid → ydraw renderer into a ydraw_embed.
 *
 * Subclass of ydraw_embed (a chrome widget): set_source parses the
 * Mermaid text, lays it out, renders it to a ydraw buffer and hands that
 * buffer to ydraw_embed, which paints the diagram's primitives into the
 * shared chrome ygrid translated by the widget's rect.min. Being chrome,
 * the diagram is skipped (and so disappears) whenever its subtree is
 * hidden — e.g. a folded collapsing_header — exactly like every other
 * chrome widget. (A figure-backed widget would leave a stale receiver-
 * side figure behind on collapse; ypdf/ymarkdown take this same embed
 * route for the same reason.)
 *
 * A diagram's layout has an intrinsic size that does not depend on the
 * widget rect, so it is rendered once here rather than re-flowed per
 * frame (ypdf renders the same way on set_file).
 *
 * The ydiagram renderer prepends a CMD_ZERO (clear-canvas) primitive to
 * every buffer for its standalone-pane use; ydraw_embed skips that prim
 * when inlining so it can't wipe the shared ygrid.
 *
 * Label widths use ydiagram's built-in heuristic (no measure callback):
 * the diagram text is drawn by the receiver's own MSDF font, so node
 * boxes may be slightly loose/tight — the same trade-off the ydiagram CLI
 * makes with --no-font. Wiring a real measure callback would pull the
 * MSDF font loader into ygui and is left for later.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * ydiagram.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_ydiagram_ptr, struct yetty_ygui_ydiagram *);
struct yetty_yclass_ptr_result yetty_ygui_ydiagram_class_get(void);
struct yetty_ygui_ydiagram_ptr_result yetty_ygui_ydiagram_from(struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/widgets/ydraw_embed.h>

#if YETTY_YGUI_HAVE_YDIAGRAM
#include <yetty/ydiagram/ydiagram.h>
#endif

#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@ygui:ydiagram")]] [[clang::annotate("parent@ygui:ydraw_embed")]]
yetty_ygui_ydiagram {
    char *source; /* Mermaid text, owned (NUL-terminated); for get_source. */
    size_t source_len;
};

[[clang::annotate("override@ygui:ydiagram:constructor")]]
static struct yetty_ycore_void_result ydiagram_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_ydiagram_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "ydiagram_constructor: super");
    struct yetty_ygui_ydiagram_ptr_result d_dr = yetty_ygui_ydiagram_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ydiagram_constructor: data_get");
    struct yetty_ygui_ydiagram *d = d_dr.value;
    d->source = NULL;
    d->source_len = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:ydiagram:destructor")]]
static struct yetty_ycore_void_result ydiagram_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_ydiagram_ptr_result d_dr = yetty_ygui_ydiagram_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ydiagram_destructor: data_get");
    struct yetty_ygui_ydiagram *d = d_dr.value;
    free(d->source);
    d->source = NULL;
    /* The rendered buffer is owned by ydraw_embed; its destructor frees it. */
    return yetty_ygui_super_void(obj, yetty_ygui_ydiagram_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ydiagram_set_source(struct yetty_yclass_object *obj,
                                                              const char *source)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "ydiagram_set_source: NULL obj");
    }
    struct yetty_ygui_ydiagram_ptr_result d_dr = yetty_ygui_ydiagram_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_ydiagram_set_source: data_get");
    struct yetty_ygui_ydiagram *d = d_dr.value;

    /* Drop the previous source; clearing the embedded buffer too keeps a
     * stale diagram from lingering if the new source is empty or fails. */
    free(d->source);
    d->source = NULL;
    d->source_len = 0;

    if (!source || !*source) {
        return yetty_ygui_ydraw_embed_set_buffer(obj, NULL);
    }

    size_t n = strlen(source);
    d->source = malloc(n + 1);
    if (!d->source) {
        return YETTY_ERR(yetty_ycore_void, "ydiagram_set_source: source malloc");
    }
    memcpy(d->source, source, n + 1);
    d->source_len = n;

#if YETTY_YGUI_HAVE_YDIAGRAM
    struct yetty_ydiagram_buffer_result br =
        yetty_ydiagram_render_mermaid(d->source, d->source_len);
    if (YETTY_IS_ERR(br)) {
        /* Keep the source (a getter can still surface it) but show nothing;
         * report the parse/layout failure to the caller. */
        struct yetty_ycore_void_result cr = yetty_ygui_ydraw_embed_set_buffer(obj, NULL);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
        return YETTY_ERR(yetty_ycore_void, "ydiagram_set_source: render", br);
    }

    /* Negotiate the widget's box from the diagram's intrinsic extent: the
     * flex layout has no content-sizing, and ydraw_embed paints the diagram
     * 1:1 (no scale-to-fit), so a wrong height clips it or leaves it
     * overflowing the container. The diagram's scene bounds are its true
     * pixel footprint; size the widget to match so the layout reserves the
     * right space and the diagram fits exactly. */
    float dw = yetty_ydraw_drawable_list_scene_max_x(br.value) -
               yetty_ydraw_drawable_list_scene_min_x(br.value);
    float dh = yetty_ydraw_drawable_list_scene_max_y(br.value) -
               yetty_ydraw_drawable_list_scene_min_y(br.value);
    if (dw > 0.0f && dh > 0.0f) {
        struct yetty_ygui_layout layout = *yetty_ygui_widget_layout_get(obj);
        layout.width = dw;
        layout.height = dh;
        struct yetty_ycore_void_result lr = yetty_ygui_widget_layout_set(obj, &layout);
        if (YETTY_IS_ERR(lr)) {
            yetty_ycore_error_destroy(lr.error);
        }
    }

    /* ydraw_embed takes ownership of the buffer (frees it on next replace
     * or on destruction) and marks the widget dirty. */
    return yetty_ygui_ydraw_embed_set_buffer(obj, br.value);
#else
    return YETTY_ERR(yetty_ycore_void,
                     "ydiagram_set_source: diagram support not built on this platform");
#endif
}

[[clang::annotate("expose")]]
struct yetty_ycore_const_char_ptr_result yetty_ygui_ydiagram_get_source(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr,
                         "yetty_ygui_ydiagram_get_source: invalid args");
    }
    struct yetty_ygui_ydiagram_ptr_result d_dr =
        yetty_ygui_ydiagram_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, d_dr,
                        "yetty_ygui_ydiagram_get_source: data_get");
    struct yetty_ygui_ydiagram *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, d->source);
}

#include "ydiagram.gen.c"
