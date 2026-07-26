/* ygui-ypdf.c — opens a PDF via pdfio, renders to drawable_list. */
#include "../internal.h"

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * ypdf.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_ypdf_ptr, struct yetty_ygui_ypdf *);
struct yetty_yclass_ptr_result yetty_ygui_ypdf_class_get(void);
struct yetty_ygui_ypdf_ptr_result yetty_ygui_ypdf_from(struct yetty_yclass_object *obj);
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/widgets/ydraw_embed.h>
#if YETTY_YGUI_HAVE_YPDF
#include <pdfio.h>
#include <yetty/ypdf/ypdf.h>
#endif

/* ypdf adds no ops or state on top of ydraw_embed; the class exists only
 * so callers can name it in yetty_ygui_add. The 1-byte slice is unused. */
struct YETTY_ANNOTATE("class@ygui:ypdf") YETTY_ANNOTATE("parent@ygui:ydraw_embed") yetty_ygui_ypdf {
    char unused;
};

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_ypdf_set_file(struct yetty_yclass_object *obj,
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

#include "yetty/gen/impl/ygui/widgets/ypdf.c"
