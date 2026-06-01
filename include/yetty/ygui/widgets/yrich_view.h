/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yrich_view` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YRICH_VIEW_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YRICH_VIEW_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yrich/yrich-types.h>

struct yetty_ygui_object;
struct yetty_yrich_document;

/* Attach a yrich document as this view's model. When `own` is non-zero
 * the view destroys the document on replace / widget destroy; otherwise
 * the caller keeps ownership. The view renders the document into a draw
 * list each frame (re-rendering on resize, document-dirty, or
 * invalidate) and blits it via the ydraw_embed base. */
struct yetty_ycore_void_result yetty_ygui_yrich_view_set_document(struct yetty_ygui_object *obj,
                                                                  struct yetty_yrich_document *doc,
                                                                  int own);

/* The attached document, or NULL. Borrowed. */
struct yetty_yrich_document *yetty_ygui_yrich_view_document(const struct yetty_ygui_object *obj);

/* Document logical content size (page/grid/slide extent). Either out
 * pointer may be NULL. Zero when no document is attached. */
struct yetty_ycore_void_result yetty_ygui_yrich_view_content_size(
    const struct yetty_ygui_object *obj, float *w, float *h);

/* Force a re-render at the next emit (after a programmatic / toolbar
 * edit that does not move the rect). */
struct yetty_ycore_void_result yetty_ygui_yrich_view_invalidate(struct yetty_ygui_object *obj);

/* Host-driven keyboard forwarding. ygui has no per-widget key slot, so a
 * host that wants editable text routes its focused-view key / text bytes
 * here; they are forwarded to the document's input handlers. */
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_key(struct yetty_ygui_object *obj,
                                                              uint32_t key,
                                                              struct yetty_yrich_input_mods mods);
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_text(struct yetty_ygui_object *obj,
                                                               const char *text, size_t len);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
