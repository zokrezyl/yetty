/*
 * ymgui_encode.h — non-blocking write helper used by the ymgui frontend.
 *
 * The OSC envelope + base64 + LZ4F compression now all live in
 * yetty_yface (see <yetty/yface/yface.h>). The frontend builds an OSC
 * sequence by feeding the wire structs into yface, then asks for the
 * resulting bytes via yetty_yface_out_buf() — those bytes are pushed
 * to the PTY through this helper.
 *
 * This file is what's left of the previous helper: just the at-most-
 * one-in-flight non-blocking write queue. Its job is to keep the demo
 * loop from blocking when yetty's PTY backpressures.
 */

#ifndef YETTY_YMGUI_FRONTEND_ENCODE_H
#define YETTY_YMGUI_FRONTEND_ENCODE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Push `bytes`/`len` to `fd` (presumed O_NONBLOCK). At most ONE partially-
 * sent buffer may be queued at a time (the previous call's leftover
 * tail). New emits while that tail is unflushed are DROPPED — interleaving
 * fresh bytes into an unfinished OSC corrupts the wire.
 *
 * Return:
 *    0  full message accepted (flushed straight, or queued for later)
 *    1  message dropped (a previous emit's tail is still in flight)
 *   -1  hard error (write returned non-EAGAIN errno)
 */
int yetty_ymgui_pending_write(int fd, const uint8_t *bytes, size_t len);

/* Try to drain the queued tail without blocking. Returns:
 *    0  drained or nothing queued
 *    1  still pending (would block)
 *   -1  hard error
 */
int yetty_ymgui_pending_flush(int fd);

/* True if a previous ymgui_pending_write left bytes queued. */
int yetty_ymgui_pending_active(void);

/* Block (poll for POLLOUT, retry write) until the pending tail is
 * drained. Used after must-reach writes (atlas upload) so subsequent
 * fresh emits aren't silently dropped by the at-most-one-in-flight
 * rule. Returns 0 on success, -1 on hard error. */
int yetty_ymgui_pending_drain_blocking(int fd);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMGUI_FRONTEND_ENCODE_H */
