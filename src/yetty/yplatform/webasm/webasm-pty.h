/* WebAssembly PTY - JSLinux iframe backend */

#ifndef WEBASM_PTY_H
#define WEBASM_PTY_H

#include <yetty/platform/pty.h>
#include "webasm-pty-pipe-source.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig;

/* WebASM PTY implementation using JSLinux iframe
 *
 * Buffer lives in parent window JS (like kernel buffer on Unix).
 * read() calls into JS to pull data from buffer via EM_ASM.
 * write() sends postMessage to iframe.
 */
struct webasm_pty {
    struct yetty_yplatform_pty base;
    struct webasm_pty_pipe_source pipe_source;
    uint32_t cols;
    uint32_t rows;
    int running;
};

/* Initialize the PTY */
struct yetty_ycore_void_result webasm_pty_init(struct webasm_pty *pty,
                                               struct yetty_yconfig *config);

#ifdef __cplusplus
}
#endif

#endif /* WEBASM_PTY_H */
