/*
 * yrich-shell.h — ygui-decorated editor shells for the three yrich
 * document kinds.
 *
 * Each builder assembles a complete editor surface out of ygui chrome —
 * a toolbar (labelled buttons), the document hosted in a scrollarea via
 * the ygui yrich_view widget, and a statusbar — and returns the pieces
 * the host needs to mount and drive it. The widgets are plain ygui
 * objects; the host owns their lifetime through the widget tree.
 *
 * These are convenience composers, not yclass classes: they sit on top
 * of the public ygui widget API (the yrich_view content widget) and the
 * yrich document library. They live in the yrich module because they are
 * yrich-app code, not generic ygui chrome.
 */
#ifndef YETTY_YRICH_YRICH_SHELL_H
#define YETTY_YRICH_YRICH_SHELL_H

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_object;
struct yetty_yrich_document;

/* The widgets a shell builder produces. `root` is the editor's top
 * container (attach it under the engine root, or pass it as the root
 * itself). `view` is the ygui yrich_view hosting the document; `doc` is
 * the yrich document it owns (borrowed convenience pointer — destroyed
 * with the view). `toolbar` / `statusbar` are exposed so the host can
 * extend the chrome or update status text. */
struct yetty_yrich_editor {
    struct yetty_yclass_object *root;
    struct yetty_yclass_object *toolbar;
    struct yetty_yclass_object *view;
    struct yetty_yclass_object *statusbar;
    struct yetty_yrich_document *doc;
};

/* Rich-text editor: a formatting toolbar over a scrolling document
 * surface with a statusbar. Creates an empty ydoc; populate it via the
 * yrich ydoc API on `out->doc` (cast to struct yetty_yrich_ydoc *). */
struct yetty_ycore_void_result yetty_yrich_ydoc_editor_create(struct yetty_yclass_object *parent,
                                                              struct yetty_yrich_editor *out);

/* Spreadsheet editor: an edit toolbar over a scrolling grid. Creates a
 * default-sized spreadsheet on `out->doc`. */
struct yetty_ycore_void_result yetty_yrich_ysheet_editor_create(struct yetty_yclass_object *parent,
                                                                struct yetty_yrich_editor *out);

/* Slide editor: a navigation toolbar (prev / next / add) over a
 * scrolling slide canvas. Creates a deck with one slide on `out->doc`. */
struct yetty_ycore_void_result yetty_yrich_yslide_editor_create(struct yetty_yclass_object *parent,
                                                                struct yetty_yrich_editor *out);

/* Refresh the statusbar + re-fit the view to the document's current
 * content size. Call after a host mutates the document so the chrome and
 * scroll extent track the new content. */
struct yetty_ycore_void_result yetty_yrich_editor_refresh(struct yetty_yrich_editor *editor);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRICH_YRICH_SHELL_H */
