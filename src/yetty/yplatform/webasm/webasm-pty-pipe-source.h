/* WebAssembly PTY pipe source - notification callback for async data */

#ifndef WEBASM_PTY_PIPE_SOURCE_H
#define WEBASM_PTY_PIPE_SOURCE_H

#include <yetty/platform/pty-pipe-source.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callback type for notifying EventLoop when data arrives */
typedef void (*webasm_pty_notify_callback)(void *user_data);

/* WebasmPtyPipeSource - extends base pipe source with callback notification
 *
 * On webasm there's no fd to poll. Instead, JS calls webasm_pty_pipe_source_notify()
 * when data arrives in the buffer, which triggers the callback.
 */
struct webasm_pty_pipe_source {
    struct yetty_yplatform_pty_pipe_source base;
    webasm_pty_notify_callback notify_callback;
    void *notify_user_data;
};

/* Set the notification callback - called by EventLoop during register_pty_pipe */
void webasm_pty_pipe_source_set_callback(struct webasm_pty_pipe_source *source,
                                         webasm_pty_notify_callback callback, void *user_data);

/* Notify that data is available - called by JS via C export */
void webasm_pty_pipe_source_notify(struct webasm_pty_pipe_source *source);

#ifdef __cplusplus
}
#endif

#endif /* WEBASM_PTY_PIPE_SOURCE_H */
