/*
 * ygui-event.h — inter-widget event subscribe / emit.
 *
 * Widgets communicate via events rather than direct pointer wiring.
 * Subscriptions live on the target object and are freed when the
 * target is destroyed.
 */
#ifndef YETTY_YGUI_EVENT_H
#define YETTY_YGUI_EVENT_H

#include <stdint.h>

#include <yclass/class.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_object;
struct yetty_ygui_event;

enum yetty_ygui_event_type {
    YETTY_YGUI_EVENT_NONE = 0,
    YETTY_YGUI_EVENT_CLICK,
    YETTY_YGUI_EVENT_VALUE_CHANGED,
    YETTY_YGUI_EVENT_FOCUS,
    YETTY_YGUI_EVENT_CLOSE,
    YETTY_YGUI_EVENT_DRAG,
    YETTY_YGUI_EVENT_MOTION,
    YETTY_YGUI_EVENT_WHEEL,
};

struct yetty_ygui_event {
    enum yetty_ygui_event_type type;
    struct yetty_ygui_object *source;
    /* Per-event-type payload — readers cast based on type. The struct
     * is small (≤32 B) so it travels by value through emit/subscribe. */
    float x, y;
    int32_t i0;
    int32_t i1;
    const void *data;
};

typedef struct yetty_ycore_void_result (*yetty_ygui_event_cb)(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *target,
                                                              const struct yetty_ygui_event *event,
                                                              void *userdata);

struct yetty_ycore_void_result yetty_ygui_object_subscribe(struct yetty_ygui_object *target,
                                                           enum yetty_ygui_event_type type,
                                                           yetty_ygui_event_cb cb, void *userdata);

struct yetty_ycore_void_result yetty_ygui_object_emit(struct yetty_ygui_object *source,
                                                      const struct yetty_ygui_event *event);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_EVENT_H */
