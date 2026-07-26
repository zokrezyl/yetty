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

/* Raw-file envelope — the payload is an UNRENDERED file (plus a small
 * prologue carrying MIME/filename hints and render flags); the terminal
 * detects the type (ymime) and runs the renderer itself (ysvg, ymarkdown,
 * ypdf, yimage, …). This is the terminal-side counterpart of what ycat
 * does client-side. args = yetty_yface_file_meta, which carries the
 * chunk-continuation fields (stream_id / sequence / FIRST / LAST) — v1
 * receivers only accept single-shot envelopes (FIRST|LAST), but the wire
 * shape already supports chunked streams for v2. */
#define YETTY_DCS_MIME_FILE 600005

/*
 * yclass RPC. One code covers both directions of a yrpc session:
 * client→server envelopes carry yrpc request frames, server→client
 * envelopes carry yrpc response frames. Each end of the PTY knows which
 * direction its transport operates in, so no separate REQ/RESP codes are
 * needed. The value is OWNED by the yclass protocol itself
 * (<yetty/yclass/transport-dcs.h>); this registry entry aliases it so
 * the code stays visible in the one place that catalogs every DCS code.
 */
#include <yetty/yclass/transport-dcs.h>
#define YETTY_DCS_YCLASS_RPC YETTY_YCLASS_RPC_DCS_CODE

/*
 * ywire connection-layer channel messages (SSH connection-layer analog).
 * Bidirectional. args = the 16-byte channel wire header
 * (msg, channel_id, window, reserved — little-endian u32 each); payload =
 * DATA chunk bytes (empty for the control messages). See
 * <yetty/ywire/channel.h> for the message set and window semantics.
 */
#define YETTY_DCS_YWIRE_CHANNEL 800001

#endif /* YETTY_YTERMINAL_DCS_CODES_H */
