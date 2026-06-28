/*
 * producer.c — out-of-process figure producer attach helper (see producer.h).
 *
 * Plain C, no yclass annotations. An out-of-process tool calls
 * yetty_yfigure_producer_attach() to open a yclass RPC session over the DCS
 * transport (YETTY_DCS_YCLASS_RPC) to the hosting yetty's root figure
 * container, then drives that container with the generated typed stubs
 * (yetty_yfigure_create_child, yetty_yfigure_set_child_rect, …). The same
 * callsite dispatches locally in-process or marshals over the session when the
 * object is a proxy.
 */
#include <yetty/yfigure/producer.h>

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/yclass/transport-dcs.h>
#include <yetty/yfigure/container.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdlib.h>

struct yetty_yfigure_producer_session {
    struct yetty_yclass_rpc_session *session; /* owns its transport */
    struct yetty_yclass_object *container;    /* root container proxy (calloc'd) */
};

struct yetty_yfigure_producer_session_ptr_result yetty_yfigure_producer_attach(int read_fd,
                                                                               int write_fd,
                                                                               int compressed)
{
    /* The yfigure classes must be known locally so method_slot_get resolves
     * each typed stub's slot and translate_class can populate the slot table.
     * Idempotent. */
    struct yetty_ycore_void_result register_result = yetty_yfigure_register();
    YETTY_RETURN_IF_ERR(yetty_yfigure_producer_session_ptr, register_result,
                        "yfigure_producer_attach: yfigure_register");

    struct yetty_yclass_transport_ptr_result transport_result = yetty_yclass_transport_dcs_create(
        read_fd, write_fd, YETTY_DCS_YCLASS_RPC, compressed ? 1 : 0);
    YETTY_RETURN_IF_ERR(yetty_yfigure_producer_session_ptr, transport_result,
                        "yfigure_producer_attach: transport_dcs_create");

    struct yetty_yclass_rpc_session_ptr_result session_result =
        yetty_yclass_rpc_session_create(transport_result.value);
    if (YETTY_IS_ERR(session_result)) {
        /* session_create did not take ownership on failure — destroy the
         * transport ourselves. */
        struct yetty_ycore_void_result destroy_result =
            transport_result.value->ops->destroy(transport_result.value);
        if (YETTY_IS_ERR(destroy_result)) {
            yetty_ycore_error_destroy(destroy_result.error);
        }
        return YETTY_ERR(yetty_yfigure_producer_session_ptr,
                         "yfigure_producer_attach: rpc_session_create", session_result);
    }
    struct yetty_yclass_rpc_session *session = session_result.value;

    /* Batched slot resolution so steady-state typed calls need no per-slot
     * RESOLVE_SLOT round-trip mid-stream. Degraded-but-OK on failure — the
     * lazy per-slot fallback (ensure_remote_id) still resolves on demand. */
    struct yetty_ycore_void_result translate_result =
        yetty_yclass_rpc_session_translate_class(session, "yetty_yfigure_container");
    if (YETTY_IS_ERR(translate_result)) {
        yetty_ycore_error_destroy(translate_result.error);
    }

    struct yetty_yclass_handle_result root_result = yetty_yclass_rpc_session_get_root(session);
    if (YETTY_IS_ERR(root_result)) {
        struct yetty_ycore_void_result session_destroy_result =
            yetty_yclass_rpc_session_destroy(session);
        if (YETTY_IS_ERR(session_destroy_result)) {
            yetty_ycore_error_destroy(session_destroy_result.error);
        }
        return YETTY_ERR(yetty_yfigure_producer_session_ptr, "yfigure_producer_attach: get_root",
                         root_result);
    }
    if (root_result.value == 0) {
        struct yetty_ycore_void_result session_destroy_result =
            yetty_yclass_rpc_session_destroy(session);
        if (YETTY_IS_ERR(session_destroy_result)) {
            yetty_ycore_error_destroy(session_destroy_result.error);
        }
        return YETTY_ERR(yetty_yfigure_producer_session_ptr,
                         "yfigure_producer_attach: host published no root container");
    }

    struct yetty_yclass_object_ptr_result proxy_result =
        yetty_yclass_object_proxy_create(session, root_result.value, NULL);
    if (YETTY_IS_ERR(proxy_result)) {
        struct yetty_ycore_void_result session_destroy_result =
            yetty_yclass_rpc_session_destroy(session);
        if (YETTY_IS_ERR(session_destroy_result)) {
            yetty_ycore_error_destroy(session_destroy_result.error);
        }
        return YETTY_ERR(yetty_yfigure_producer_session_ptr,
                         "yfigure_producer_attach: proxy_create", proxy_result);
    }

    struct yetty_yfigure_producer_session *producer_session = calloc(1, sizeof(*producer_session));
    if (!producer_session) {
        free(proxy_result.value);
        struct yetty_ycore_void_result session_destroy_result =
            yetty_yclass_rpc_session_destroy(session);
        if (YETTY_IS_ERR(session_destroy_result)) {
            yetty_ycore_error_destroy(session_destroy_result.error);
        }
        return YETTY_ERR(yetty_yfigure_producer_session_ptr, "yfigure_producer_attach: calloc");
    }
    producer_session->session = session;
    producer_session->container = proxy_result.value;
    return YETTY_OK(yetty_yfigure_producer_session_ptr, producer_session);
}

struct yetty_yclass_object *yetty_yfigure_producer_session_container(
    struct yetty_yfigure_producer_session *producer_session)
{
    return producer_session ? producer_session->container : NULL;
}

struct yetty_yclass_rpc_session *yetty_yfigure_producer_session_rpc(
    struct yetty_yfigure_producer_session *producer_session)
{
    return producer_session ? producer_session->session : NULL;
}

struct yetty_ycore_void_result yetty_yfigure_producer_detach(
    struct yetty_yfigure_producer_session *producer_session)
{
    if (!producer_session) {
        return YETTY_OK_VOID();
    }
    /* The container proxy is a plain calloc'd carrier (object_proxy_create);
     * it does not dereference the session on free, so free it before the
     * session tears down the transport. */
    free(producer_session->container);
    struct yetty_ycore_void_result session_destroy_result =
        yetty_yclass_rpc_session_destroy(producer_session->session);
    free(producer_session);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, session_destroy_result,
                        "yfigure_producer_detach: session_destroy");
    return YETTY_OK_VOID();
}
