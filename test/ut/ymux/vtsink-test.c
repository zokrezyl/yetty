/*
 * ymux vtsink contract test (#699.2) — headless, no GPU, no yvterm. The
 * client-side ordered terminal-byte sink: created through the yclass framework,
 * it routes each applied `feed` to a wired emit callback, tracks the applied
 * generation, and stays safe when no emit is wired or the feed is empty. This
 * also exercises the generated LOCAL dispatch of the `feed` RPC slot
 * (obj->session NULL -> vtsink_feed_impl in-process), proving the class is
 * correctly wired into codegen before the transport is built on it.
 */

#include <yetty/api/ymux/vtsink.h>
#include <yetty/yclass/rpc.h>
#include <yetty/yclass/transport.h>
#include <yetty/yclass/transport-fd.h>

#include "../../../src/yetty/ymux/rpc-lane.h"

#include "ytest.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/* Hand-written helpers — live outside the generated header, like the client's
 * raw-sink seam. The module register chains every ymux class registration and
 * the RPC skel lookups (declared in the generated impl glue, not the api). */
struct yetty_ycore_void_result yetty_ymux_register(void);
struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_make(void);
void yetty_ymux_vtsink_set_emit(struct yetty_yclass_object *obj,
                                void (*emit)(uint64_t generation, const uint8_t *bytes, size_t len,
                                             void *userdata),
                                void *userdata);

struct emit_capture {
    uint8_t bytes[256];
    size_t len;
    uint64_t last_generation;
    int calls;
};

static void capture_emit(uint64_t generation, const uint8_t *bytes, size_t len, void *userdata)
{
    struct emit_capture *capture = userdata;
    capture->last_generation = generation;
    capture->calls += 1;
    if (len <= sizeof(capture->bytes) - capture->len) {
        memcpy(capture->bytes + capture->len, bytes, len);
        capture->len += len;
    }
}

static void free_sink(struct ytest *test, struct yetty_yclass_object *sink)
{
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(sink);
    YTEST_CHECK_OK(test, free_res);
}

static void test_feed_routes_to_emit(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result sink_res = yetty_ymux_vtsink_make();
    YTEST_REQUIRE_OK(test, sink_res);
    struct yetty_yclass_object *sink = sink_res.value;

    struct emit_capture capture = {0};
    yetty_ymux_vtsink_set_emit(sink, capture_emit, &capture);

    const char payload[] = "\x1b[Hhi";
    struct yetty_ycore_buffer bytes = {
        .data = (uint8_t *)payload, .size = sizeof(payload) - 1, .capacity = sizeof(payload) - 1};
    YTEST_REQUIRE_OK(test, yetty_ymux_feed(sink, 42, bytes));
    YTEST_CHECK_EQ_INT(test, capture.calls, 1);
    YTEST_CHECK_EQ_SIZE(test, capture.len, sizeof(payload) - 1);
    YTEST_CHECK(test, memcmp(capture.bytes, payload, sizeof(payload) - 1) == 0);
    YTEST_CHECK(test, capture.last_generation == 42);

    /* A second feed advances the tracked generation and appends. */
    struct yetty_ycore_buffer more = {.data = (uint8_t *)"x", .size = 1, .capacity = 1};
    YTEST_REQUIRE_OK(test, yetty_ymux_feed(sink, 43, more));
    YTEST_CHECK_EQ_INT(test, capture.calls, 2);
    YTEST_CHECK(test, capture.last_generation == 43);

    free_sink(test, sink);
}

static void test_feed_empty_and_no_emit(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result sink_res = yetty_ymux_vtsink_make();
    YTEST_REQUIRE_OK(test, sink_res);
    struct yetty_yclass_object *sink = sink_res.value;

    /* No emit wired: feed must not crash and still return OK. */
    struct yetty_ycore_buffer data = {.data = (uint8_t *)"data", .size = 4, .capacity = 4};
    YTEST_REQUIRE_OK(test, yetty_ymux_feed(sink, 1, data));

    /* Empty feed with an emit wired: emit is not called (nothing to render). */
    struct emit_capture capture = {0};
    yetty_ymux_vtsink_set_emit(sink, capture_emit, &capture);
    struct yetty_ycore_buffer empty = {0};
    YTEST_REQUIRE_OK(test, yetty_ymux_feed(sink, 2, empty));
    YTEST_CHECK_EQ_INT(test, capture.calls, 0);

    free_sink(test, sink);
}

