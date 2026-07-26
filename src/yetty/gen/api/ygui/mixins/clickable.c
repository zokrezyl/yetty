/* GENERATED — do not edit. */
#include <yetty/api/ygui/mixins/clickable.h>

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

struct yetty_ycore_int_result;
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object * obj, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object * obj, float x, float y, int button);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(struct yetty_yclass_object *, float, float, int);

