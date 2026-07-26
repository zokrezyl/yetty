/* GENERATED — do not edit. */
#include <yetty/api/yai/turn-engine.h>

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

struct yai_app;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_object * obj, struct yai_app * app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_object * obj, struct yai_app * app);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_exit_fn)(struct yetty_yclass_object *, struct yai_app *, int64_t);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_eof_fn)(struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_interrupt_fn)(struct yetty_yclass_object *, struct yai_app *);

