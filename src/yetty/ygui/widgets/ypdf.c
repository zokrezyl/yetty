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
 * exists only so callers can `yetty_ygui_add(yetty_ygui_ypdf_class_get().value, …)`
 * symbolically. Hand-written because YETTY_YGUI_DEFINE_CLASS computes the
 * ops count via sizeof and so requires at least one op entry. */
static const struct yetty_yclass_descriptor ypdf_desc = {
    .name = "yetty_ygui_ypdf",
    .type = YETTY_YCLASS_TYPE_REGULAR,
    .data_size = 0,
};

struct yetty_yclass_ptr_result yetty_ygui_ypdf_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    struct yetty_yclass_ptr_result _pr = yetty_ygui_ydraw_embed_class_get();
    if (YETTY_IS_ERR(_pr)) {
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_ypdf_class_get: parent failed", _pr);
    }
    struct yetty_yclass_ptr_result r = yetty_yclass_register(
        &ypdf_desc, NULL, 0, _pr.value, NULL, 0);
    if (YETTY_IS_OK(r)) cls = r.value;
    return r;
}
