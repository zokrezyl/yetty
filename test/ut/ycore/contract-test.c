/*
 * ycore foundation contract test (#416) — pure, headless.
 *
 * Exercises the data-structure foundations every other module builds on and
 * which had zero direct coverage: the growable buffer, the base64 codec, hex
 * colour parsing, the fixed-capacity uint32 hash map, the Result macros, and
 * the heap-linked error cause chain (including its serialize/deserialize wire
 * round-trip). No GPU, no I/O, no context.
 */

#include <yetty/ycore/map.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ycore/util.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Growable buffer: write grows capacity as needed and preserves bytes; clear
 * resets size but keeps capacity; append concatenates.
 *-------------------------------------------------------------------------*/
static void test_buffer(struct ytest *test)
{
    struct yetty_ycore_buffer_result br = yetty_ycore_buffer_create(0);
    YTEST_REQUIRE_OK(test, br);
    struct yetty_ycore_buffer buf = br.value;

    /* Incremental writes past any small initial capacity keep all bytes. */
    for (int i = 0; i < 500; i++) {
        uint8_t byte = (uint8_t)(i & 0xFF);
        YTEST_REQUIRE_OK(test, yetty_ycore_buffer_write(&buf, &byte, 1));
    }
    YTEST_CHECK_EQ_SIZE(test, buf.size, 500u);
    YTEST_CHECK(test, buf.capacity >= 500u);
    for (int i = 0; i < 500; i++) {
        YTEST_CHECK_EQ_INT(test, buf.data[i], (uint8_t)(i & 0xFF));
    }

    /* Embedded NUL bytes are preserved (binary-safe). */
    const uint8_t bin[4] = {0x00, 0xFF, 0x00, 0x7F};
    YTEST_REQUIRE_OK(test, yetty_ycore_buffer_write(&buf, bin, sizeof(bin)));
    YTEST_CHECK_EQ_INT(test, buf.data[500], 0x00);
    YTEST_CHECK_EQ_INT(test, buf.data[501], 0xFF);
    YTEST_CHECK_EQ_INT(test, buf.data[503], 0x7F);

    /* clear resets size, keeps capacity. */
    size_t cap_before = buf.capacity;
    yetty_ycore_buffer_clear(&buf);
    YTEST_CHECK_EQ_SIZE(test, buf.size, 0u);
    YTEST_CHECK_EQ_SIZE(test, buf.capacity, cap_before);

    /* append(dst, src) concatenates src's bytes. */
    struct yetty_ycore_buffer_result sr = yetty_ycore_buffer_create(0);
    YTEST_REQUIRE_OK(test, sr);
    struct yetty_ycore_buffer src = sr.value;
    YTEST_REQUIRE_OK(test, yetty_ycore_buffer_write(&src, "hello", 5));
    YTEST_REQUIRE_OK(test, yetty_ycore_buffer_write(&buf, "AB", 2));
    YTEST_REQUIRE_OK(test, yetty_ycore_buffer_append(&buf, &src));
    YTEST_REQUIRE_EQ_SIZE(test, buf.size, 7u);
    YTEST_CHECK(test, memcmp(buf.data, "ABhello", 7) == 0);

    yetty_ycore_buffer_destroy(&src);
    yetty_ycore_buffer_destroy(&buf);
}

/*---------------------------------------------------------------------------
 * base64 encode → decode round-trips byte-for-byte across lengths (padding
 * boundaries) and binary content.
 *-------------------------------------------------------------------------*/
static void check_b64_roundtrip(struct ytest *test, const uint8_t *data, size_t len)
{
    struct yetty_ycore_buffer_result enc = yetty_ycore_base64_encode(data, len);
    YTEST_REQUIRE_OK(test, enc);
    /* Encoded length is the padded 4-per-3 size (0 for empty input). */
    YTEST_CHECK_EQ_SIZE(test, enc.value.size, ((len + 2) / 3) * 4);

    uint8_t out[512];
    size_t decoded = yetty_ycore_base64_decode((const char *)enc.value.data, enc.value.size,
                                               (char *)out, sizeof(out));
    YTEST_CHECK_EQ_SIZE(test, decoded, len);
    if (len > 0) {
        YTEST_CHECK(test, memcmp(out, data, len) == 0);
    }
    yetty_ycore_buffer_destroy(&enc.value);
}

static void test_base64_roundtrip(struct ytest *test)
{
    const uint8_t blob[] = {'y', 'e', 't', 't', 'y'};
    /* Every residue class mod 3 exercises a different padding tail. */
    for (size_t len = 0; len <= sizeof(blob); len++) {
        check_b64_roundtrip(test, blob, len);
    }
    /* Binary content with NULs and high bytes. */
    const uint8_t bin[] = {0x00, 0x01, 0xFF, 0x80, 0x00, 0x7F, 0xAA};
    check_b64_roundtrip(test, bin, sizeof(bin));
}

