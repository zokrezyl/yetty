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

void demo_raw_stdin(void);
void demo_sleep_ms(int ms);

/* Opens a side-channel trace file under /tmp keyed by the demo name,
 * so progress messages don't end up muxed into the OSC byte stream on
 * stdout / stderr. Returns NULL if the file can't be opened. Caller
 * fcloses. */
FILE *demo_trace_open(const char *demo_name);

#endif /* YETTY_DEMO_YWASM_COMMON_H */
