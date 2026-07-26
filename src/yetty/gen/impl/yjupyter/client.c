/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* calloc/free for proxy + buffer marshalling */
#include <string.h>  /* memcpy/strcmp/strlen */

struct yetty_yclass_object_ptr_result;
struct yetty_ycore_char_ptr_result;
struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yjupyter_client_open(struct yetty_yclass_object * obj, const char * base_url, const char * token);
struct yetty_ycore_char_ptr_result yetty_yjupyter_client_execute(struct yetty_yclass_object * obj, const char * code, const char * tag);
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_poll(struct yetty_yclass_object * obj, int timeout_ms);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_kernel_state(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_tag_for(struct yetty_yclass_object * obj, const char * parent_msg_id);
struct yetty_ycore_void_result yetty_yjupyter_client_close(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yjupyter_client_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_open_fn)(struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_client_execute_fn)(struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_yclass_object_ptr_result (*yetty_yjupyter_client_poll_fn)(struct yetty_yclass_object *, int);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_client_kernel_state_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_client_tag_for_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_close_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_open_fn yetty_yjupyter_client_yetty_yjupyter_client_open_check = client_open;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_execute_fn yetty_yjupyter_client_yetty_yjupyter_client_execute_check = client_execute;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_poll_fn yetty_yjupyter_client_yetty_yjupyter_client_poll_check = client_poll;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_kernel_state_fn yetty_yjupyter_client_yetty_yjupyter_client_kernel_state_check = client_kernel_state;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_tag_for_fn yetty_yjupyter_client_yetty_yjupyter_client_tag_for_check = client_tag_for;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_close_fn yetty_yjupyter_client_yetty_yjupyter_client_close_check = client_close;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_client_destroy_fn yetty_yjupyter_client_yetty_yjupyter_client_destroy_check = client_destroy;

struct yetty_yclass_ptr_result yetty_yjupyter_client_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yjupyter_client");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yjupyter_client",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yjupyter_client),
        .data_align = _Alignof(struct yetty_yjupyter_client),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yjupyter", "client_open", (yetty_yclass_method_id_t)yetty_yjupyter_client_open, (yetty_yclass_impl_t)client_open},
        {"yetty_yjupyter", "client_execute", (yetty_yclass_method_id_t)yetty_yjupyter_client_execute, (yetty_yclass_impl_t)client_execute},
        {"yetty_yjupyter", "client_poll", (yetty_yclass_method_id_t)yetty_yjupyter_client_poll, (yetty_yclass_impl_t)client_poll},
        {"yetty_yjupyter", "client_kernel_state", (yetty_yclass_method_id_t)yetty_yjupyter_client_kernel_state, (yetty_yclass_impl_t)client_kernel_state},
        {"yetty_yjupyter", "client_tag_for", (yetty_yclass_method_id_t)yetty_yjupyter_client_tag_for, (yetty_yclass_impl_t)client_tag_for},
        {"yetty_yjupyter", "client_close", (yetty_yclass_method_id_t)yetty_yjupyter_client_close, (yetty_yclass_impl_t)client_close},
        {"yetty_yjupyter", "client_destroy", (yetty_yclass_method_id_t)yetty_yjupyter_client_destroy, (yetty_yclass_impl_t)client_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yjupyter_client_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yjupyter_client_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yjupyter_client_ptr_result yetty_yjupyter_client_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_client_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yjupyter_client_ptr, "yetty_yjupyter_client_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yjupyter_client_ptr, "yetty_yjupyter_client_from: object_data", slice_r);
    return YETTY_OK(yetty_yjupyter_client_ptr, (struct yetty_yjupyter_client *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_client_to(struct yetty_yjupyter_client *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_client_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yjupyter_client_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yjupyter_client_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_yclass_object_ptr_result yetty_yjupyter_client_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yjupyter_client");
    if (ctx && ctx->session)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_client_create: remote create unsupported for a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yjupyter_client_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_client_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r =
        yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) return alloc_r;
    return alloc_r;
}


/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_yjupyter_client_class_get(void);
struct yetty_ycore_void_result yetty_yjupyter_client_register(void);

/* ---- yjupyter_client: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yjupyter_client_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yjupyter_client") == 0)
        return yetty_yjupyter_client_class_get();
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yjupyter_client: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yjupyter_client_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yjupyter_client_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yjupyter_client_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_ycore_void_result yetty_yjupyter_client_register(void);
struct yetty_ycore_void_result yetty_yjupyter_protocol_register(void);
struct yetty_ycore_void_result yetty_yjupyter_register(void);


/* ---- yjupyter: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yjupyter_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yjupyter_client_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yjupyter_register: submodule yjupyter_client");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yjupyter_protocol_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yjupyter_register: submodule yjupyter_protocol");
    }
    registered = true;
    return YETTY_OK_VOID();
}
