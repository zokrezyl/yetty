/* GENERATED — do not edit. */
/* Object API for regular class(es) `terminal` (implementation module: yterminal).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YTERMINAL_TERMINAL_H
#define YETTY_YCLASSGEN_API_YTERMINAL_TERMINAL_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yterminal_terminal;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YTERMINAL_TERMINAL_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YTERMINAL_TERMINAL_PTR_RESULT
struct yetty_yterminal_terminal_ptr_result {
    int ok;
    union {
        struct yetty_yterminal_terminal *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yterminal_terminal_ptr_result yetty_yterminal_terminal_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_to(struct yetty_yterminal_terminal *data);

/* figure_root_container: navigate from the terminal (the session root a
 * connecting tool receives) to its root figure container. Object-returning
 * wire slot — a remote tool receives a session-bound container proxy; a
 * local caller receives the real container object. */
struct yetty_yclass_object_ptr_result yetty_yterminal_figure_root_container(struct yetty_yclass_object * obj);

struct yetty_yclass_object_ptr_result yetty_yterminal_terminal_create(struct yetty_yclass_ctx *ctx);



#ifdef __cplusplus
}
#endif

#endif
