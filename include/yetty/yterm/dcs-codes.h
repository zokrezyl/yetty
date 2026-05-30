#ifndef YETTY_YTERM_DCS_CODES_H
#define YETTY_YTERM_DCS_CODES_H

/*
 * Yetty DCS vendor IDs — leaf header, no yetty deps. DCS is the
 * `ESC P <code> ; <payload> ESC \\` envelope kind; we use the
 * has_args=0 shape (no args slot) for all yetty DCS codes, so the
 * payload is whatever the consumer protocol says (binary frames
 * for yclass RPC, etc.).
 *
 * One code covers both directions of a yrpc session: client→server
 * envelopes carry yrpc request frames, server→client envelopes carry
 * yrpc response frames. Each end of the PTY knows which direction
 * its transport operates in, so no separate REQ/RESP codes are
 * needed.
 */

#define YETTY_DCS_YCLASS_RPC 800000

#endif /* YETTY_YTERM_DCS_CODES_H */
