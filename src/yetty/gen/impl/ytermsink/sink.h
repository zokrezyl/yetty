/* GENERATED — do not edit. */
/* Public interface for regular class(es) `sink` (module: ytermsink).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YTERMSINK_SINK_H
#define YETTY_YCLASSGEN_YTERMSINK_SINK_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure abstract interface — no per-instance data. The implementing host (the
 * terminal) carries the real state; these slots dispatch onto its object. The
 * single reserved member keeps the data slice a well-formed (non-empty) C
 * struct without implying any state. */
struct yetty_yclass_ptr_result yetty_ytermsink_sink_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ytermsink_sink;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YTERMSINK_SINK_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YTERMSINK_SINK_PTR_RESULT
struct yetty_ytermsink_sink_ptr_result {
    int ok;
    union {
        struct yetty_ytermsink_sink *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ytermsink_sink_ptr_result yetty_ytermsink_sink_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ytermsink_sink_to(struct yetty_ytermsink_sink *data);

/* pty_write: relay child-directed bytes to the real PTY. Default no-op — a
 * host with no PTY (headless/test) drops them. */
struct yetty_ycore_void_result yetty_ytermsink_pty_write(struct yetty_yclass_object * obj, const char * data, size_t len);
/* request_render: ask the host to schedule a frame. Default no-op. */
struct yetty_ycore_void_result yetty_ytermsink_request_render(struct yetty_yclass_object * obj);
/* mouse_sub: report the current DEC 1500/1501 (card click/move) plus key
 * subscription state so the host starts/stops forwarding input. Default
 * no-op. */
struct yetty_ycore_void_result yetty_ytermsink_mouse_sub(struct yetty_yclass_object * obj, int click_enabled, int move_enabled, int key_enabled);
/* clipboard_write: hand an OSC 52 payload up so the host sets the OS
 * clipboard. `clipboard` is non-zero for the system clipboard ('c'), zero
 * for the primary selection. Default no-op. */
struct yetty_ycore_void_result yetty_ytermsink_clipboard_write(struct yetty_yclass_object * obj, const char * text, size_t len, int clipboard);
/* sixel_write: hand a decoded sixel image up so the host presents it as an
 * anchored image figure. Default no-op. */
struct yetty_ycore_void_result yetty_ytermsink_sixel_write(struct yetty_yclass_object * obj, const char * data, size_t len);

typedef struct yetty_ycore_void_result (*yetty_ytermsink_pty_write_fn)(struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_request_render_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_mouse_sub_fn)(struct yetty_yclass_object *, int, int, int);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_clipboard_write_fn)(struct yetty_yclass_object *, const char *, size_t, int);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_sixel_write_fn)(struct yetty_yclass_object *, const char *, size_t);

struct yetty_yclass_object_ptr_result yetty_ytermsink_sink_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ytermsink_register(void);

#ifdef __cplusplus
}
#endif

#endif
