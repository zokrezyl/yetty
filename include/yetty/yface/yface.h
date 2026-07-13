/*
 * yetty/yface/yface.h — bidirectional OSC stream wrapper.
 *
 * yface owns two growable byte buffers and the codec state. The OSC code
 * itself identifies the message type; there are no verbs in the body.
 *
 *   in_buf   — accumulates DECODED payload bytes (after b64 decode
 *              + optional LZ4F decompress) for a single received envelope.
 *              Cleared at start of each envelope.
 *   out_buf  — accumulates the OSC ENVELOPE for transport
 *              (\e]<code>;<b64-args>;<b64-payload>\e\\). The caller ships
 *              out_buf.data[..size] over its transport and clears it.
 *
 * Wire shape (uniform across all yface-encoded OSCs):
 *
 *     \e]<code>;<b64-args>;<b64-payload>\e\\
 *
 *   <code>     — the OSC code itself is the discriminator (no verbs).
 *                Codes are allocated by the consumer; yface is
 *                content-agnostic.
 *   <args>     — per-code binary header (b64 on the wire). For "bin"
 *                codes carrying compressed/large payloads this is a
 *                yetty_yface_bin_meta. For codes with no parameters
 *                (clear, mouse, resize, …) this slot is empty — the wire
 *                still carries `;;` so receivers always split the same
 *                way.
 *   <payload>  — body bytes (b64 on the wire, optionally LZ4F-compressed
 *                if the bin meta says so).
 *
 * Whether the body is LZ4F-compressed is a per-code policy known to
 * both sides; for bin codes the meta in args makes it self-describing
 * so receivers don't need an extra map.
 *
 * Outgoing pipeline (caller → wire):
 *
 *     yetty_yface_start_write(yface, wire_code, compressed, args, args_len);
 *         emits "\e]<code>;" raw, b64-encodes args bytes (if any) into
 *         the args slot, then ";", opens an LZ4F frame if compressed=1,
 *         and starts the streaming b64 encoder for the payload.
 *
 *     yetty_yface_write(yface, src, len);          // any number of times
 *         feeds bytes into the codec; produced bytes go through b64 into
 *         out_buf. May emit zero bytes (LZ4F batches up to its block size).
 *
 *     yetty_yface_finish_write(yface);
 *         flushes codec + b64 + writes "\e\\". out_buf is now a complete
 *         OSC sequence ready for write(2).
 *
 * Incoming pipeline (wire → consumer):
 *
 *     yetty_yface_set_handlers(yface, on_osc, on_raw, user);
 *     yetty_yface_feed_bytes(yface, raw, n);         // any number of times
 *
 *         The stream scanner finds \e]…\e\\ envelopes in `raw`, b64-
 *         decodes the args slot, b64-decodes the payload (running it
 *         through LZ4F if the bin meta says so), and fires
 *         `on_osc(user, wire_code, args, args_len, payload, payload_len)`.
 *         Any bytes outside an envelope are forwarded verbatim through
 *         `on_raw` so consumers can keep their raw-byte handling
 *         (keyboard, CSI, …) alongside structured OSC events.
 */

#ifndef YETTY_YFACE_YFACE_H
#define YETTY_YFACE_YFACE_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ywire/wire-statemachine.h> /* enum yetty_ywire_envelope_kind */

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque — codec + scanner state lives in the .c file. */
struct yetty_yface;

YETTY_YRESULT_DECLARE(yetty_yface_ptr, struct yetty_yface *);

/*-----------------------------------------------------------------------------
 * Wire structs that travel in the args slot
 *
 * Each OSC code defines what (if anything) lives in `<args>`. For "bin"
 * codes that ship a compressed binary payload (ydraw serialized buffer,
 * ymgui frame, …) the args slot carries this meta header so the receiver
 * knows how to decode the payload before touching it.
 *
 * Codes that need no args (clear, yaml-text, mouse-event, resize, …)
 * leave the slot empty (`;;` on the wire).
 *---------------------------------------------------------------------------*/

#define YETTY_YFACE_BIN_MAGIC 0x4E494249u /* "IBIN" */
#define YETTY_YFACE_BIN_VERSION 1u

#define YETTY_YFACE_COMP_NONE 0u
#define YETTY_YFACE_COMP_LZ4F 1u

