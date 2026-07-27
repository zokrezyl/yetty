/* GENERATED — do not edit. */
#include <yetty/api/yai/editor-vi.h>

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

struct yai_app;
struct yetty_ycore_int_result;
struct yetty_ycore_int_result yetty_yai_feed_byte(struct yetty_yclass_object *obj,
                                                  struct yai_app *app, int byte);
typedef struct yetty_ycore_int_result (*yetty_yai_feed_byte_fn)(struct yetty_yclass_object *,
                                                                struct yai_app *, int);
