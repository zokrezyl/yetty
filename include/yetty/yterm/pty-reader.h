#ifndef YETTY_YTERM_PTY_READER_H
#define YETTY_YTERM_PTY_READER_H

#include <yetty/ycore/result.h>
#include <yetty/platform/pty.h>
#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Yetty OSC vendor IDs.
 *
 * 6xxxxx = client → server (frontend / ygui / yrich / … emit toward yetty)
 * 7xxxxx = server → client (yetty terminal emits toward the inferior)
 *
 * Each code is its own message kind (no verbs in the OSC body). ypaint
 * lives at 600000–600003; ymgui's more custom shapes start at 610000.
 * Wire-format codes for ymgui (YMGUI_OSC_*) live in <yetty/ymgui/wire.h>. */
#define YETTY_OSC_YPAINT_CLEAR 600000   /* empty body */
#define YETTY_OSC_YPAINT_BIN 600001     /* args = yetty_yface_bin_meta */
#define YETTY_OSC_YPAINT_YAML 600002    /* yaml text payload */
#define YETTY_OSC_YPAINT_OVERLAY 600003 /* overlay variant */

struct yetty_yterm_pty_reader;

struct yetty_yterm_pty_reader_result yetty_yterm_pty_reader_create(struct yetty_yplatform_pty *pty);

void yetty_yterm_pty_reader_destroy(struct yetty_yterm_pty_reader *reader);

void yetty_yterm_pty_reader_register_default_sink(struct yetty_yterm_pty_reader *reader,
                                                  struct yetty_yterm_terminal_layer *layer);

void yetty_yterm_pty_reader_register_osc_sink(struct yetty_yterm_pty_reader *reader, int vendor_id,
                                              struct yetty_yterm_terminal_layer *layer);

int yetty_yterm_pty_reader_read(struct yetty_yterm_pty_reader *reader);

/* Feed data directly to the pty reader (used by uv_pipe_t read callback) */
void yetty_yterm_pty_reader_feed(struct yetty_yterm_pty_reader *reader, const char *data,
                                 size_t len);

YETTY_YRESULT_DECLARE(yetty_yterm_pty_reader, struct yetty_yterm_pty_reader *);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_PTY_READER_H */
