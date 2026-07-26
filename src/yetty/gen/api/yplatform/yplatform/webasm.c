/* GENERATED — do not edit. */
#include <yetty/api/yplatform/yplatform/webasm.h>

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
struct yetty_ycore_void_result yetty_yplatform_platform_init(struct yetty_yclass_object * obj, struct yetty_yclass_object * app, int argc, char ** argv);
struct yetty_ycore_void_result yetty_yplatform_platform_run(struct yetty_yclass_object * obj, struct yetty_yclass_object * app, int argc, char ** argv);
typedef struct yetty_ycore_void_result (*yetty_yplatform_platform_init_fn)(struct yetty_yclass_object *, struct yetty_yclass_object *, int, char **);
typedef struct yetty_ycore_void_result (*yetty_yplatform_platform_run_fn)(struct yetty_yclass_object *, struct yetty_yclass_object *, int, char **);

