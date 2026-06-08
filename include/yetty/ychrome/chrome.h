/* GENERATED — do not edit. */
/* Public interface for regular class(es) `chrome` (module: ychrome).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YCHROME_CHROME_H
#define YETTY_YCLASSGEN_YCHROME_CHROME_H

#include <yetty/yclass/class.h>
#include <yetty/ychrome/methods.h>

struct yetty_yclass_ptr_result yetty_ychrome_chrome_class_get(void);

/* Feature flags for configure(). OR them together; FLAG_ALL enables the lot.
 * Copied verbatim into the generated chrome.h. */
#define YETTY_YCHROME_FLAG_DRAG 0x1u     /* caption drag moves the window      */
#define YETTY_YCHROME_FLAG_RESIZE 0x2u   /* right/bottom edges resize          */
#define YETTY_YCHROME_FLAG_MAXIMIZE 0x4u /* caption double-click toggles max   */
#define YETTY_YCHROME_FLAG_ALL                                                                     \
    (YETTY_YCHROME_FLAG_DRAG | YETTY_YCHROME_FLAG_RESIZE | YETTY_YCHROME_FLAG_MAXIMIZE)
struct yetty_ycore_int_result yetty_ychrome_hover_button(struct yetty_yclass_object *obj);

#endif
