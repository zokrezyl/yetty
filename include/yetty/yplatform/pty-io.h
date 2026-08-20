#ifndef YETTY_YPLATFORM_PTY_IO_H
#define YETTY_YPLATFORM_PTY_IO_H

/*
 * pty-io.h — reliable full-buffer PTY write.
 *
 * The fork PTY master is non-blocking: fork_pty_write() returns 0 (not an
 * error) on EAGAIN/EWOULDBLOCK when the kernel buffer is full, and may accept
 * fewer than `len` bytes on a partial write. A single ops->write() call
 * therefore does NOT guarantee delivery — the tail is silently dropped if the
 * caller stops on a short/zero return. The established reliable-write idiom in
 * this codebase (terminal_dcs_emit_response, terminal_yface_emit) loops over
 * short writes and RETRIES a zero return with a brief backoff. This helper
 * factors that idiom so the caller keeps ownership of `data` until every byte
 * is accepted — exactly-once, in order.
 */

#include <yetty/ycore/result.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/time.h>

#include <stddef.h>
#include <stdint.h>

/* Write the ENTIRE [data, data+len) to `pty`, looping over short writes and
 * retrying zero-byte writes (EAGAIN) with a 1 ms backoff. Returns void-ok once
 * every byte was accepted. A genuine write error (a dead child / closed slave)
 * stops and returns it — nothing more can be delivered, and there is no shell to
 * deliver to. The caller must keep `data` valid for the whole call. */
static inline struct yetty_ycore_void_result yetty_yplatform_pty_write_all(
    struct yetty_platform_pty *pty, const uint8_t *data, size_t len)
{
    if (!pty || !pty->ops || !pty->ops->write) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_pty_write_all: no write op");
    }
    if (!data || len == 0) {
        return YETTY_OK_VOID();
    }
    size_t off = 0;
    while (off < len) {
        struct yetty_ycore_size_result wr =
            pty->ops->write(pty, (const char *)data + off, len - off);
        if (YETTY_IS_ERR(wr)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_pty_write_all: write", wr);
        }
        if (wr.value == 0) {
            /* EAGAIN / EWOULDBLOCK — kernel buffer full. The reader (the pane /
             * resumed shell) drains it; retry after a brief backoff rather than
             * drop the unwritten tail. */
            yetty_yplatform_ytime_sleep_ms(1);
            continue;
        }
        off += wr.value;
    }
    return YETTY_OK_VOID();
}

#endif /* YETTY_YPLATFORM_PTY_IO_H */
