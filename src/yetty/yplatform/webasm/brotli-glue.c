/* WebASM brotli glue — exposes the linked brotli decoder to JS so the
 * asset preload shim (build-tools/web/yetty-assets-preload.js) can
 * decompress *.br files in MEMFS.
 *
 * Why not just use the browser's DecompressionStream('br')?
 *   - DecompressionStream brotli support is Chrome 124+, Firefox 127+,
 *     Safari 18+. We can't yet require those everywhere — early access
 *     hardware decode boxes / older Chromium-based browsers / Steam
 *     in-game browser etc. all 4xx that constructor.
 *   - The single-threaded `webasm` brotli prebuilt is already fetched
 *     for us (see build-tools/cmake/libs/brotli.cmake). Linking it costs
 *     ~80 KB of wasm and gives us total format control. Fits the no-JS-
 *     dependency stance the rest of the asset pipeline takes.
 */

#include <emscripten/emscripten.h>
#include <stdint.h>
#include <stdlib.h>
#include <brotli/decode.h>

/* Streaming decode into a heap buffer that grows as needed.
 *
 * Args:
 *   in        — pointer to brotli-encoded bytes (in wasm memory).
 *   in_len    — length of the encoded buffer.
 *   out_ptr   — out-param: receives a malloc'd buffer holding the
 *               decompressed bytes. The JS caller takes ownership and
 *               must call _free(*out_ptr) when done.
 *   out_len   — out-param: receives the decompressed length.
 *
 * Returns 1 on success, 0 on failure. On failure the out-params are
 * NOT written; the caller must NOT _free anything.
 *
 * Memory growth strategy: start at max(input × 4, 4 KiB) and double
 * on NEEDS_MORE_OUTPUT. Yetty's compressible assets are KB to a few
 * MB, so 1–3 reallocs at worst.
 */
EMSCRIPTEN_KEEPALIVE
int yetty_brotli_decode(const uint8_t *in, size_t in_len,
                        uint8_t **out_ptr, size_t *out_len)
{
    BrotliDecoderState *st = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    if (!st) {
        return 0;
    }

    size_t cap = in_len * 4;
    if (cap < 4096) {
        cap = 4096;
    }
    uint8_t *buf = malloc(cap);
    if (!buf) {
        BrotliDecoderDestroyInstance(st);
        return 0;
    }

    size_t avail_in = in_len;
    const uint8_t *next_in = in;
    size_t total_out = 0;

    for (;;) {
        size_t avail_out = cap - total_out;
        uint8_t *next_out = buf + total_out;

        BrotliDecoderResult r = BrotliDecoderDecompressStream(
            st, &avail_in, &next_in, &avail_out, &next_out, NULL);

        total_out = (size_t)(next_out - buf);

        if (r == BROTLI_DECODER_RESULT_SUCCESS) {
            break;
        }
        if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            cap *= 2;
            uint8_t *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
                BrotliDecoderDestroyInstance(st);
                return 0;
            }
            buf = nb;
            continue;
        }
        /* NEEDS_MORE_INPUT here = truncated; ERROR = corrupted. Both fatal. */
        free(buf);
        BrotliDecoderDestroyInstance(st);
        return 0;
    }

    BrotliDecoderDestroyInstance(st);
    *out_ptr = buf;
    *out_len = total_out;
    return 1;
}
