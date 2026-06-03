/* yclass fd transport — wraps a bidirectional file descriptor as a
 * yetty_yclass_transport. send = write(2), recv = read(2). Owns the
 * fd: closes it on destroy.
 *
 * Useful for tests and for socketpair-driven setups; in-process yetty
 * will run over the wire_statemachine transport (separate impl). */

#ifndef YCLASS_TRANSPORT_FD_H
#define YCLASS_TRANSPORT_FD_H

#include <yetty/yclass/transport.h>
#include <yetty/ycore/result.h>

YETTY_YRESULT_DECLARE(yetty_yclass_transport_ptr, struct yetty_yclass_transport *);

struct yetty_yclass_transport_ptr_result yetty_yclass_transport_fd_create(int fd);

#endif /* YCLASS_TRANSPORT_FD_H */
