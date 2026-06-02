/* ygui-ypdf.c — opens a PDF via pdfio, renders to draw_list. */
#include "../internal.h"
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/widgets/ydraw_embed.h>
#include <yetty/ygui/widgets/ypdf.h>
#if YETTY_YGUI_HAVE_YPDF
#include <pdfio.h>
#include <yetty/ypdf/ypdf.h>
#endif

/* ypdf adds no ops or state on top of ydraw_embed; the class exists only
 * so callers can name it in yetty_ygui_add. The 1-byte slice is unused. */
struct [[clang::annotate("class@ygui:ypdf")]] [[clang::annotate("parent@ygui:ydraw_embed")]]
ypdf_data {
    char unused;
};

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_ypdf_set_file(struct yetty_ygui_object *obj,
                                                        const char *path)
{
    if (!obj || !path) {
        return YETTY_ERR(yetty_ycore_void, "ypdf_set_file: NULL");
    }
#if YETTY_YGUI_HAVE_YPDF
    pdfio_file_t *pdf = pdfioFileOpen(path, NULL, NULL, NULL, NULL);
    if (!pdf) {
        return YETTY_ERR(yetty_ycore_void, "ypdf_set_file: open");
    }
    struct yetty_ypdf_render_result rr = yetty_ypdf_render_pdf(pdf);
    pdfioFileClose(pdf);
    if (YETTY_IS_ERR(rr)) {
        return YETTY_ERR(yetty_ycore_void, "ypdf_set_file: render", rr);
    }
    return yetty_ygui_ydraw_embed_set_buffer(obj, rr.value.buffer);
#else
    return YETTY_ERR(yetty_ycore_void, "ypdf_set_file: PDF support not built on this platform");
#endif
}

#include "ypdf.gen.c"
