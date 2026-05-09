#ifndef YETTY_YPLATFORM_PTY_POLL_SOURCE_H
#define YETTY_YPLATFORM_PTY_POLL_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callback fired when data arrives from PTY output */
typedef void (*yetty_pty_data_cb)(void *ctx, const char *data, size_t len);

/* Pty poll source base - platform-specific pollable handle for PTY I/O
 *
 * Platform implementations embed this as first member for structural inheritance.
 *
 * abstract: platform-opaque value — fd on Unix, CRT fd on Windows (via _open_osfhandle)
 * on_data/on_data_ctx: callback set by the consumer (terminal) before registering with event loop
 */
struct yetty_yplatform_pty_poll_source {
    uintptr_t abstract;
    yetty_pty_data_cb on_data;
    void *on_data_ctx;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_PTY_POLL_SOURCE_H */
