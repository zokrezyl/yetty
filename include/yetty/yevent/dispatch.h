#ifndef YETTY_YEVENT_DISPATCH_H
#define YETTY_YEVENT_DISPATCH_H

#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui_event;
struct yetty_ycore_xthread_event_pipe;

/*
 * Register `listener` for the full set of event types the yetty top-level
 * dispatcher needs (keyboard, mouse, scroll, resize, render, window-refresh,
 * shutdown, plus the named ZOOM_* events).
 *
 * The listener's `handler` field must be set by the caller before invoking
 * this function. On the first failed registration, returns the error and
 * leaves any previously-registered slots in place (caller is expected to
 * tear down via event-loop destroy).
 */
struct yetty_ycore_void_result
yetty_yevent_register_default_listeners(struct yetty_yplatform_event_loop *el,
                                        struct yetty_ycore_event_listener *listener);

/*
 * Post an event into the platform input pipe. Used by translation code paths
 * (e.g. Ctrl+Scroll → ZOOM_VISUAL, RPC injections, keyboard remapping) that
 * want to reuse the normal event-loop dispatch instead of calling the handler
 * directly.
 *
 * Safe to call with a NULL or write-less pipe — logs an error and returns.
 */
void yetty_yevent_post_async(struct yetty_ycore_xthread_event_pipe *pipe,
                             const struct yetty_yui_event *event);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YEVENT_DISPATCH_H */
