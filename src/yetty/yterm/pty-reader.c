/* pty-reader.c — thin shell around the OSC state machine. */

#include <yetty/yterm/pty-reader.h>
#include <yetty/yterm/osc-statemachine.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>

#define PTY_READ_MAX_CHUNK 65536

struct yetty_yterm_pty_reader {
    struct yetty_platform_pty *pty;
    struct yetty_yterm_osc_statemachine *osc_statemachine;
};

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

    struct yetty_yterm_osc_statemachine_ptr_result sr =
        yetty_yterm_osc_statemachine_create(pty);
    if (YETTY_IS_ERR(sr)) {
        free(r);
        return YETTY_ERR(yetty_yterm_pty_reader, "pty_reader: SM create", sr);
    }
    r->osc_statemachine = sr.value;
    return YETTY_OK(yetty_yterm_pty_reader, r);
}

struct yetty_ycore_void_result yetty_yterm_pty_reader_destroy(struct yetty_yterm_pty_reader *reader)
{
    if (!reader) {
        return YETTY_ERR(yetty_ycore_void, "pty_reader: reader is NULL");
    }
    if (reader->osc_statemachine) {
        struct yetty_ycore_void_result r =
            yetty_yterm_osc_statemachine_destroy(reader->osc_statemachine);
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
    return yetty_yterm_osc_statemachine_set_default(reader->osc_statemachine, layer);
}

struct yetty_ycore_void_result yetty_yterm_pty_reader_register_osc_sink(
    struct yetty_yterm_pty_reader *reader, int code,
    struct yetty_yrender_terminal_layer *layer)
{
    if (!reader) {
        return YETTY_ERR(yetty_ycore_void, "pty_reader: reader is NULL");
    }
    return yetty_yterm_osc_statemachine_register(reader->osc_statemachine, code, layer);
}

struct yetty_ycore_void_result yetty_yterm_pty_reader_feed(struct yetty_yterm_pty_reader *reader,
                                                           const char *data, size_t len)
{
    if (!reader) {
        return YETTY_ERR(yetty_ycore_void, "pty_reader: reader is NULL");
    }
    struct yetty_ycore_void_result fr =
        yetty_yterm_osc_statemachine_feed(reader->osc_statemachine, data, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "pty_reader: feed");
    struct yetty_ycore_void_result pr =
        yetty_yterm_osc_statemachine_process(reader->osc_statemachine);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "pty_reader: process");
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_yterm_pty_reader_read(struct yetty_yterm_pty_reader *reader)
{
    if (!reader || !reader->pty) {
        return YETTY_ERR(yetty_ycore_size, "pty_reader: reader/pty NULL");
    }
    struct yetty_ycore_void_result pr =
        yetty_yterm_osc_statemachine_process(reader->osc_statemachine);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ycore_size, "pty_reader: SM process", pr);
    }
    return YETTY_OK(yetty_ycore_size, 0);
}
