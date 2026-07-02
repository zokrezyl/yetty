/*
 * yclass RPC contract test.
 *
 * Drives the transport-agnostic request dispatcher (yetty_yclass_rpc_dispatch_one)
 * directly with hand-built request frames — no sockets needed — to pin the
 * server-side handling of every op plus the malformed-input, unknown-class, and
 * unknown-handle error paths. Then a single socketpair + server-thread
 * round-trip exercises the real transport, session, and client proxy so the
 * local-object (session == NULL) vs remote-proxy (session set) distinction is
 * covered end to end.
 */

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/yclass/transport.h>
#include <yetty/yclass/transport-fd.h>

#include "ytest.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#define YTRPC_DOMAIN "ytrpc"

struct widget_data {
    int poked;
};

/* Self-dispatching stub; its address is the 'poke' slot key. */
static struct yetty_ycore_int_result ytrpc_poke(struct yetty_yclass_object *obj);

static struct yetty_ycore_int_result poke_impl(struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 1);
}

static struct yetty_ycore_int_result ytrpc_poke(struct yetty_yclass_object *obj)
{
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 0);
}

static const struct yetty_yclass *widget_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (!cls) {
        static const struct yetty_yclass_op ops[] = {
            {YTRPC_DOMAIN, "poke", (yetty_yclass_method_id_t)ytrpc_poke,
             (yetty_yclass_impl_t)poke_impl},
        };
        static const struct yetty_yclass_descriptor desc = {
            .name = "ytrpc_widget",
            .type = YETTY_YCLASS_TYPE_REGULAR,
            .data_size = sizeof(struct widget_data),
            .data_align = _Alignof(struct widget_data),
        };
        struct yetty_yclass_ptr_result r =
            yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
        if (YETTY_IS_OK(r)) {
            cls = r.value;
        } else {
            yetty_ycore_error_destroy(r.error);
        }
    }
    return cls;
}

/*---------------------------------------------------------------------------
 * dispatch_one — malformed and error paths (no transport).
 *-------------------------------------------------------------------------*/
static void test_dispatch_unknown_op(struct ytest *test)
{
    uint8_t resp[64];
    /* op 15 is outside the defined enum → rejected, not crashed. */
    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(15u, 0u);
    struct yetty_ycore_size_result res =
        yetty_yclass_rpc_dispatch_one(header, NULL, 0, resp, sizeof(resp));
    YTEST_CHECK_ERR(test, res);
}

static void test_dispatch_call_without_skel(struct ytest *test)
{
    YTEST_REQUIRE_NOT_NULL(test, widget_class_get()); /* registers the ytrpc slot */
    uint8_t resp[64];
    /* A CALL to a real slot that has no registered skel (hand-registered
     * class, no generated wire bridge) must error at the skel lookup, not
     * crash or dispatch into garbage. */
    struct yetty_yclass_method_slot_result slot =
        yetty_yclass_method_slot_get(YTRPC_DOMAIN, (yetty_yclass_method_id_t)ytrpc_poke);
    YTEST_REQUIRE_OK(test, slot);
    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(YETTY_YCLASS_RPC_OP_CALL, (uint32_t)slot.value);
    struct yetty_ycore_size_result res =
        yetty_yclass_rpc_dispatch_one(header, NULL, 0, resp, sizeof(resp));
    YTEST_CHECK_ERR(test, res);
}

static void test_resolve_slot_unknown(struct ytest *test)
{
    uint8_t resp[8];
    const char *name = "ytrpc_no_such_slot_name";
    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, 0u);
    struct yetty_ycore_size_result res =
        yetty_yclass_rpc_dispatch_one(header, name, strlen(name), resp, sizeof(resp));
    /* Unknown slot is a valid answer (UINT32_MAX on the wire), not an error. */
    YTEST_REQUIRE_OK(test, res);
    YTEST_CHECK_EQ_SIZE(test, res.value, sizeof(uint32_t));
    uint32_t out = 0;
    memcpy(&out, resp, sizeof(out));
    YTEST_CHECK(test, out == UINT32_MAX);
}

