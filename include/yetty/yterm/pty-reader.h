#ifndef YETTY_YTERM_PTY_READER_H
#define YETTY_YTERM_PTY_READER_H

#include <yetty/ycore/result.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/yterm/osc-statemachine.h>
#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yterm_pty_reader;

YETTY_YRESULT_DECLARE(yetty_yterm_pty_reader, struct yetty_yterm_pty_reader *);

struct yetty_yterm_pty_reader_result yetty_yterm_pty_reader_create(struct yetty_platform_pty *pty);

struct yetty_ycore_void_result yetty_yterm_pty_reader_destroy(struct yetty_yterm_pty_reader *reader);

/* Layer that consumes byte spans outside any OSC envelope (text → vterm). */
struct yetty_ycore_void_result yetty_yterm_pty_reader_register_default_sink(
    struct yetty_yterm_pty_reader *reader, struct yetty_yrender_terminal_layer *layer);

/* Layer that owns one OSC code. The SM does b64 / lz4 decoding before
 * the layer ever sees the bytes. */
struct yetty_ycore_void_result yetty_yterm_pty_reader_register_osc_sink(
    struct yetty_yterm_pty_reader *reader, int code,
    struct yetty_yrender_terminal_layer *layer);

/* Pull-side drain (sync-read PTY backends). */
struct yetty_ycore_size_result yetty_yterm_pty_reader_read(struct yetty_yterm_pty_reader *reader);

/* Push-side feed (libuv read-callback path — async-delivery backends). */
struct yetty_ycore_void_result yetty_yterm_pty_reader_feed(struct yetty_yterm_pty_reader *reader,
                                                           const char *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_PTY_READER_H */
