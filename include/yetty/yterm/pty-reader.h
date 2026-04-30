#ifndef YETTY_YTERM_PTY_READER_H
#define YETTY_YTERM_PTY_READER_H

#include <yetty/ycore/result.h>
#include <yetty/platform/pty.h>
#include <yetty/yterm/osc-codes.h>
#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yterm_pty_reader;

struct yetty_yterm_pty_reader_result yetty_yterm_pty_reader_create(struct yetty_platform_pty *pty);

void yetty_yterm_pty_reader_destroy(struct yetty_yterm_pty_reader *reader);

void yetty_yterm_pty_reader_register_default_sink(struct yetty_yterm_pty_reader *reader,
                                                  struct yetty_yrender_terminal_layer *layer);

void yetty_yterm_pty_reader_register_osc_sink(struct yetty_yterm_pty_reader *reader, int vendor_id,
                                              struct yetty_yrender_terminal_layer *layer);

int yetty_yterm_pty_reader_read(struct yetty_yterm_pty_reader *reader);

/* Feed data directly to the pty reader (used by uv_pipe_t read callback) */
void yetty_yterm_pty_reader_feed(struct yetty_yterm_pty_reader *reader, const char *data,
                                 size_t len);

YETTY_YRESULT_DECLARE(yetty_yterm_pty_reader, struct yetty_yterm_pty_reader *);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_PTY_READER_H */
