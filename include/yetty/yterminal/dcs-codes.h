#ifndef YETTY_YTERMINAL_DCS_CODES_H
#define YETTY_YTERMINAL_DCS_CODES_H

/*
 * Yetty DCS vendor IDs — leaf header (no other yetty deps), so thin
 * clients (yecho, ycat, future tools) can refer to these wire codes
 * without pulling yterm/terminal.h and its webgpu transitive include.
 *
 * DCS is the `ESC P <code> <final> <b64-args> ; <b64-payload> ESC \`
 * envelope kind. The decimal code rides in the DCS parameter field, a
 * single private final byte (YETTY_YWIRE_DCS_FINAL) marks end-of-command,
 * and the rest is the opaque DCS data string — a conformant ECMA-48 DCS
 * string that a pass-through terminal (tmux) forwards untouched. See
 * <yetty/ywire/wire-statemachine.h> for the framing rationale.
 *
 * 6xxxxx = client → server (frontend / ygui / yrich / … emit toward yetty)
 * 8xxxxx = bidirectional control channels (yclass RPC)
 *
 * Bulk drawing / figure payloads live on DCS because they are large
 * opaque blobs — exactly what DCS is for (cf. Sixel, ReGIS). Short
 * control/metadata messages (client input: mouse / resize / focus / key)
 * stay on OSC; see <yetty/yterminal/client-input.h>.
 *
 * Each code is its own message kind (no verbs in the body). ydraw lives
 * at 600000–600004.
 */

#define YETTY_DCS_YDRAW_CLEAR 600000   /* empty body */
#define YETTY_DCS_YDRAW_BIN 600001     /* args = yetty_yface_bin_meta */
#define YETTY_DCS_YDRAW_YAML 600002    /* yaml text payload */
#define YETTY_DCS_YDRAW_OVERLAY 600003 /* overlay variant */

/* Scene-canvas variant for entity-scoped producers (ygui). Same on-wire
 * envelope shape as YDRAW_BIN, but lands on a separate yterm layer
 * whose canvas is a scene-canvas (entity tree + GROUP/DELETE incremental
 * updates) rather than a scrolling-canvas. Producers that need the
 * incremental shipping (only re-emit dirty widgets) target this code;
 * legacy flat-list producers (ypdf, ycat svg) keep using YDRAW_BIN. */
#define YETTY_DCS_YDRAW_SCENE_BIN 600004

/* ycompositor envelope — positioned-figure compositor's wire path.
 * Same on-wire shape as YDRAW_SCENE_BIN (yface binary, framed FAM
 * records — `u32 type | u32 payload_size | payload`), but routed to
 * the ycompositor layer which decodes records into a yfigure tree. */
#define YETTY_DCS_YCOMPOSITOR_BIN 630000

/*
 * yclass RPC. One code covers both directions of a yrpc session:
 * client→server envelopes carry yrpc request frames, server→client
 * envelopes carry yrpc response frames. Each end of the PTY knows which
 * direction its transport operates in, so no separate REQ/RESP codes are
 * needed.
 */
#define YETTY_DCS_YCLASS_RPC 800000

#endif /* YETTY_YTERMINAL_DCS_CODES_H */