/*---------------------------------------------------------------------------
 * Hex colour parsing: all four length forms, packed as (A<<24)|(B<<16)|(G<<8)|R,
 * short form expands each nibble, and malformed input is rejected.
 *-------------------------------------------------------------------------*/
static void test_hex_color(struct ytest *test)
{
    uint32_t c = 0;
    /* parse_hex_color has the side effect of writing *out, so it goes through
     * the single-evaluation YTEST_CHECK/REQUIRE, not the *_EQ_* macros. */

    /* #RRGGBB → alpha defaults to 0xFF. Red = R:FF G:00 B:00 A:FF. */
    YTEST_REQUIRE(test, yetty_ycore_parse_hex_color("#FF0000", &c) == 1);
    YTEST_CHECK(test, c == 0xFF0000FFu);
    YTEST_REQUIRE(test, yetty_ycore_parse_hex_color("#00FF00", &c) == 1);
    YTEST_CHECK(test, c == 0xFF00FF00u);

    /* #RRGGBBAA → explicit alpha. R:11 G:22 B:33 A:44. */
    YTEST_REQUIRE(test, yetty_ycore_parse_hex_color("#11223344", &c) == 1);
    YTEST_CHECK(test, c == 0x44332211u);

    /* Short form: each nibble is doubled ("f" → 0xff). #f00 == #FF0000. */
    YTEST_REQUIRE(test, yetty_ycore_parse_hex_color("#f00", &c) == 1);
    YTEST_CHECK(test, c == 0xFF0000FFu);

    /* Leading '#' is optional. */
    YTEST_REQUIRE(test, yetty_ycore_parse_hex_color("00ff00", &c) == 1);
    YTEST_CHECK(test, c == 0xFF00FF00u);

    /* Malformed: bad nibble, wrong length, NULL. */
    YTEST_CHECK(test, yetty_ycore_parse_hex_color("#GG0000", &c) == 0);
    YTEST_CHECK(test, yetty_ycore_parse_hex_color("#12345", &c) == 0);
    YTEST_CHECK(test, yetty_ycore_parse_hex_color(NULL, &c) == 0);
}

/*---------------------------------------------------------------------------
 * Fixed-capacity uint32 hash map: insert/get, update-in-place, missing key,
 * and the 3/4 load-factor rejection (capacity rounds up to a minimum of 16).
 *-------------------------------------------------------------------------*/
static void test_map(struct ytest *test)
{
    struct yetty_ycore_map map;
    /* Side-effecting calls go through YTEST_CHECK/REQUIRE (single evaluation),
     * never the *_EQ_* macros (which evaluate their arguments twice for the
     * failure message). */
    YTEST_REQUIRE(test, yetty_ycore_map_init(&map, 16) == 0);

    /* Insert + read back, then update in place while below the load ceiling
     * (the load-factor gate is checked before the key lookup, so an update is
     * only accepted while there is headroom). */
    YTEST_REQUIRE(test, yetty_ycore_map_put(&map, 5, 100u) == 0);
    const uint32_t *v = yetty_ycore_map_get(&map, 5);
    YTEST_REQUIRE_NOT_NULL(test, v);
    YTEST_CHECK_EQ_INT(test, *v, 100u);
    YTEST_REQUIRE(test, yetty_ycore_map_put(&map, 5, 999u) == 0); /* update, no new slot */
    v = yetty_ycore_map_get(&map, 5);
    YTEST_REQUIRE_NOT_NULL(test, v);
    YTEST_CHECK_EQ_INT(test, *v, 999u);

    /* Missing key → NULL. */
    YTEST_CHECK(test, yetty_ycore_map_get(&map, 9999u) == NULL);

    /* Fill to the 3/4 ceiling (12 for capacity 16); key 5 already counts. */
    for (uint32_t k = 1; k <= 12; k++) {
        if (k == 5) {
            continue;
        }
        YTEST_REQUIRE(test, yetty_ycore_map_put(&map, k, k * 10u) == 0);
    }
    /* At the ceiling, a fresh key is rejected; inserted keys stay retrievable. */
    YTEST_CHECK(test, yetty_ycore_map_put(&map, 13, 1300u) == -1);
    YTEST_CHECK(test, yetty_ycore_map_get(&map, 13) == NULL);
    for (uint32_t k = 1; k <= 12; k++) {
        YTEST_CHECK(test, yetty_ycore_map_get(&map, k) != NULL);
    }

    yetty_ycore_map_destroy(&map);
}