static void test_resolve_slot_equivalence(struct ytest *test)
{
    /* The slot the client resolves over RPC must equal the id the server
     * assigned locally — the wire view of the class agrees with the local
     * dispatch table. */
    YTEST_REQUIRE_NOT_NULL(test, widget_class_get()); /* registers the ytrpc slot */
    struct yetty_yclass_method_slot_result local =
        yetty_yclass_method_slot_get(YTRPC_DOMAIN, (yetty_yclass_method_id_t)ytrpc_poke);
    YTEST_REQUIRE_OK(test, local);
    struct yetty_yclass_const_char_ptr_result qname =
        yetty_yclass_method_slot_name(local.value);
    YTEST_REQUIRE_OK(test, qname);
    YTEST_REQUIRE_NOT_NULL(test, qname.value);

    uint8_t resp[8];
    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, 0u);
    struct yetty_ycore_size_result res = yetty_yclass_rpc_dispatch_one(
        header, qname.value, strlen(qname.value), resp, sizeof(resp));
    YTEST_REQUIRE_OK(test, res);
    YTEST_CHECK_EQ_SIZE(test, res.value, sizeof(uint32_t));
    uint32_t out = 0;
    memcpy(&out, resp, sizeof(out));
    YTEST_CHECK_EQ_SIZE(test, out, (uint32_t)local.value);
}

static void test_create_unknown_class(struct ytest *test)
{
    uint8_t resp[16];
    const char *name = "ytrpc_class_that_does_not_exist";
    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(YETTY_YCLASS_RPC_OP_CREATE, 0u);
    struct yetty_ycore_size_result res =
        yetty_yclass_rpc_dispatch_one(header, name, strlen(name), resp, sizeof(resp));
    YTEST_CHECK_ERR(test, res);
}

static void test_get_class_unknown(struct ytest *test)
{
    uint8_t resp[256];
    const char *name = "ytrpc_class_that_does_not_exist";
    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(YETTY_YCLASS_RPC_OP_GET_CLASS, 0u);
    struct yetty_ycore_size_result res =
        yetty_yclass_rpc_dispatch_one(header, name, strlen(name), resp, sizeof(resp));
    YTEST_CHECK_ERR(test, res);
}

static void test_create_and_handle_resolve(struct ytest *test)
{
    YTEST_REQUIRE_NOT_NULL(test, widget_class_get());

    uint8_t resp[16];
    const char *name = "ytrpc_widget";
    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(YETTY_YCLASS_RPC_OP_CREATE, 0u);
    struct yetty_ycore_size_result res =
        yetty_yclass_rpc_dispatch_one(header, name, strlen(name), resp, sizeof(resp));
    YTEST_REQUIRE_OK(test, res);
    YTEST_CHECK_EQ_SIZE(test, res.value, sizeof(uint64_t));
    uint64_t handle = 0;
    memcpy(&handle, resp, sizeof(handle));
    YTEST_CHECK(test, handle != 0);

    /* The minted handle resolves to a live object of the right class. */
    struct yetty_yclass_void_ptr_result obj_res = yetty_yclass_rpc_handle_resolve(handle);
    YTEST_REQUIRE_OK(test, obj_res);
    struct yetty_yclass_object *obj = obj_res.value;
    YTEST_REQUIRE_NOT_NULL(test, obj);
    YTEST_CHECK(test, obj->klass == widget_class_get());

    /* An unknown handle is rejected, not resolved to garbage. */
    struct yetty_yclass_void_ptr_result bogus =
        yetty_yclass_rpc_handle_resolve(handle + 0x100000u);
    YTEST_CHECK_ERR(test, bogus);
}

/*---------------------------------------------------------------------------
 * Real transport round-trip: local object vs remote proxy.
 *-------------------------------------------------------------------------*/
struct server_arg {
    struct yetty_yclass_transport *transport;
};

