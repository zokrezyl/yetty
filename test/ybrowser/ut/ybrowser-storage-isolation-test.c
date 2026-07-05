/*
 * ybrowser storage-isolation regression test — headless, no network/GPU.
 *
 * localStorage, sessionStorage and document.cookie used to be process-wide
 * file-scope arrays, so every engine (tab) in the process read and wrote one
 * shared map and one cookie string. They are engine-owned now; this test pins
 * that: two live engines must never see each other's keys or cookies.
 *
 * Each page's script writes its observations into the DOM, and the test reads
 * them back out of the laid-out box tree via the yetty_ylexbor_test_box_*
 * introspection API. Self-skips when the build carries no JS engine (the
 * sentinel the script writes never shows up).
 */

#include <yetty/ybrowser/ybrowser.h>

#include "ytest.h"

#include <stddef.h>
#include <string.h>

static struct yetty_ylexbor *load(struct ytest *test, const char *html)
{
    struct yetty_ylexbor_config cfg = {
        .viewport_width = 800,
        .viewport_height = 600,
        .default_font_size = 16.0f,
    };
    struct yetty_ylexbor_ptr_result create_res = yetty_ylexbor_create(&cfg);
    YTEST_REQUIRE_OK(test, create_res);
    struct yetty_ycore_void_result load_res =
        yetty_ylexbor_load_html(create_res.value, html, strlen(html));
    YTEST_REQUIRE_OK(test, load_res);
    return create_res.value;
}

/* Concatenate every inline text run in the box tree into `out`. */
static void joined_text(struct yetty_ylexbor *engine, char *out, size_t out_size)
{
    out[0] = '\0';
    int total = yetty_ylexbor_test_box_count(engine);
    for (int i = 0; i < total; i++) {
        int kind = 0, weight = 0, italic = 0, underline = 0;
        char text[256] = {0};
        if (yetty_ylexbor_test_box_info_at(engine, i, &kind, &weight, &italic, &underline, text,
                                           sizeof(text)) != 0) {
            continue;
        }
        if (kind == YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT && text[0]) {
            size_t used = strlen(out);
            strncat(out, text, out_size - used - 1);
        }
    }
}

/*---------------------------------------------------------------------------
 * Engine A populates all three storage surfaces and echoes them into the DOM;
 * engine B — created while A is still alive — reads the same keys and must
 * find nothing.
 *-------------------------------------------------------------------------*/
static void test_two_engines_share_nothing(struct ytest *test)
{
    struct yetty_ylexbor *engine_a =
        load(test, "<html><body><div id='out'>pending</div><script>"
                   "localStorage.setItem('iso_key', 'local-a');"
                   "sessionStorage.setItem('iso_key', 'session-a');"
                   "document.cookie = 'iso_cookie=cookie-a';"
                   "document.getElementById('out').textContent = 'A[' +"
                   "  localStorage.getItem('iso_key') + '|' +"
                   "  sessionStorage.getItem('iso_key') + '|' +"
                   "  document.cookie + ']';"
                   "</script></body></html>");

    char text_a[512];
    joined_text(engine_a, text_a, sizeof(text_a));

    /* No sentinel → the script never ran → build has no JS engine. */
    if (strstr(text_a, "A[") == NULL) {
        yetty_ylexbor_destroy(engine_a);
        YTEST_SKIP(test, "no JS engine in this build — nothing to isolate");
    }

    /* Sanity: engine A sees its own writes. */
    YTEST_CHECK(test, strstr(text_a, "local-a") != NULL);
    YTEST_CHECK(test, strstr(text_a, "session-a") != NULL);
    YTEST_CHECK(test, strstr(text_a, "iso_cookie=cookie-a") != NULL);

    struct yetty_ylexbor *engine_b =
        load(test, "<html><body><div id='out'>pending</div><script>"
                   "var leaked_local = localStorage.getItem('iso_key');"
                   "var leaked_session = sessionStorage.getItem('iso_key');"
                   "var cookie = document.cookie;"
                   "document.getElementById('out').textContent = 'B[' +"
                   "  (leaked_local === null ? 'nolocal' : 'LEAK-local') + '|' +"
                   "  (leaked_session === null ? 'nosession' : 'LEAK-session') + '|' +"
                   "  (!cookie ? 'nocookie' : 'LEAK-cookie') + ']';"
                   "</script></body></html>");

    char text_b[512];
    joined_text(engine_b, text_b, sizeof(text_b));

    YTEST_CHECK(test, strstr(text_b, "B[") != NULL);
    YTEST_CHECK(test, strstr(text_b, "nolocal") != NULL);
    YTEST_CHECK(test, strstr(text_b, "nosession") != NULL);
    YTEST_CHECK(test, strstr(text_b, "nocookie") != NULL);
    YTEST_CHECK(test, strstr(text_b, "LEAK") == NULL);

    /* And the other direction: B's page also wrote nothing into A. */
    yetty_ylexbor_destroy(engine_b);
    yetty_ylexbor_destroy(engine_a);
}

int main(void)
{
    struct ytest test = ytest_begin("ybrowser_storage_isolation");
    YTEST_RUN(&test, test_two_engines_share_nothing);
    return ytest_end(&test);
}
