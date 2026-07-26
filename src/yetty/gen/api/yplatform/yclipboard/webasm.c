/* GENERATED — do not edit. */
#include <yetty/api/yplatform/yclipboard/webasm.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* malloc/free for buffer marshalling */
#include <string.h>  /* memcpy/strlen */

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yplatform_clipboard_set_text(struct yetty_yclass_object * obj, const char * text, size_t len);
struct yetty_ycore_void_result yetty_yplatform_clipboard_request_paste(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_set_text_fn)(struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_request_paste_fn)(struct yetty_yclass_object *);

