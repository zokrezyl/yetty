#ifndef YETTY_YPLATFORM_PTY_PIPE_SOURCE_H
#define YETTY_YPLATFORM_PTY_PIPE_SOURCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PTY pipe source — provides fd for uv_pipe_t integration
 *
 * abstract: fd on Unix (pty master), CRT fd on Windows (via _open_osfhandle)
 * Event loop does: uv_pipe_open(abstract) + uv_read_start(alloc_cb, read_cb)
 */
struct yetty_platform_pty_pipe_source {
    uintptr_t abstract;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_PTY_PIPE_SOURCE_H */