/*---------------------------------------------------------------------------
 * Result macros: OK/ERR construction and IS_OK/IS_ERR discrimination.
 *-------------------------------------------------------------------------*/
static struct yetty_ycore_int_result maybe(int fail)
{
    if (fail) {
        return YETTY_ERR(yetty_ycore_int, "requested failure");
    }
    return YETTY_OK(yetty_ycore_int, 42);
}

static void test_result_macros(struct ytest *test)
{
    struct yetty_ycore_int_result ok = maybe(0);
    YTEST_CHECK(test, YETTY_IS_OK(ok));
    YTEST_CHECK(test, !YETTY_IS_ERR(ok));
    YTEST_CHECK_EQ_INT(test, ok.value, 42);

    struct yetty_ycore_int_result err = maybe(1);
    YTEST_CHECK(test, YETTY_IS_ERR(err));
    YTEST_CHECK(test, !YETTY_IS_OK(err));
    YTEST_CHECK(test, strcmp(err.error.msg, "requested failure") == 0);
    YTEST_CHECK(test, err.error.cause == NULL); /* root error, no chain */
}

/*---------------------------------------------------------------------------
 * Error cause chain: the 3-arg YETTY_ERR transfers the inner chain; serialize
 * → deserialize preserves every frame; snprint renders them all.
 *-------------------------------------------------------------------------*/
static void test_error_chain_serialize(struct ytest *test)
{
    struct yetty_ycore_void_result inner = YETTY_ERR(yetty_ycore_void, "inner cause");
    struct yetty_ycore_void_result mid = YETTY_ERR(yetty_ycore_void, "middle wrap", inner);
    struct yetty_ycore_void_result outer = YETTY_ERR(yetty_ycore_void, "outer context", mid);

    /* Live chain: outer → middle → inner. */
    YTEST_CHECK(test, strcmp(outer.error.msg, "outer context") == 0);
    YTEST_REQUIRE_NOT_NULL(test, outer.error.cause);
    YTEST_CHECK(test, strcmp(outer.error.cause->msg, "middle wrap") == 0);
    YTEST_REQUIRE_NOT_NULL(test, outer.error.cause->cause);
    YTEST_CHECK(test, strcmp(outer.error.cause->cause->msg, "inner cause") == 0);
    YTEST_CHECK(test, outer.error.cause->cause->cause == NULL);

    /* Serialize the whole chain, rebuild it, and compare frame by frame. */
    uint8_t wire[1024];
    size_t n = yetty_ycore_error_serialize(outer.error, wire, sizeof(wire));
    YTEST_REQUIRE(test, n > 0);
    struct yetty_ycore_error *rebuilt = yetty_ycore_error_deserialize(wire, n);
    YTEST_REQUIRE_NOT_NULL(test, rebuilt);
    YTEST_CHECK(test, strcmp(rebuilt->msg, "outer context") == 0);
    YTEST_REQUIRE_NOT_NULL(test, rebuilt->cause);
    YTEST_CHECK(test, strcmp(rebuilt->cause->msg, "middle wrap") == 0);
    YTEST_REQUIRE_NOT_NULL(test, rebuilt->cause->cause);
    YTEST_CHECK(test, strcmp(rebuilt->cause->cause->msg, "inner cause") == 0);

    /* snprint renders all three frames. */
    char text[512];
    size_t written = yetty_ycore_error_snprint(text, sizeof(text), outer.error);
    YTEST_REQUIRE(test, written > 0);
    YTEST_CHECK(test, strstr(text, "outer context") != NULL);
    YTEST_CHECK(test, strstr(text, "middle wrap") != NULL);
    YTEST_CHECK(test, strstr(text, "inner cause") != NULL);

    /* Free the rebuilt heap chain (every node is heap: attach as the cause of
     * a stack top and destroy walks + frees it). */
    struct yetty_ycore_error holder = {.msg = "holder", .cause = rebuilt};
    yetty_ycore_error_destroy(holder);

    /* Free the live chain. destroy walks .cause; only the outer top is by
     * value, so this is the single owning destroy (mid/inner tops are stack
     * and share the freed heap cause nodes — do NOT destroy them too). */
    yetty_ycore_error_destroy(outer.error);
}

int main(void)
{
    struct ytest test = ytest_begin("ycore_contract");
    YTEST_RUN(&test, test_buffer);
    YTEST_RUN(&test, test_base64_roundtrip);
    YTEST_RUN(&test, test_hex_color);
    YTEST_RUN(&test, test_map);
    YTEST_RUN(&test, test_result_macros);
    YTEST_RUN(&test, test_error_chain_serialize);
    return ytest_end(&test);
}
