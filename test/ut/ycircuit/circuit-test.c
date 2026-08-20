/*
 * ycircuit parser + render golden test.
 *
 * Parses a valid schematic DSL, renders to a non-empty ydraw buffer, rejects
 * malformed input, and pins render determinism. Headless (no GPU/display).
 */

#include <yetty/api/ycircuit/circuit.h>
#include <yetty/ydraw-list/drawable-list.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char k_dsl[] = "circuit Half-wave\n"
                            "resistor 2 2 h R1 10k\n"
                            "diode 6 2 h D1 1N4148\n"
                            "wire 0 2 1 2\n"
                            "wire 3 2 5 2\n"
                            "gnd 6 4\n";

static struct yetty_yclass_object *make_parsed(struct ytest *test, const char *dsl, size_t len,
                                               int expect_parse_ok)
{
    struct yetty_yclass_object_ptr_result cr = yetty_ycircuit_circuit_create(NULL);
    YTEST_REQUIRE_OK(test, cr);
    struct yetty_yclass_object *obj = cr.value;
    struct yetty_ycore_void_result pr = yetty_ycircuit_parse(obj, dsl, len);
    if (expect_parse_ok) {
        YTEST_REQUIRE_OK(test, pr);
    } else {
        YTEST_CHECK_ERR(test, pr);
    }
    return obj;
}

static void test_valid_render_nonempty(struct ytest *test)
{
    struct yetty_yclass_object *obj = make_parsed(test, k_dsl, sizeof(k_dsl) - 1, 1);
    struct yetty_ydraw_drawable_list_result rr = yetty_ycircuit_render(obj);
    YTEST_REQUIRE_OK(test, rr);
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(rr.value) > 0);
    yetty_ydraw_drawable_list_destroy(rr.value);
    yetty_ycircuit_destroy(obj);
}

static void test_malformed_rejected(struct ytest *test)
{
    /* Non-numeric component coordinates — a real parse error. */
    const char *bad = "circuit X\nresistor here there\n";
    struct yetty_yclass_object *obj = make_parsed(test, bad, strlen(bad), 0);
    yetty_ycircuit_destroy(obj);
}

static void test_deterministic(struct ytest *test)
{
    struct yetty_yclass_object *obj = make_parsed(test, k_dsl, sizeof(k_dsl) - 1, 1);
    struct yetty_ydraw_drawable_list_result a = yetty_ycircuit_render(obj);
    struct yetty_ydraw_drawable_list_result b = yetty_ycircuit_render(obj);
    YTEST_REQUIRE_OK(test, a);
    YTEST_REQUIRE_OK(test, b);
    size_t sa = yetty_ydraw_drawable_list_size(a.value);
    size_t sb = yetty_ydraw_drawable_list_size(b.value);
    YTEST_REQUIRE_EQ_SIZE(test, sa, sb);
    YTEST_CHECK(test, sa > 0);
    YTEST_CHECK_MEM_EQ(test, yetty_ydraw_drawable_list_data(a.value),
                       yetty_ydraw_drawable_list_data(b.value), sa);
    yetty_ydraw_drawable_list_destroy(a.value);
    yetty_ydraw_drawable_list_destroy(b.value);
    yetty_ycircuit_destroy(obj);
}

int main(void)
{
    struct ytest test = ytest_begin("ycircuit_parse");
    YTEST_RUN(&test, test_valid_render_nonempty);
    YTEST_RUN(&test, test_malformed_rejected);
    YTEST_RUN(&test, test_deterministic);
    return ytest_end(&test);
}
