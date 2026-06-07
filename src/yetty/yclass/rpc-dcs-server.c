/* DCS RPC server adapter — attaches to an existing wire_statemachine.
 *
 * One DCS envelope = one yrpc request frame. The buffered-handler
 * model fires our callback once per envelope with the full decoded
 * body; we parse out (header, body_len, body), dispatch via
 * yetty_yclass_rpc_dispatch_one, then ship the response back as one
 * DCS envelope via the caller-supplied `emit` callback. */

#include <yetty/yclass/rpc-dcs-server.h>
#include <yetty/yclass/rpc.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DCS_SERVER_BUF_MAX 65536

struct dcs_server_state {
    int dcs_code;
    int compressed;
    yetty_yclass_rpc_dcs_emit_fn emit;
    void *user;
};

static struct yetty_ycore_void_result on_request_envelope(void *userdata,
                                                          enum yetty_ywire_envelope_kind kind,
                                                          int code, const uint8_t *args,
                                                          size_t args_len, const uint8_t *payload,
                                                          size_t payload_len)
{
    (void)kind;
    (void)code;
    (void)args;
    (void)args_len;
    struct dcs_server_state *st = userdata;

    if (payload_len < 8) {
        ywarn("rpc-dcs-server: short request frame: %zu bytes (<8 header)", payload_len);
        return YETTY_OK_VOID();
    }
    uint32_t header;
    uint32_t body_len;
    memcpy(&header, payload, 4);
    memcpy(&body_len, payload + 4, 4);
    if ((size_t)body_len > payload_len - 8) {
        ywarn("rpc-dcs-server: body_len=%u exceeds payload (%zu)", body_len, payload_len - 8);
        return YETTY_OK_VOID();
    }

    /* Dispatch the request — same per-request shape as the
     * transport-based server loop in rpc.c. */
    static uint8_t resp[DCS_SERVER_BUF_MAX];
    /* A failed dispatch is logged server-side and answered with a
     * zero-length response so the client maps it to "remote impl returned
     * error" and the loop stays up for the next frame — same contract as
     * the transport-based server loop in rpc.c. */
    struct yetty_ycore_size_result dispatch_res =
        yetty_yclass_rpc_dispatch_one(header, payload + 8, body_len, resp, sizeof(resp));
    size_t resp_len = 0;
    if (YETTY_IS_OK(dispatch_res)) {
        resp_len = dispatch_res.value;
    } else {
        yetty_ycore_error_print(stderr, "[dcs-server] dispatch", dispatch_res.error);
        yetty_ycore_error_destroy(dispatch_res.error);
    }

    /* Wire response: u32 resp_len | resp bytes — packed into one
     * DCS envelope. */
    uint8_t resp_frame[DCS_SERVER_BUF_MAX + 4];
    uint32_t rl = (uint32_t)resp_len;
    memcpy(resp_frame, &rl, 4);
    if (resp_len) {
        memcpy(resp_frame + 4, resp, resp_len);
    }

    struct yetty_ycore_buffer wire = {0};
    struct yetty_ycore_void_result er =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, st->dcs_code, /*has_args=*/0, st->compressed,
                         NULL, 0, resp_frame, 4 + resp_len, &wire);
    if (YETTY_IS_ERR(er)) {
        yetty_ycore_buffer_destroy(&wire);
        return YETTY_ERR(yetty_ycore_void, "rpc-dcs-server: emit response", er);
    }

    struct yetty_ycore_void_result wr = st->emit(wire.data, wire.size, st->user);
    yetty_ycore_buffer_destroy(&wire);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "rpc-dcs-server: ship response");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yclass_rpc_dcs_server_attach(
    struct yetty_ywire_wire_statemachine *sm, int dcs_code, int compressed,
    yetty_yclass_rpc_dcs_emit_fn emit, void *user)
{
    if (!sm) {
        return YETTY_ERR(yetty_ycore_void, "rpc_dcs_server_attach: NULL sm");
    }
    if (!emit) {
        return YETTY_ERR(yetty_ycore_void, "rpc_dcs_server_attach: NULL emit");
    }
    struct dcs_server_state *st = calloc(1, sizeof(*st));
    if (!st) {
        return YETTY_ERR(yetty_ycore_void, "rpc_dcs_server_attach: calloc");
    }
    st->dcs_code = dcs_code;
    st->compressed = compressed ? 1 : 0;
    st->emit = emit;
    st->user = user;

    /* Leaks st on register failure — the SM destroy path frees its
     * buffered_handler wrapper but not user state. Acceptable: attach
     * is called once at init; a failure here means the terminal is
     * not going to come up anyway. */
    struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_register_buffered(
        sm, YETTY_YWIRE_ENVELOPE_DCS, dcs_code, /*has_args=*/0, on_request_envelope, st);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "rpc_dcs_server_attach: register_buffered");
    return YETTY_OK_VOID();
}
