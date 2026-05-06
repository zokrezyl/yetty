#ifndef YETTY_YDVNC_TRANSPORT_H
#define YETTY_YDVNC_TRANSPORT_H

/*
 * Transport vtable — abstracts the byte stream below the RFB protocol so
 * future SSH / Unix-socket / vnc:// implementations can slot in without
 * touching rfb-client.c. Today only TCP is implemented.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydvnc_transport;

YETTY_YRESULT_DECLARE(yetty_ydvnc_transport_ptr, struct yetty_ydvnc_transport *);

/* Callbacks the rfb-client provides to the transport layer. */
struct yetty_ydvnc_transport_callbacks {
    void *ctx;
    void (*on_connect)(void *ctx);
    void (*on_connect_error)(void *ctx, const char *error);
    void (*on_data)(void *ctx, const uint8_t *data, size_t nbytes);
    void (*on_disconnect)(void *ctx);
};

struct yetty_ydvnc_transport_ops {
    struct yetty_ycore_void_result (*connect)(struct yetty_ydvnc_transport *self,
                                              const char *host, uint16_t port,
                                              const struct yetty_ydvnc_transport_callbacks *cbs);
    struct yetty_ycore_void_result (*send)(struct yetty_ydvnc_transport *self,
                                           const void *data, size_t nbytes);
    struct yetty_ycore_void_result (*disconnect)(struct yetty_ydvnc_transport *self);
    struct yetty_ycore_void_result (*destroy)(struct yetty_ydvnc_transport *self);
};

struct yetty_ydvnc_transport {
    const struct yetty_ydvnc_transport_ops *ops;
};

/* Concrete TCP transport — uses the platform event loop's tcp_client API. */
struct yetty_ydvnc_transport_ptr_result yetty_ydvnc_transport_tcp_create(
    struct yetty_yplatform_event_loop *event_loop);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDVNC_TRANSPORT_H */
