#ifndef YETTY_YTERMINAL_OSC_CODES_H
#define YETTY_YTERMINAL_OSC_CODES_H

/*
 * Yetty OSC vendor IDs — leaf header (no other yetty deps).
 *
 * OSC (`ESC ] <code> ; <args> ; <payload> ST`) is reserved for short
 * control / metadata messages. The client-input channel (mouse / resize
 * / focus / key / subscribe) lives in <yetty/yterminal/client-input.h>.
 *
 * Bulk drawing payloads (ydraw, scene) used to live here on OSC; they now
 * ride DCS instead — large opaque blobs belong on a Device Control String,
 * which a multiplexer forwards verbatim. The figure tree is driven over the
 * typed yclass-RPC channel (YETTY_DCS_YCLASS_RPC). See
 * <yetty/yterminal/dcs-codes.h> (YETTY_DCS_YDRAW_*) and the framing rationale
 * in <yetty/ywire/wire-statemachine.h>.
 *
 * 6xxxxx = client → server, 7xxxxx = server → client.
 */

#endif /* YETTY_YTERMINAL_OSC_CODES_H */
