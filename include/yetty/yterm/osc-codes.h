#ifndef YETTY_YTERM_OSC_CODES_H
#define YETTY_YTERM_OSC_CODES_H

/*
 * Yetty OSC vendor IDs — leaf header (no other yetty deps), so thin
 * clients (yecho, future tools) can refer to these wire codes without
 * pulling yterm/terminal.h and its webgpu transitive include.
 *
 * 6xxxxx = client → server (frontend / ygui / yrich / … emit toward yetty)
 * 7xxxxx = server → client (yetty terminal emits toward the inferior)
 *
 * Each code is its own message kind (no verbs in the OSC body). ydraw
 * lives at 600000–600003; ymgui's more custom shapes start at 610000.
 * Wire-format codes for ymgui (YMGUI_OSC_*) live in <yetty/ymgui/wire.h>.
 */

#define YETTY_OSC_YDRAW_CLEAR 600000   /* empty body */
#define YETTY_OSC_YDRAW_BIN 600001     /* args = yetty_yface_bin_meta */
#define YETTY_OSC_YDRAW_YAML 600002    /* yaml text payload */
#define YETTY_OSC_YDRAW_OVERLAY 600003 /* overlay variant */

#endif /* YETTY_YTERM_OSC_CODES_H */
