/*
 * ymusic parser + render golden test.
 *
 * Parses a valid LilyPond-subset score, renders to a non-empty ydraw buffer,
 * handles garbage input gracefully (the parser is deliberately tolerant), and
 * pins render determinism. Headless (no GPU/display).
 */

#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/api/ymusic/music.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char k_score[] = "\\version \"2.24.0\"\n"
                              "\\relative c' { \\clef treble c4 d e f g a b c }\n";

static struct yetty_yclass_object *make_obj(struct ytest *test)
{
    struct yetty_yclass_object_ptr_result cr = yetty_ymusic_music_create(NULL);
    YTEST_REQUIRE_OK(test, cr);
    return cr.value;
}

static void test_valid_render_nonempty(struct ytest *test)
{
    struct yetty_yclass_object *obj = make_obj(test);
    YTEST_REQUIRE_OK(test, yetty_ymusic_parse(obj, k_score, sizeof(k_score) - 1));
    struct yetty_ydraw_drawable_list_result rr = yetty_ymusic_render(obj);
    YTEST_REQUIRE_OK(test, rr);
    YTEST_CHECK(test, yetty_ydraw_drawable_list_size(rr.value) > 0);
    yetty_ydraw_drawable_list_destroy(rr.value);
    yetty_ymusic_destroy(obj);
}

static void test_garbage_graceful(struct ytest *test)
{
    /* The LilyPond-subset parser skips unknown tokens rather than failing, so
     * garbage must not crash: parse returns a Result and, if OK, render still
     * produces a valid (possibly minimal) buffer. */
    struct yetty_yclass_object *obj = make_obj(test);
    const char *garbage = "@@@ this is not music at all ### {{{";
    struct yetty_ycore_void_result pr = yetty_ymusic_parse(obj, garbage, strlen(garbage));
    if (YETTY_IS_OK(pr)) {
        struct yetty_ydraw_drawable_list_result rr = yetty_ymusic_render(obj);
        if (YETTY_IS_OK(rr)) {
            yetty_ydraw_drawable_list_destroy(rr.value);
        } else {
            yetty_ycore_error_destroy(rr.error);
        }
        YTEST_CHECK(test, 1); /* reached here without crashing */
    } else {
        yetty_ycore_error_destroy(pr.error);
        YTEST_CHECK(test, 1);
    }
    yetty_ymusic_destroy(obj);
}

static void test_deterministic(struct ytest *test)
{
    struct yetty_yclass_object *obj = make_obj(test);
    YTEST_REQUIRE_OK(test, yetty_ymusic_parse(obj, k_score, sizeof(k_score) - 1));
    struct yetty_ydraw_drawable_list_result a = yetty_ymusic_render(obj);
    struct yetty_ydraw_drawable_list_result b = yetty_ymusic_render(obj);
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
    yetty_ymusic_destroy(obj);
}

int main(void)
{
    struct ytest test = ytest_begin("ymusic_parse");
    YTEST_RUN(&test, test_valid_render_nonempty);
    YTEST_RUN(&test, test_garbage_graceful);
    YTEST_RUN(&test, test_deterministic);
    return ytest_end(&test);
}
