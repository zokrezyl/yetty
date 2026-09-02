/* GENERATED — do not edit. */
#include <yetty/api/ygui2/widgets/scrollarea.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* malloc/free for buffer marshalling */
#include <string.h> /* memcpy/strlen */

struct yetty_ycore_int_result;
struct yetty_ycore_int_result yetty_ygui2_widget_on_scroll(struct yetty_yclass_object *obj,
                                                           float local_x, float local_y,
                                                           float wheel_dy);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_scroll_fn)(
    struct yetty_yclass_object *, float, float, float);