/*---------------------------------------------------------------------------
 * The #699.2 core, end to end in-process: a served vtsink on one side of a
 * socketpair, a producer holding a proxy on the other. The producer's typed
 * `yetty_ymux_feed(proxy, ...)` marshals over the wire, the server dispatches
 * the generated skel into vtsink_feed_impl, the emit captures the bytes, and
 * the synchronous reply doubles as the application ACK the caller observes.
 * Two sequential feeds pin ordered delivery. This is the daemon-side call
 * shape verbatim — only the transport differs (socketpair here, the ymux
 * connection lane in the daemon).
 *-------------------------------------------------------------------------*/

struct sink_server_arg {
    struct yetty_yclass_transport *transport;
};

static void *sink_server_thread(void *arg)
{
    struct sink_server_arg *server = arg;
    struct yetty_ycore_void_result run = yetty_yclass_rpc_server_run(server->transport);
    if (YETTY_IS_ERR(run)) {
        yetty_ycore_error_destroy(run.error);
    }
    return NULL;
}

static void test_feed_over_rpc_loopback(struct ytest *test)
{
    /* Server side: a real vtsink, emit wired to a capture, published as root. */
    struct yetty_yclass_object_ptr_result sink_res = yetty_ymux_vtsink_make();
    YTEST_REQUIRE_OK(test, sink_res);
    struct yetty_yclass_object *sink = sink_res.value;
    struct emit_capture capture = {0};
    yetty_ymux_vtsink_set_emit(sink, capture_emit, &capture);
    YTEST_REQUIRE_OK(test, yetty_yclass_rpc_set_root(sink));

    int fds[2];
    YTEST_REQUIRE(test, socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    struct yetty_yclass_transport_ptr_result server_transport =
        yetty_yclass_transport_fd_create(fds[1]);
    YTEST_REQUIRE_OK(test, server_transport);
    struct sink_server_arg arg = {.transport = server_transport.value};
    pthread_t thread;
    YTEST_REQUIRE(test, pthread_create(&thread, NULL, sink_server_thread, &arg) == 0);

    /* Producer side (the daemon's call shape): session + root proxy + feed. */
    struct yetty_yclass_transport_ptr_result client_transport =
        yetty_yclass_transport_fd_create(fds[0]);
    YTEST_REQUIRE_OK(test, client_transport);
    struct yetty_yclass_rpc_session_ptr_result session_res =
        yetty_yclass_rpc_session_create(client_transport.value);
    YTEST_REQUIRE_OK(test, session_res);
    struct yetty_yclass_rpc_session *session = session_res.value;

    struct yetty_yclass_handle_result root = yetty_yclass_rpc_session_get_root(session);
    YTEST_REQUIRE_OK(test, root);
    YTEST_CHECK(test, root.value != 0);
    struct yetty_yclass_object_ptr_result proxy_res =
        yetty_yclass_object_proxy_create(session, root.value, NULL);
    YTEST_REQUIRE_OK(test, proxy_res);
    struct yetty_yclass_object *proxy = proxy_res.value;

    /* Ordered typed pushes over the wire; each OK is the applied ACK. */
    const char first_payload[] = "\x1b[Hover-rpc";
    struct yetty_ycore_buffer first_bytes = {.data = (uint8_t *)first_payload,
                                             .size = sizeof(first_payload) - 1,
                                             .capacity = sizeof(first_payload) - 1};
    YTEST_REQUIRE_OK(test, yetty_ymux_feed(proxy, 7, first_bytes));
    const char second_payload[] = "!";
    struct yetty_ycore_buffer second_bytes = {
        .data = (uint8_t *)second_payload, .size = 1, .capacity = 1};
    YTEST_REQUIRE_OK(test, yetty_ymux_feed(proxy, 8, second_bytes));

    /* Tear the client down; the server sees EOF and exits. Join before
     * asserting the capture so the server thread's writes are visible. */
    free(proxy);
    YTEST_CHECK_OK(test, yetty_yclass_rpc_session_destroy(session));
    pthread_join(thread, NULL);
    YTEST_CHECK_OK(test, arg.transport->ops->destroy(arg.transport));

    YTEST_CHECK_EQ_INT(test, capture.calls, 2);
    YTEST_CHECK_EQ_SIZE(test, capture.len, sizeof(first_payload) - 1 + 1);
    YTEST_CHECK(test, memcmp(capture.bytes, first_payload, sizeof(first_payload) - 1) == 0);
    YTEST_CHECK(test, capture.bytes[sizeof(first_payload) - 1] == '!');
    YTEST_CHECK(test, capture.last_generation == 8);

    free_sink(test, sink);
}

/*---------------------------------------------------------------------------
 * The daemon-side lane mechanism (#699.2), end to end with ZERO blocking
 * round-trips: an ASYNC session over the ymux rpc-lane transport, the feed
 * slot id seeded by name (the serving peer resolves its own id locally via
 * dispatch_one(RESOLVE_SLOT) and publishes it — the production VTSINK_PUBLISH
 * shape), typed pipelined feeds that return before dispatch, and a pumped
 * completion once the response frame arrives on the lane. This is exactly the
 * daemon's wiring; only the frame carrier differs (a test buffer here, the
 * YMUX_PROTO lane in the daemon).
 *-------------------------------------------------------------------------*/

struct lane_rig {
    uint8_t requests[2048]; /* accumulated outbound request bytes */
    size_t request_len;
    int error_sink_hits;
};

static struct yetty_ycore_void_result lane_rig_tx(const uint8_t *bytes, size_t len, void *userdata)
{
    struct lane_rig *rig = userdata;
    if (rig->request_len + len > sizeof(rig->requests)) {
        return YETTY_ERR(yetty_ycore_void, "lane rig: capture full");
    }
    memcpy(rig->requests + rig->request_len, bytes, len);
    rig->request_len += len;
    return YETTY_OK_VOID();
}

static void lane_rig_error_sink(const char *qualified_name, struct yetty_ycore_error *error,
                                void *userdata)
{
    struct lane_rig *rig = userdata;
    (void)qualified_name;
    rig->error_sink_hits += 1;
    yetty_ycore_error_destroy(*error);
}

/* Serve every complete request frame accumulated on the lane: dispatch it
 * locally and push the response frame back onto the lane rx. Returns the
 * number of requests served. */
static int lane_rig_serve(struct ytest *test, struct lane_rig *rig,
                          struct yetty_yclass_transport *lane)
{
    int served = 0;
    size_t offset = 0;
    while (rig->request_len - offset >= 2 * sizeof(uint32_t)) {
        uint32_t header = 0;
        uint32_t body_len = 0;
        memcpy(&header, rig->requests + offset, sizeof(uint32_t));
        memcpy(&body_len, rig->requests + offset + sizeof(uint32_t), sizeof(uint32_t));
        if (rig->request_len - offset - 2 * sizeof(uint32_t) < body_len) {
            break; /* partial frame — wait for more bytes */
        }
        uint8_t response[1024];
        struct yetty_ycore_size_result dispatch_res =
            yetty_yclass_rpc_dispatch_one(header, rig->requests + offset + 2 * sizeof(uint32_t),
                                          body_len, response, sizeof(response));
        YTEST_REQUIRE_OK(test, dispatch_res);
        uint32_t response_len = (uint32_t)dispatch_res.value;
        YTEST_REQUIRE_OK(test, yetty_ymux_rpc_lane_push_rx(lane, (const uint8_t *)&response_len,
                                                           sizeof(response_len)));
        YTEST_REQUIRE_OK(test, yetty_ymux_rpc_lane_push_rx(lane, response, response_len));
        offset += 2 * sizeof(uint32_t) + body_len;
        served += 1;
    }
    memmove(rig->requests, rig->requests + offset, rig->request_len - offset);
    rig->request_len -= offset;
    return served;
}

static void test_feed_over_lane(struct ytest *test)
{
    /* The served side: a vtsink published as root, feed slot id resolved
     * LOCALLY (the peer publishes both out of band in production). */
    struct yetty_yclass_object_ptr_result sink_res = yetty_ymux_vtsink_make();
    YTEST_REQUIRE_OK(test, sink_res);
    struct yetty_yclass_object *sink = sink_res.value;
    struct emit_capture capture = {0};
    yetty_ymux_vtsink_set_emit(sink, capture_emit, &capture);
    struct yetty_yclass_handle_result root_res = yetty_yclass_rpc_set_root(sink);
    YTEST_REQUIRE_OK(test, root_res);

    const char slot_name[] = "yetty_ymux_feed";
    uint32_t feed_remote_id = YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
    {
        uint32_t resolve_header = YETTY_YCLASS_RPC_HDR_MAKE(YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, 0u);
        struct yetty_ycore_size_result resolve_res =
            yetty_yclass_rpc_dispatch_one(resolve_header, slot_name, sizeof(slot_name) - 1,
                                          &feed_remote_id, sizeof(feed_remote_id));
        YTEST_REQUIRE_OK(test, resolve_res);
        YTEST_REQUIRE(test, feed_remote_id != YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED);
    }

    /* The producer side (the daemon's wiring, verbatim). */
    struct lane_rig rig = {0};
    struct yetty_yclass_transport_ptr_result lane_res =
        yetty_ymux_rpc_lane_create(lane_rig_tx, &rig);
    YTEST_REQUIRE_OK(test, lane_res);
    struct yetty_yclass_transport *lane = lane_res.value;
    struct yetty_yclass_rpc_session_ptr_result session_res = yetty_yclass_rpc_session_create(lane);
    YTEST_REQUIRE_OK(test, session_res);
    struct yetty_yclass_rpc_session *session = session_res.value;
    yetty_yclass_rpc_session_set_error_sink(session, lane_rig_error_sink, &rig);
    YTEST_REQUIRE_OK(
        test, yetty_yclass_rpc_session_seed_remote_id_by_name(session, slot_name, feed_remote_id));
    struct yetty_yclass_object_ptr_result proxy_res =
        yetty_yclass_object_proxy_create(session, root_res.value, NULL);
    YTEST_REQUIRE_OK(test, proxy_res);
    struct yetty_yclass_object *proxy = proxy_res.value;

    /* Two pipelined feeds: both return immediately (no blocking IO on the
     * lane), the bytes land in the rig, nothing has dispatched yet. */
    const char first_payload[] = "lane-a";
    struct yetty_ycore_buffer first_bytes = {.data = (uint8_t *)first_payload,
                                             .size = sizeof(first_payload) - 1,
                                             .capacity = sizeof(first_payload) - 1};
    YTEST_REQUIRE_OK(test, yetty_ymux_feed(proxy, 9, first_bytes));
    const char second_payload[] = "b";
    struct yetty_ycore_buffer second_bytes = {
        .data = (uint8_t *)second_payload, .size = 1, .capacity = 1};
    YTEST_REQUIRE_OK(test, yetty_ymux_feed(proxy, 10, second_bytes));
    YTEST_CHECK_EQ_INT(test, capture.calls, 0); /* pipelined — not yet applied */
    YTEST_CHECK(test, rig.request_len > 0);

    /* Serve the lane: dispatch both requests, application happens HERE. */
    YTEST_CHECK_EQ_INT(test, lane_rig_serve(test, &rig, lane), 2);
    YTEST_CHECK_EQ_INT(test, capture.calls, 2);
    YTEST_CHECK_EQ_SIZE(test, capture.len, sizeof(first_payload) - 1 + 1);
    YTEST_CHECK(test, memcmp(capture.bytes, first_payload, sizeof(first_payload) - 1) == 0);
    YTEST_CHECK(test, capture.bytes[sizeof(first_payload) - 1] == 'b');
    YTEST_CHECK(test, capture.last_generation == 10);
    YTEST_CHECK_EQ_SIZE(test, rig.request_len, 0); /* no partial frames left */

    /* Drain the completions from the pushed response frames — the applied
     * ACKs. A failure would surface through the error sink; none may. */
    YTEST_CHECK_OK(test, yetty_yclass_rpc_session_pump(session));
    YTEST_CHECK_EQ_INT(test, rig.error_sink_hits, 0);

    free(proxy);
    YTEST_CHECK_OK(test, yetty_yclass_rpc_session_destroy(session)); /* owns the lane */
    free_sink(test, sink);
}

int main(void)
{
    struct yetty_ycore_void_result init = yetty_yclass_rpc_init();
    if (YETTY_IS_ERR(init)) {
        yetty_ycore_error_print(stderr, "rpc_init", init.error);
        yetty_ycore_error_destroy(init.error);
        return 1;
    }
    /* Registers every ymux class + the RPC skel lookups (the server side of the
     * loopback dispatches feed through them). */
    struct yetty_ycore_void_result register_res = yetty_ymux_register();
    if (YETTY_IS_ERR(register_res)) {
        yetty_ycore_error_print(stderr, "ymux_register", register_res.error);
        yetty_ycore_error_destroy(register_res.error);
        return 1;
    }

    struct ytest test = ytest_begin("ymux_vtsink");
    YTEST_RUN(&test, test_feed_routes_to_emit);
    YTEST_RUN(&test, test_feed_empty_and_no_emit);
    YTEST_RUN(&test, test_feed_over_rpc_loopback);
    YTEST_RUN(&test, test_feed_over_lane);
    return ytest_end(&test);
}
