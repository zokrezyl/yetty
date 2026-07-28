/* GENERATED — do not edit. */
/* Object API for regular class(es) `framework` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_FRAMEWORK_H
#define YETTY_YCLASSGEN_API_YGUI_FRAMEWORK_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygui/framework-defs.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;
struct yetty_ygui_theme;
struct yetty_ywire_connection;

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_framework;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FRAMEWORK_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_FRAMEWORK_PTR_RESULT
struct yetty_ygui_framework_ptr_result {
    int ok;
    union {
        struct yetty_ygui_framework *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_framework_ptr_result yetty_ygui_framework_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_framework_to(struct yetty_ygui_framework *data);

struct yetty_yclass_object_ptr_result yetty_ygui_framework_create(struct yetty_yclass_ctx *ctx);

/* Public teardown — symmetric to the generated yetty_ygui_framework_create.
 * Runs the destructor chain (framework_destructor frees the widget tree, owned
 * theme, and attached session) then releases the yclass object. Best-effort:
 * both steps run; the first error is surfaced. */
struct yetty_ycore_void_result yetty_ygui_framework_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_framework_set_container_obj(
    struct yetty_yclass_object *obj, struct yetty_yclass_object *container);
struct yetty_ycore_void_result yetty_ygui_framework_set_session(
    struct yetty_yclass_object *obj, struct yetty_yclass_rpc_session *session);
struct yetty_ycore_void_result yetty_ygui_framework_attach(struct yetty_yclass_object *obj,
                                                           int read_fd, int write_fd,
                                                           int compressed);
/* Attach over a caller-provided multiplexed connection — the endpoint path.
 * The framework opens its OWN dynamic RPC channel on the connection (the SSH
 * model), so it never shares/tears a lane with other clients on the PTY. */
struct yetty_ycore_void_result yetty_ygui_framework_attach_connection(
    struct yetty_yclass_object *obj, struct yetty_ywire_connection *connection);
struct yetty_ycore_void_result yetty_ygui_framework_set_viewport(struct yetty_yclass_object *obj,
                                                                 float width_px, float height_px);
struct yetty_ycore_void_result yetty_ygui_framework_set_theme(struct yetty_yclass_object *obj,
                                                              struct yetty_ygui_theme *theme);
struct yetty_ycore_void_result yetty_ygui_framework_apply_config_to_theme(
    struct yetty_yclass_object *obj, const struct yetty_yconfig_config *config);
struct yetty_ycore_void_result yetty_ygui_framework_feed_input(struct yetty_yclass_object *obj,
                                                               const char *bytes, size_t n);
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_button(
    struct yetty_yclass_object *obj, float x, float y, int button, int pressed, int mods);
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_motion(
    struct yetty_yclass_object *obj, float x, float y);
struct yetty_ycore_int_result yetty_ygui_framework_feed_mouse_scroll(
    struct yetty_yclass_object *obj, float x, float y, float dx, float dy);
struct yetty_ycore_void_result yetty_ygui_framework_set_root(struct yetty_yclass_object *obj,
                                                             struct yetty_yclass_object *root);
struct uint32_result yetty_ygui_framework_alloc_id(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_framework_free_id(struct yetty_yclass_object *obj,
                                                            uint32_t id);
struct yetty_ycore_void_result yetty_ygui_framework_ship_figure_delta(
    struct yetty_yclass_object *obj, uint32_t figure_id, const uint8_t *body, uint32_t body_len);
struct yetty_ycore_void_result yetty_ygui_framework_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_framework_forget_remote(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_framework_emit(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
