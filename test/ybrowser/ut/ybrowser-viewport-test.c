/*
 * ybrowser viewport-coherence dynamic test — headless, no network/GPU.
 *
 * Proves that the JS window viewport and matchMedia() track the engine viewport
 * LIVE across yetty_ylexbor_set_viewport(): window.innerWidth reflects the new
 * viewport, and a MediaQueryList retained across the resize re-evaluates its
 * `matches` getter (rather than returning a value frozen at creation). The WPT
 * suite pins the static coherence and the strict parser; this pins the dynamic
 * behaviour the one-shot WPT runner cannot exercise. Self-skips when the build
 * carries no JS engine.
 */

#include <yetty/ybrowser/ybrowser.h>

#include "ytest.h"

#include <stdlib.h>
#include <string.h>

static struct yetty_ylexbor *make_engine(struct ytest *test, int vw, int vh)
{
    struct yetty_ylexbor_config cfg = {
        .viewport_width = vw,
        .viewport_height = vh,
        .default_font_size = 16.0f,
    };
    struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_ycore_void_result lr =
        yetty_ylexbor_load_html(r.value, "<html><body></body></html>", 26);
    YTEST_REQUIRE_OK(test, lr);
    return r.value;
}

/* Evaluate an expression in the page context; returns the displayed value
 * string (caller frees), or NULL on a Result error. */
static char *eval(struct yetty_ylexbor *r, const char *src)
{
    struct yetty_ycore_char_ptr_result res = yetty_ylexbor_eval_js(r, src);
    if (YETTY_IS_ERR(res)) {
        return NULL;
    }
    return res.value;
}

/* eval_js renders results console-style: booleans/numbers are unquoted
 * (true / 600), strings are quoted ("x"), so the checks below eval raw values,
 * not String(...)-wrapped ones. */
static void test_viewport_live_across_resize(struct ytest *test)
{
    struct yetty_ylexbor *r = make_engine(test, 1000, 800);

    char *probe = eval(r, "1+1");
    if (!probe || strcmp(probe, "2") != 0) {
        free(probe);
        yetty_ylexbor_destroy(r);
        YTEST_SKIP(test, "no JS engine in this build");
    }
    free(probe);

    /* One MediaQueryList, retained across every resize below. */
    char *setup = eval(r, "globalThis.__mq = matchMedia('(min-width: 900px)'); 1");
    YTEST_CHECK_STR_EQ(test, setup ? setup : "", "1");
    free(setup);

    /* `matches` must be a live accessor, not a stored data property. */
    char *is_getter = eval(r, "typeof Object.getOwnPropertyDescriptor(globalThis.__mq,'matches')"
                              ".get === 'function'");
    YTEST_CHECK_STR_EQ(test, is_getter ? is_getter : "", "true");
    free(is_getter);

    /* Initial viewport 1000: innerWidth reflects it, (min-width:900px) matches. */
    char *iw0 = eval(r, "window.innerWidth");
    YTEST_CHECK_STR_EQ(test, iw0 ? iw0 : "", "1000");
    free(iw0);
    char *m0 = eval(r, "globalThis.__mq.matches");
    YTEST_CHECK_STR_EQ(test, m0 ? m0 : "", "true");
    free(m0);

    /* Shrink below the breakpoint. */
    struct yetty_ycore_void_result sv1 = yetty_ylexbor_set_viewport(r, 600, 800);
    YTEST_CHECK(test, !YETTY_IS_ERR(sv1));
    char *iw1 = eval(r, "window.innerWidth");
    YTEST_CHECK_STR_EQ(test, iw1 ? iw1 : "", "600");
    free(iw1);
    /* SAME object now reports false — the getter re-evaluated. */
    char *m1 = eval(r, "globalThis.__mq.matches");
    YTEST_CHECK_STR_EQ(test, m1 ? m1 : "", "false");
    free(m1);

    /* Grow back above — flips live again. */
    struct yetty_ycore_void_result sv2 = yetty_ylexbor_set_viewport(r, 1200, 800);
    YTEST_CHECK(test, !YETTY_IS_ERR(sv2));
    char *m2 = eval(r, "globalThis.__mq.matches");
    YTEST_CHECK_STR_EQ(test, m2 ? m2 : "", "true");
    free(m2);
    /* documentElement.clientWidth (root CSSOM special case) tracks it too. */
    char *de = eval(r, "document.documentElement.clientWidth");
    YTEST_CHECK_STR_EQ(test, de ? de : "", "1200");
    free(de);

    yetty_ylexbor_destroy(r);
}

int main(void)
{
    struct ytest test = ytest_begin("ybrowser_viewport");
    YTEST_RUN(&test, test_viewport_live_across_resize);
    return ytest_end(&test);
}
