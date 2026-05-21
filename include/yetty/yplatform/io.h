/*
 * yplatform/io.h — cross-platform file-descriptor utilities.
 *
 * The yrdawn client pumps incoming OSC bytes from its stdin without
 * blocking. POSIX has poll(2) + read(2); Windows has PeekNamedPipe +
 * ReadFile. This header papers over the difference so the client
 * stays portable without `#ifdef _WIN32` per-call.
 */
#ifndef YETTY_YPLATFORM_IO_H
#define YETTY_YPLATFORM_IO_H

#include <stddef.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read whatever bytes are immediately readable from `fd` into `buf`,
 * up to `cap` bytes. Returns:
 *   ok, value > 0     — bytes read (may be < cap).
 *   ok, value == 0    — nothing currently available, or EOF / HUP.
 *   err               — a real error: bad fd, broken pipe, etc.
 *
 * Does not block. EAGAIN / EWOULDBLOCK / EINTR are normalised to
 * "value == 0", so callers can spin-wait with a short sleep without
 * re-checking errno each time. */
YETTY_YRESULT_DECLARE(yetty_yplatform_io_size, size_t);

struct yetty_yplatform_io_size_result
yetty_yplatform_io_read_nonblocking(int fd, void *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_IO_H */
