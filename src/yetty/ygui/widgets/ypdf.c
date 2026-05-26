/* ygui-ypdf.c — opens a PDF via pdfio, renders to draw_list. */
#include "../internal.h"
#include <pdfio.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/ygui/widgets/ypdf.h>
#include <yetty/ypdf/ypdf.h>

struct yetty_ycore_void_result yetty_ygui_ypdf_set_file(struct yetty_ygui_object *obj,
                                                        const char *path)
{
    if (!obj || !path) return YETTY_ERR(yetty_ycore_void, "ypdf_set_file: NULL");
    pdfio_file_t *pdf = pdfioFileOpen(path, NULL, NULL, NULL, NULL);
    if (!pdf) return YETTY_ERR(yetty_ycore_void, "ypdf_set_file: open");
    struct yetty_ypdf_render_result rr = yetty_ypdf_render_pdf(pdf);
    pdfioFileClose(pdf);
    if (YETTY_IS_ERR(rr)) return YETTY_ERR(yetty_ycore_void, "ypdf_set_file: render", rr);
    return yetty_ygui_ydraw_embed_set_buffer(obj, rr.value.buffer);
}

/* ypdf adds zero ops on top of ydraw_embed — the public class accessor
 * exists only so callers can `yetty_ygui_add(yetty_ygui_ypdf_class_get(), …)`
 * symbolically. Hand-written because YETTY_YGUI_DEFINE_CLASS computes the
 * ops count via sizeof and so requires at least one op entry. */
static const struct yetty_ygui_class_descriptor ypdf_desc = {
    .name = "yetty_ygui_ypdf",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = 0,
};

const struct yetty_ygui_class *yetty_ygui_ypdf_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (cls) return cls;
    struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
        &ypdf_desc, NULL, 0, yetty_ygui_ydraw_embed_class_get(), NULL, 0);
    if (YETTY_IS_ERR(r)) return NULL;
    cls = r.value;
    return cls;
}
