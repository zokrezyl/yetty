/*
 * ydummy-client — PURE WIRE CLIENT demo for the ydummy pilot module.
 *
 * The link-boundary probe of the yclass client/server split: this binary
 * knows a shader as a PIECE OF TEXT, a rect as four floats, time as a
 * float. It compiles against the generated <yetty/api/ydummy/canvas.h> with NO
 * webgpu include path and links ONLY the generated call stubs
 * (yetty_api_ydummy) + the yclass runtime — no skeletons, no class
 * accessor, no implementation, no GPU. `nm` on this binary must show zero
 * wgpu* / *_skel / *_class_get / impl symbols; that assertion IS the
 * acceptance test.
 *
 * It connects over an inherited file descriptor (the server spawns it,
 * passing the socketpair end), obtains the server's root canvas proxy via
 * GET_ROOT, and drives it through the typed stubs — every remote id
 * resolved by canonical slot name, so no local slot table, no class
 * registration, no metadata of any kind is needed on this side.
 *
 * Usage: ydummy-client <fd>
 */

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/yclass/transport-fd.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/api/ydummy/canvas.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fail hard on any Result error — this is a probe binary, the error chain
 * on stderr IS the diagnostic. */
static void check(struct yetty_ycore_void_result result, const char *what)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_print(stderr, what, result.error);
        yetty_ycore_error_destroy(result.error);
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <fd>\n", argv[0]);
        return 2;
    }
    int connect_fd = atoi(argv[1]);
    if (connect_fd <= 0) {
        fprintf(stderr, "ydummy-client: bad fd '%s'\n", argv[1]);
        return 2;
    }

    struct yetty_yclass_transport_ptr_result transport_res =
        yetty_yclass_transport_fd_create(connect_fd);
    if (YETTY_IS_ERR(transport_res)) {
        yetty_ycore_error_print(stderr, "ydummy-client: transport_fd_create", transport_res.error);
        yetty_ycore_error_destroy(transport_res.error);
        return 1;
    }
    struct yetty_yclass_rpc_session_ptr_result session_res =
        yetty_yclass_rpc_session_create(transport_res.value);
    if (YETTY_IS_ERR(session_res)) {
        yetty_ycore_error_print(stderr, "ydummy-client: session_create", session_res.error);
        yetty_ycore_error_destroy(session_res.error);
        return 1;
    }
    struct yetty_yclass_rpc_session *session = session_res.value;

    /* The server's root object is its canvas; wrap the handle in a proxy.
     * klass stays NULL — a pure client carries no class metadata. */
    struct yetty_yclass_handle_result root_res = yetty_yclass_rpc_session_get_root(session);
    if (YETTY_IS_ERR(root_res)) {
        yetty_ycore_error_print(stderr, "ydummy-client: get_root", root_res.error);
        yetty_ycore_error_destroy(root_res.error);
        return 1;
    }
    if (root_res.value == 0) {
        fprintf(stderr, "ydummy-client: server published no root canvas\n");
        return 1;
    }
    struct yetty_yclass_object_ptr_result proxy_res =
        yetty_yclass_object_proxy_create(session, root_res.value, NULL);
    if (YETTY_IS_ERR(proxy_res)) {
        yetty_ycore_error_print(stderr, "ydummy-client: proxy_create", proxy_res.error);
        yetty_ycore_error_destroy(proxy_res.error);
        return 1;
    }
    struct yetty_yclass_object *canvas = proxy_res.value;

    /* The shader IS a piece of text. Concentric animated rings — visibly
     * different from the server's built-in default gradient, so the
     * readback proves these bytes crossed the wire. */
    static const char ring_fragment[] =
        "fn ydummy_fragment(uv: vec2f, time: f32) -> vec4f {\n"
        "    let centered = uv - vec2f(0.5, 0.5);\n"
        "    let radius = length(centered) * 8.0;\n"
        "    let wave = 0.5 + 0.5 * sin(radius * 6.28318 - time * 4.0);\n"
        "    return vec4f(wave, uv.x, 1.0 - uv.y, 1.0);\n"
        "}\n";
    struct yetty_ycore_buffer ring_wgsl = {
        .data = (uint8_t *)ring_fragment,
        .size = sizeof(ring_fragment) - 1,
        .capacity = 0,
    };
    check(yetty_ydummy_set_shader(canvas, ring_wgsl), "ydummy-client: set_shader");
    check(yetty_ydummy_set_rect(canvas, 64.0f, 64.0f, 448.0f, 448.0f), "ydummy-client: set_rect");
    check(yetty_ydummy_set_time(canvas, 1.5f), "ydummy-client: set_time");

    printf("ydummy-client: shader + rect + time shipped over the wire\n");

    /* The proxy is a bare calloc'd handle wrapper the caller owns. */
    free(canvas);
    struct yetty_ycore_void_result session_destroy_res = yetty_yclass_rpc_session_destroy(session);
    if (YETTY_IS_ERR(session_destroy_res)) {
        yetty_ycore_error_print(stderr, "ydummy-client: session_destroy",
                                session_destroy_res.error);
        yetty_ycore_error_destroy(session_destroy_res.error);
        return 1;
    }
    return 0;
}