static void *server_thread(void *arg)
{
    struct server_arg *server = (struct server_arg *)arg;
    struct yetty_ycore_void_result run = yetty_yclass_rpc_server_run(server->transport);
    if (YETTY_IS_ERR(run)) {
        yetty_ycore_error_destroy(run.error);
    }
    return NULL;
}

static void test_proxy_roundtrip(struct ytest *test)
{
    YTEST_REQUIRE_NOT_NULL(test, widget_class_get());

    /* Server side: a real object designated as the RPC root. */
    struct yetty_yclass_object_ptr_result server_obj_res =
        yetty_yclass_object_alloc(widget_class_get());
    YTEST_REQUIRE_OK(test, server_obj_res);
    struct yetty_yclass_object *server_obj = server_obj_res.value;
    /* A local object has no session. */
    YTEST_CHECK_NULL(test, server_obj->session);

    struct yetty_yclass_handle_result root_set = yetty_yclass_rpc_set_root(server_obj);
    YTEST_REQUIRE_OK(test, root_set);

    int fds[2];
    YTEST_REQUIRE(test, socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    struct yetty_yclass_transport_ptr_result server_transport =
        yetty_yclass_transport_fd_create(fds[1]);
    YTEST_REQUIRE_OK(test, server_transport);
    struct server_arg arg = {.transport = server_transport.value};
    pthread_t thread;
    YTEST_REQUIRE(test, pthread_create(&thread, NULL, server_thread, &arg) == 0);

    struct yetty_yclass_transport_ptr_result client_transport =
        yetty_yclass_transport_fd_create(fds[0]);
    YTEST_REQUIRE_OK(test, client_transport);
    struct yetty_yclass_rpc_session_ptr_result session_res =
        yetty_yclass_rpc_session_create(client_transport.value);
    YTEST_REQUIRE_OK(test, session_res);
    struct yetty_yclass_rpc_session *session = session_res.value;

    /* Discover the root over RPC_OP_GET_ROOT and wrap it in a client proxy. */
    struct yetty_yclass_handle_result root = yetty_yclass_rpc_session_get_root(session);
    YTEST_REQUIRE_OK(test, root);
    YTEST_CHECK(test, root.value != 0);
    struct yetty_yclass_object_ptr_result proxy_res =
        yetty_yclass_object_proxy_create(session, root.value, NULL);
    YTEST_REQUIRE_OK(test, proxy_res);
    struct yetty_yclass_object *proxy = proxy_res.value;

    /* The proxy is a remote object: it carries the session the local server
     * object does not. */
    YTEST_CHECK(test, proxy->session == session);

    /* The proxy is a bare calloc'd handle wrapper the caller owns. */
    free(proxy);

    /* Shut the client down; the server's recv sees EOF and its loop exits. */
    struct yetty_ycore_void_result session_destroy = yetty_yclass_rpc_session_destroy(session);
    YTEST_CHECK_OK(test, session_destroy);
    pthread_join(thread, NULL);
    struct yetty_ycore_void_result server_destroy =
        arg.transport->ops->destroy(arg.transport);
    YTEST_CHECK_OK(test, server_destroy);
}

int main(void)
{
    struct yetty_ycore_void_result init = yetty_yclass_rpc_init();
    if (YETTY_IS_ERR(init)) {
        yetty_ycore_error_print(stderr, "rpc_init", init.error);
        yetty_ycore_error_destroy(init.error);
        return 1;
    }

    struct ytest test = ytest_begin("yclass_rpc");
    YTEST_RUN(&test, test_dispatch_unknown_op);
    YTEST_RUN(&test, test_dispatch_call_without_skel);
    YTEST_RUN(&test, test_resolve_slot_unknown);
    YTEST_RUN(&test, test_resolve_slot_equivalence);
    YTEST_RUN(&test, test_create_unknown_class);
    YTEST_RUN(&test, test_get_class_unknown);
    YTEST_RUN(&test, test_create_and_handle_resolve);
    YTEST_RUN(&test, test_proxy_roundtrip);
    return ytest_end(&test);
}
