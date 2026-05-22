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

/* Scene-canvas variant for entity-scoped producers (ygui). Same on-wire
 * envelope shape as YDRAW_BIN, but lands on a separate yterm layer
 * whose canvas is a scene-canvas (entity tree + GROUP/DELETE incremental
 * updates) rather than a scrolling-canvas. Producers that need the
 * incremental shipping (only re-emit dirty widgets) target this code;
 * legacy flat-list producers (ypdf, ycat svg) keep using YDRAW_BIN. */
#define YETTY_OSC_YDRAW_SCENE_BIN 600004

/* ycompositor envelope — new positioned-figure compositor's wire path.
 * Same on-wire shape as YDRAW_SCENE_BIN (yface binary, framed FAM
 * records — `u32 type | u32 payload_size | payload`), but routed to
 * the ycompositor layer which decodes records into a yfigure tree
 * (currently just one default ygrid). Producers that want to migrate
 * to the new compositor stack (ygui first, then ymgui, yrdawn, …)
 * target this code under an env-var gate during the transition. */
#define YETTY_OSC_YCOMPOSITOR_BIN 630000

#endif /* YETTY_YTERM_OSC_CODES_H */
