/*
 * ygui-event.c — synchronous event subscribe / emit.
 *
 * Subscriptions are a singly-linked list on the target object. emit
 * walks the source's subscription list looking for matches on event
 * type and fires each callback synchronously. The continuous-event /
 * discrete-event distinction documented in the issue is purely about
 * scheduling intent — today both fire on the calling thread; later
 * phases may queue discrete events for libuv-tick drainage.
 */

#include "internal.h"
#include <yetty/ygui/widget.h>

#include <stdlib.h>

struct yetty_ycore_void_result yetty_ygui_widget_subscribe(struct yetty_yclass_object *target,
                                                           enum yetty_ygui_event_type type,
                                                           yetty_ygui_event_cb cb, void *userdata)
{
    if (!target || !cb) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_subscribe: NULL arg");
    }
    struct yetty_ygui_event_subscription *sub = calloc(1, sizeof(*sub));
    if (!sub) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_subscribe: calloc failed");
    }
    sub->type = type;
    sub->cb = cb;
    sub->userdata = userdata;
    sub->next = yetty_ygui_widget_subscriptions(target);
    yetty_ygui_widget_set_subscriptions(target, sub);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_emit(struct yetty_yclass_object *source,
                                                      const struct yetty_ygui_event *event)
{
    if (!source || !event) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit: NULL arg");
    }
    /* Walk the source's own subscriptions — the event model wires
     * "subscribe to events on a target", so emit fires on the source
     * itself (i.e., subscriptions live on the emitter today; the
     * full target/source split lands when bubbling is added). */
    for (struct yetty_ygui_event_subscription *s = yetty_ygui_widget_subscriptions(source); s;
         s = s->next) {
        if (s->type != event->type) {
            continue;
        }
        struct yetty_ycore_void_result r =
            s->cb(NULL, (struct yetty_yclass_object *)source, event, s->userdata);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit: callback failed", r);
        }
    }
    return YETTY_OK_VOID();
}
