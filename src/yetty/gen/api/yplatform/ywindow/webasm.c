/* GENERATED — do not edit. */
#include <yetty/api/yplatform/ywindow/webasm.h>

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
struct yetty_ycore_void_result yetty_yplatform_window_open(struct yetty_yclass_object *obj,
                                                           int width, int height,
                                                           const char *title);
struct yetty_ycore_void_result yetty_yplatform_window_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_get_size(struct yetty_yclass_object *obj,
                                                               int *width, int *height);
struct yetty_ycore_void_result yetty_yplatform_window_get_framebuffer_size(
    struct yetty_yclass_object *obj, int *width, int *height);
struct yetty_ycore_void_result yetty_yplatform_window_get_content_scale(
    struct yetty_yclass_object *obj, float *xscale, float *yscale);
struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_set_title(struct yetty_yclass_object *obj,
                                                                const char *title);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_open_fn)(
    struct yetty_yclass_object *, int, int, const char *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_size_fn)(
    struct yetty_yclass_object *, int *, int *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_framebuffer_size_fn)(
    struct yetty_yclass_object *, int *, int *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_content_scale_fn)(
    struct yetty_yclass_object *, float *, float *);
typedef struct yetty_ycore_int_result (*yetty_yplatform_window_should_close_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_set_title_fn)(
    struct yetty_yclass_object *, const char *);
