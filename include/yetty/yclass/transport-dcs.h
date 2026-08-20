/* yclass DCS-over-PTY transport — client side.
 *
 * Wraps a pair of file descriptors (read_fd = where DCS responses
 * arrive, write_fd = where DCS requests go out) as a
 * `yetty_yclass_transport`. Each yrpc call ships as one DCS envelope
 * out, one DCS envelope back: the transport coalesces the three
 * yrpc writes (header / body_len / body) into a single envelope at
 * flush time, and assembles inbound envelopes into a byte stream the
 * yrpc client side reads.
 *
 * Envelope shape is the wire_statemachine's `has_args=0` form:
 *   ESC P <code> ; <b64 [+ optional lz4f] body> ESC \\
 * — one `;`, no args slot. The body is the raw yrpc frame bytes
 * (`u32 header | u32 body_len | u8 body[]` on requests, `u32 resp_len
 * | u8 resp[]` on responses).
 *
 * Send-buffer flush model: `send(bytes, n)` always appends to an
 * internal buffer and returns `n`. The buffer is flushed (one DCS
 * envelope emitted on `write_fd`) on the next `recv()` call. yrpc's
 * call pattern is write-everything-then-read-response, so one
 * envelope per call is what lands on the wire.
 *
 * fds are NOT owned. Caller closes them after destroying the
 * transport. */

#ifndef YCLASS_TRANSPORT_DCS_H
#define YCLASS_TRANSPORT_DCS_H

#include <yetty/yclass/transport.h>        /* for yetty_yclass_transport_ptr_result */
#include <yetty/ywire/wire-statemachine.h> /* for yetty_ywire_raw_cb */

#ifdef __cplusplus
extern "C" {
#endif

/* The yclass-RPC protocol's own DCS envelope code. Single source of
 * truth — the terminal's DCS registry (yterminal/dcs-codes.h) aliases
 * this value; nothing else defines it. */
#define YETTY_YCLASS_RPC_DCS_CODE 800000

/* `dcs_code` is the DCS code used in both directions for this
 * session — YETTY_YCLASS_RPC_DCS_CODE for the yclass-RPC protocol
 * (yetty_yclass_transport_default_create passes it for you).
 * `compressed`: 0 = b64-only (smaller overhead for tiny frames),
 * 1 = b64+lz4 (worth it for larger frames). */
struct yetty_yclass_transport_ptr_result yetty_yclass_transport_dcs_create(int read_fd,
                                                                           int write_fd,
                                                                           int dcs_code,
                                                                           int compressed);

/* Install a sink for raw bytes that arrive on read_fd OUTSIDE any DCS envelope
 * (e.g. keystrokes on the shared pane stdin) while the transport is reading a
 * response. Without a sink these bytes are discarded; with one, a synchronous
 * RPC no longer drops input typed during its round-trip. `cb` fires from inside
 * recv() for each contiguous raw run — do not re-enter the transport from it;
 * buffer and process after the call returns. Pass cb=NULL to clear. */
struct yetty_ycore_void_result yetty_yclass_transport_dcs_set_raw_sink(
    struct yetty_yclass_transport *transport, yetty_ywire_raw_cb cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YCLASS_TRANSPORT_DCS_H */
