/* GENERATED — do not edit. */
#include <yetty/api/yplatform/ywindow-chrome/glfw.h>

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
struct yetty_yui_event;
struct yetty_ycore_void_result yetty_yplatform_window_chrome_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_handle_event(struct yetty_yclass_object * obj, const struct yetty_yui_event * event);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_handle_event_fn)(struct yetty_yclass_object *, const struct yetty_yui_event *);

