/* GENERATED — do not edit. */
#include <yetty/api/ygui2/widgets/dropdown.h>

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
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_ygui2_widget_paint(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_drawable_list *list);
struct yetty_ycore_int_result yetty_ygui2_widget_on_press(struct yetty_yclass_object *obj,
                                                          float local_x, float local_y, int button,
                                                          int mods);
struct yetty_ycore_void_result yetty_ygui2_widget_cleanup(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_paint_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_press_fn)(
    struct yetty_yclass_object *, float, float, int, int);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_cleanup_fn)(
    struct yetty_yclass_object *);