struct yetty_yface_bin_meta {
    uint32_t magic;            /* YETTY_YFACE_BIN_MAGIC */
    uint32_t version;          /* YETTY_YFACE_BIN_VERSION */
    uint32_t compressed;       /* YETTY_YFACE_COMP_* */
    uint32_t compression_algo; /* reserved for non-LZ4F future codecs */
    uint64_t raw_size;         /* uncompressed payload size, 0 if unknown */
    uint32_t reserved[2];      /* pad to 32 B for forward-compat */
};

/*
 * Args meta for YETTY_DCS_MIME_FILE — the raw-file envelope the terminal
 * renders itself (see <yetty/yterminal/dcs-codes.h>). Same 32-byte size
 * as yetty_yface_bin_meta so it fits the wire statemachine's fixed args
 * buffer.
 *
 * Continuation model (designed in from v1, enforced from v2):
 * a logical file transfer is a run of envelopes sharing `stream_id`,
 * with `sequence` counting 0..N, FIRST set on the first chunk and LAST
 * on the final one. Normal terminal output may interleave between
 * chunks. ABORT cancels an open stream. v1 senders and receivers use
 * the single-shot form: one envelope with FIRST|LAST both set,
 * sequence 0. Each chunk's payload is its own complete LZ4F frame.
 *
 * The variable-length part of the header (MIME hint, filename hint,
 * render flags) does not fit the args slot; it rides as a prologue at
 * the head of the FIRST chunk's decompressed payload — see
 * <yetty/ymime/mime.h> for the codec.
 */

#define YETTY_YFACE_FILE_MAGIC 0x4C494649u /* "IFIL" */
#define YETTY_YFACE_FILE_VERSION 1u

#define YETTY_YFACE_FILE_FLAG_FIRST 0x1u
#define YETTY_YFACE_FILE_FLAG_LAST 0x2u
#define YETTY_YFACE_FILE_FLAG_ABORT 0x4u

struct yetty_yface_file_meta {
    uint32_t magic;           /* YETTY_YFACE_FILE_MAGIC */
    uint8_t version;          /* YETTY_YFACE_FILE_VERSION */
    uint8_t compressed;       /* YETTY_YFACE_COMP_* */
    uint8_t compression_algo; /* reserved for non-LZ4F future codecs */
    uint8_t flags;            /* YETTY_YFACE_FILE_FLAG_* */
    uint32_t stream_id;       /* continuation correlation; sender-chosen */
    uint32_t sequence;        /* chunk index within the stream, 0-based */
    uint64_t total_raw_size;  /* whole decompressed file (with prologue),
                               * 0 if unknown */
    uint32_t chunk_raw_size;  /* decompressed size of THIS chunk, 0 if
                               * unknown */
    uint32_t reserved;
};

#ifndef __cplusplus
/* Both metas must stay 32 B — the wire statemachine's args buffer is
 * sized for exactly this. */
_Static_assert(sizeof(struct yetty_yface_bin_meta) == 32, "bin_meta must be 32 bytes");
_Static_assert(sizeof(struct yetty_yface_file_meta) == 32, "file_meta must be 32 bytes");
#endif

/*-----------------------------------------------------------------------------
 * Lifecycle
 *---------------------------------------------------------------------------*/
struct yetty_yface_ptr_result yetty_yface_create(void);
/* Best-effort teardown — always frees the yface; the Result surfaces the
 * first sub-destroy failure (e.g. the wire statemachine) for callers that
 * want to observe it. */
struct yetty_ycore_void_result yetty_yface_destroy(struct yetty_yface *yface);

/*-----------------------------------------------------------------------------
 * Buffer access — in_buf / out_buf are owned by yface; caller may read
 * them and clear them via yetty_ycore_buffer_clear() when transferred.
 * The pointers are stable for the lifetime of the yface.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_buffer *yetty_yface_in_buf(struct yetty_yface *yface);
struct yetty_ycore_buffer *yetty_yface_out_buf(struct yetty_yface *yface);

/*-----------------------------------------------------------------------------
 * Outgoing — appends to out_buf
 *
 * `compressed`:
 *   0 — raw b64 only (no LZ4). Use for short structured payloads where
 *       LZ4 framing would dominate (mouse events, resize, …).
 *   1 — LZ4F frame, then b64. Use for large payloads where the wire
 *       savings outweigh the framing overhead (ImGui frames, textures,
 *       video, multi-MB blobs).
 *---------------------------------------------------------------------------*/
