/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yshadertoy` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YSHADERTOY_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YSHADERTOY_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_yshadertoy_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Set the widget's WGSL shader text. The widget hosts a
 * YETTY_YFIGURE_KIND_YSHADERTOY child figure and ships this source to it
 * as a figure body; the figure injects iResolution/iTime/iTimeDelta/
 * iFrame/iMouse and runs the user's mainImage(...). Re-shipped only when
 * the source changes (the figure recompiles on receipt), so setting it
 * once is cheap. See yetty/yshadertoy/figure.h for the WGSL contract. */
struct yetty_ycore_void_result yetty_ygui_yshadertoy_set_source(struct yetty_ygui_object *obj,
                                                                const char *src, size_t len);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
