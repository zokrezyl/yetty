/*
 * producer.h — out-of-process figure producer attach helper (the emit side).
 *
 * The container (container.c) is the RECEIVER: it exposes typed figure-tree
 * mutation slots (create_child / set_child_* / apply_child_body / …). This is
 * the client-side helper an out-of-process tool uses to reach a hosting
 * yetty's root figure container over the yclass RPC transport and drive those
 * typed slots — instead of hand-building a wire record stream.
 *
 * It is ygui-free on purpose: any app — a ygui app, a bare ydraw tool, or the
 * window chrome (ychrome) — uses it to put figures on a remote pane.
 */
#ifndef YETTY_YFIGURE_PRODUCER_H
#define YETTY_YFIGURE_PRODUCER_H

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_object;
struct yetty_yclass_rpc_session;
struct yetty_yfigure_producer_session;

YETTY_YRESULT_DECLARE(yetty_yfigure_producer_session_ptr, struct yetty_yfigure_producer_session *);

/*===========================================================================
 * yclass-RPC attach — the producer path.
 *
 * An out-of-process tool attaches to the hosting yetty's root figure
 * container over the yclass RPC transport (DCS code YETTY_DCS_YCLASS_RPC) and
 * drives it with the generated typed stubs (yetty_yfigure_create_child,
 * yetty_yfigure_set_child_rect, …). The same callsites that dispatch locally
 * in-process marshal over the session when the object is a proxy.
 *=========================================================================*/

/* Attach to the host's root figure container over the yclass RPC DCS
 * transport. `read_fd` is where RPC responses arrive (the tool's input from
 * the terminal), `write_fd` is where requests go (its output to the terminal);
 * a tool over a PTY passes STDIN_FILENO / STDOUT_FILENO. `compressed`: 0 =
 * base64 only (cheapest for tiny frames), 1 = base64+lz4.
 *
 * Ensures the yfigure classes are registered locally, opens the session, runs
 * the startup handshake (batched slot resolution + RPC_OP_GET_ROOT), and
 * returns a session wrapping the root container proxy. The fds are borrowed —
 * the caller keeps ownership and closes them after detach. */
struct yetty_yfigure_producer_session_ptr_result yetty_yfigure_producer_attach(int read_fd,
                                                                               int write_fd,
                                                                               int compressed);

/* The root container proxy to drive with the typed yetty_yfigure_* stubs.
 * Returns NULL for a NULL session. */
struct yetty_yclass_object *yetty_yfigure_producer_session_container(
    struct yetty_yfigure_producer_session *producer_session);

/* The underlying RPC session (e.g. to proxy other host objects). NULL-safe. */
struct yetty_yclass_rpc_session *yetty_yfigure_producer_session_rpc(
    struct yetty_yfigure_producer_session *producer_session);

/* Tear down the session (destroys the owned transport) and the container
 * proxy. NULL-safe. */
struct yetty_ycore_void_result yetty_yfigure_producer_detach(
    struct yetty_yfigure_producer_session *producer_session);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFIGURE_PRODUCER_H */