/* `args`/`args_len` are b64-encoded into the args slot of the wire
 * (`\e]<code>;<b64-args>;<b64-payload>\e\\`). NULL/0 → empty args slot,
 * the wire still carries `;;` between code and payload so receivers can
 * uniformly split.
 *
 * `compressed` controls whether the body bytes the caller hands to
 * yetty_yface_write are run through LZ4F before the b64 encoder. For bin
 * codes the same flag should also be reflected in the meta struct that
 * lives in `args` so the receiver can mirror the choice without needing
 * a yface API param. */
struct yetty_ycore_void_result yetty_yface_start_write(struct yetty_yface *yface,
                                                       enum yetty_ywire_envelope_kind kind,
                                                       int wire_code, int compressed,
                                                       const void *args, size_t args_len);
struct yetty_ycore_void_result yetty_yface_write(struct yetty_yface *yface, const void *src,
                                                 size_t len);
struct yetty_ycore_void_result yetty_yface_finish_write(struct yetty_yface *yface);

/*-----------------------------------------------------------------------------
 * Incoming — stream-scanner mode
 *
 * Caller pushes raw bytes (e.g. read(stdin) output). yface scans for
 * \e]…\e\\ envelopes, decodes the body, and emits one on_osc callback per
 * complete envelope. Anything outside an envelope is forwarded byte-for-
 * byte through on_raw — that's where the consumer plugs their keyboard /
 * CSI parser.
 *
 * Either callback may be NULL to discard that direction.
 *---------------------------------------------------------------------------*/
typedef void (*yetty_yface_msg_cb)(void *user, int wire_code, const uint8_t *args, size_t args_len,
                                   const uint8_t *payload, size_t payload_len);
typedef void (*yetty_yface_raw_cb)(void *user, const char *bytes, size_t n);

void yetty_yface_set_handlers(struct yetty_yface *yface, yetty_yface_msg_cb on_osc,
                              yetty_yface_raw_cb on_raw, void *user);

struct yetty_ycore_void_result yetty_yface_feed_bytes(struct yetty_yface *yface, const char *bytes,
                                                      size_t n);

/*-----------------------------------------------------------------------------
 * Low-level read API — for callers that already hold the body
 *
 * The stream API above is the primary read path; this remains for
 * call sites that have the b64 body extracted by some other parser
 * (e.g. ywire wire-statemachine-driven layer dispatch). `compressed` must
 * match what the writer used.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_yface_start_read(struct yetty_yface *yface, int compressed);
struct yetty_ycore_void_result yetty_yface_feed(struct yetty_yface *yface, const char *b64,
                                                size_t n);
struct yetty_ycore_void_result yetty_yface_finish_read(struct yetty_yface *yface);

/*-----------------------------------------------------------------------------
 * One-shot helpers for callers that emit / consume a single OSC at a time
 * (ycat once per file, ythorvg once per invocation, …). They internally
 * spin up a transient codec, do the encode/decode, tear it down — an
 * allocation + LZ4F-context churn per call.
 *
 * NOT for hot paths: an emitter that runs per frame / per render / per
 * flush must keep a long-lived yetty_yface and use the streaming
 * start_write/write/finish_write API above (the ymgui RenderDrawData
 * path is the reference example) — it reuses the LZ4F context and
 * scratch across envelopes.
 *---------------------------------------------------------------------------*/

/* Encode and append the full DCS sequence
 *   "\eP<wire_code>y<b64(args)>;<b64[(LZ4F)body]>\e\\"
 * to `out_buf`. The `y` is the DCS final byte (YETTY_YWIRE_DCS_FINAL);
 * see <yetty/ywire/wire-statemachine.h>. `args`/`args_len` and
 * `body`/`body_len` may both be 0; the wire still carries the `;`
 * between them. `compressed` matches yetty_yface_start_write. */
struct yetty_ycore_void_result yetty_yface_emit(int wire_code, int compressed, const void *args,
                                                size_t args_len, const void *body, size_t body_len,
                                                struct yetty_ycore_buffer *out_buf);

/* Same, but write the full envelope straight to `fd` (blocking write). */
struct yetty_ycore_void_result yetty_yface_emit_to_fd(int fd, int wire_code, int compressed,
                                                      const void *args, size_t args_len,
                                                      const void *body, size_t body_len);

/* Decode an OSC body's b64-payload (the bytes between the second `;`
 * and the trailing ESC\) into `out_buf`. `compressed` must match the
 * writer (typically read from the bin meta in args). */
struct yetty_ycore_void_result yetty_yface_decode(const char *b64, size_t n, int compressed,
                                                  struct yetty_ycore_buffer *out_buf);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFACE_YFACE_H */
