/* pty-reader.c — thin shell around the OSC state machine. */

#include <yetty/yterm/pty-reader.h>
#include <yetty/yterm/osc-statemachine.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>

#define PTY_READ_MAX_CHUNK 65536

struct yetty_yterm_pty_reader {
    struct yetty_platform_pty *pty;
    struct yetty_yterm_osc_sm *sm;
};

/* Thunk: the SM hands the active layer through `user`. The layer's
 * polymorphic `process(self, sm)` does the work. */
static struct yetty_ycore_void_result layer_process_thunk(void *user,
                                                          struct yetty_yterm_osc_sm *sm)
{
    struct yetty_yrender_terminal_layer *layer = user;
    if (!layer || !layer->ops || !layer->ops->process) {
        return YETTY_ERR(yetty_ycore_void, "pty_reader: layer has no process op");
    }
    return layer->ops->process(layer, sm);
}

struct yetty_yterm_pty_reader_result yetty_yterm_pty_reader_create(struct yetty_platform_pty *pty)
{
    if (!pty) {
        return YETTY_ERR(yetty_yterm_pty_reader, "pty_reader: pty is NULL");
    }

    struct yetty_yterm_pty_reader *r = calloc(1, sizeof(struct yetty_yterm_pty_reader));
    if (!r) {
        return YETTY_ERR(yetty_yterm_pty_reader, "pty_reader: alloc failed");
    }
    r->pty = pty;

    struct yetty_yterm_osc_sm_ptr_result sr = yetty_yterm_osc_sm_create();
    if (YETTY_IS_ERR(sr)) {
        free(r);
        return YETTY_ERR(yetty_yterm_pty_reader, "pty_reader: SM create", sr);
    }
    r->sm = sr.value;
    return YETTY_OK(yetty_yterm_pty_reader, r);
}

struct yetty_ycore_void_result yetty_yterm_pty_reader_destroy(struct yetty_yterm_pty_reader *reader)
{
    if (!reader) {
        return YETTY_ERR(yetty_ycore_void, "pty_reader: reader is NULL");
    }
    if (reader->sm) {
        struct yetty_ycore_void_result r = yetty_yterm_osc_sm_destroy(reader->sm);
        if (YETTY_IS_ERR(r)) {
            free(reader);
            return YETTY_ERR(yetty_ycore_void, "pty_reader: SM destroy", r);
        }
    }
    free(reader);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yterm_pty_reader_register_default_sink(
    struct yetty_yterm_pty_reader *reader, struct yetty_yrender_terminal_layer *layer)
{
    if (!reader) {
        return YETTY_ERR(yetty_ycore_void, "pty_reader: reader is NULL");
    }
    return yetty_yterm_osc_sm_set_raw_handler(reader->sm, layer_process_thunk, layer);
}

struct yetty_ycore_void_result yetty_yterm_pty_reader_register_osc_sink(
    struct yetty_yterm_pty_reader *reader, int code, enum yetty_yterm_osc_codec codec,
    struct yetty_yrender_terminal_layer *layer)
{
    if (!reader) {
        return YETTY_ERR(yetty_ycore_void, "pty_reader: reader is NULL");
    }
    return yetty_yterm_osc_sm_register(reader->sm, code, codec, layer_process_thunk, layer);
}

struct yetty_ycore_void_result yetty_yterm_pty_reader_feed(struct yetty_yterm_pty_reader *reader,
                                                           const char *data, size_t len)
{
    if (!reader) {
        return YETTY_ERR(yetty_ycore_void, "pty_reader: reader is NULL");
    }
    struct yetty_ycore_void_result fr = yetty_yterm_osc_sm_feed(reader->sm, data, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "pty_reader: feed");
    struct yetty_ycore_void_result pr = yetty_yterm_osc_sm_process(reader->sm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "pty_reader: process");
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_yterm_pty_reader_read(struct yetty_yterm_pty_reader *reader)
{
    if (!reader || !reader->pty) {
        return YETTY_ERR(yetty_ycore_size, "pty_reader: reader/pty NULL");
    }

    size_t total = 0;
    for (;;) {
        struct yetty_ycore_size_result rr =
            yetty_yterm_osc_sm_read_pty(reader->sm, reader->pty);
        if (YETTY_IS_ERR(rr)) {
            return rr;
        }
        if (rr.value == 0) {
            break; /* EAGAIN / EOF */
        }
        total += rr.value;

        struct yetty_ycore_void_result pr = yetty_yterm_osc_sm_process(reader->sm);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ycore_size, "pty_reader: SM process", pr);
        }

        if (total >= PTY_READ_MAX_CHUNK) {
            break;
        }
    }
    return YETTY_OK(yetty_ycore_size, total);
}
