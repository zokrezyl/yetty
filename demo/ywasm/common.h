/*
 * demo/ywasm/common.h — small helpers shared by every ywasm demo.
 *
 * Every demo runs as the inferior of a yetty terminal launched with
 * `./yetty -e <demo>`. The demo's stdin / stdout are the PTY slave;
 * yetty's wire-statemachine parses anything starting with `\e]620000;`
 * and routes it to the ywasm-layer. Going the other way, yetty writes
 * `\e]720xxx;` envelopes through the PTY master and we read them on
 * stdin. The kernel tty driver would otherwise echo our ESC bytes back
 * as caret-notation, corrupting the wire — `demo_raw_stdin()` shuts
 * that off.
 */
#ifndef YETTY_DEMO_YWASM_COMMON_H
#define YETTY_DEMO_YWASM_COMMON_H

#include <stdio.h>
#include <stdint.h>

struct yetty_ywasm_client;

void demo_raw_stdin(void);
void demo_sleep_ms(int ms);

/* Opens a side-channel trace file under /tmp keyed by the demo name,
 * so progress messages don't end up muxed into the OSC byte stream on
 * stdout / stderr. Returns NULL if the file can't be opened. Caller
 * fcloses. */
FILE *demo_trace_open(const char *demo_name);

/* Wire a 'q' keystroke from the host yetty into a flag the demo can
 * poll inside its wait loops. yetty's ywasm-layer already forwards
 * focused key/char events as KEY OSC frames; this helper just decodes
 * `codepoint == 'q'` and writes 1 into *flag (passed as user data so
 * the client doesn't need to grow another field). Pair with a
 * `demo_should_quit(flag)` check inside any pump loop the demo runs. */
extern int demo_quit_flag;
void demo_install_quit_on_q(struct yetty_ywasm_client *c);

#endif /* YETTY_DEMO_YWASM_COMMON_H */
