/*
 * ybrowser DOM/attribute contract test (#422) — headless, no network/GPU.
 *
 * Complements the layout/inline/paint geometry tests with the DOM-model surface
 * they don't touch: element attribute readback (id / class / href / data-*),
 * text-content survival through the box tree, and element tag/class counting.
 * Parses small HTML strings and asserts against the post-parse/layout box
 * vector via the yetty_ylexbor_test_box_* introspection API. Deterministic and
 * fully offline (the css/Lexbor engine leaks are third-party internals,
 * suppressed narrowly via test/lsan.supp — see the CMake comment).
 */

#include <yetty/ybrowser/ybrowser.h>

#include "ytest.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static struct yetty_ylexbor *load(struct ytest *test, const char *html)
{
    struct yetty_ylexbor_config cfg = {
        .viewport_width = 800,
        .viewport_height = 600,
        .default_font_size = 16.0f,
    };
    struct yetty_ylexbor_ptr_result r = yetty_ylexbor_create(&cfg);
    YTEST_REQUIRE_OK(test, r);
    struct yetty_ycore_void_result lr = yetty_ylexbor_load_html(r.value, html, strlen(html));
    YTEST_REQUIRE_OK(test, lr);
    return r.value;
}

/* Index of the nth (0-based) box whose element tag == `tag`, or -1. */
static int find_tag(struct yetty_ylexbor *yl, const char *tag, int nth)
{
    int total = yetty_ylexbor_test_box_count(yl);
    int seen = 0;
    for (int i = 0; i < total; i++) {
        char t[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, t, sizeof(t)) != 0) {
            continue;
        }
        if (strcmp(t, tag) == 0) {
            if (seen == nth) {
                return i;
            }
            seen++;
        }
    }
    return -1;
}

static int count_tag(struct yetty_ylexbor *yl, const char *tag)
{
    int total = yetty_ylexbor_test_box_count(yl);
    int n = 0;
    for (int i = 0; i < total; i++) {
        char t[16] = {0};
        float x, y, w, h;
        if (yetty_ylexbor_test_box_at(yl, i, &x, &y, &w, &h, t, sizeof(t)) != 0) {
            continue;
        }
        if (strcmp(t, tag) == 0) {
            n++;
        }
    }
    return n;
}

/*---------------------------------------------------------------------------
 * Element attributes read back by name; a missing attribute reports failure.
 *-------------------------------------------------------------------------*/
static void test_attributes(struct ytest *test)
{
    struct yetty_ylexbor *yl = load(
        test, "<html><body><a id='lnk' href='/foo' class='nav' data-k='v'>link</a></body></html>");

    int a = find_tag(yl, "a", 0);
    YTEST_REQUIRE(test, a >= 0);

    char buf[64];
    int rc;

    rc = yetty_ylexbor_test_box_attr_at(yl, a, "href", buf, sizeof(buf));
    YTEST_CHECK_EQ_INT(test, rc, 0);
    YTEST_CHECK(test, strcmp(buf, "/foo") == 0);

    rc = yetty_ylexbor_test_box_attr_at(yl, a, "id", buf, sizeof(buf));
    YTEST_CHECK_EQ_INT(test, rc, 0);
    YTEST_CHECK(test, strcmp(buf, "lnk") == 0);

    rc = yetty_ylexbor_test_box_attr_at(yl, a, "class", buf, sizeof(buf));
    YTEST_CHECK_EQ_INT(test, rc, 0);
    YTEST_CHECK(test, strcmp(buf, "nav") == 0);

    rc = yetty_ylexbor_test_box_attr_at(yl, a, "data-k", buf, sizeof(buf));
    YTEST_CHECK_EQ_INT(test, rc, 0);
    YTEST_CHECK(test, strcmp(buf, "v") == 0);

    /* A missing attribute is reported (non-zero), not a stale/garbage value. */
    rc = yetty_ylexbor_test_box_attr_at(yl, a, "nonexistent", buf, sizeof(buf));
    YTEST_CHECK(test, rc != 0);

    yetty_ylexbor_destroy(yl);
}

/*---------------------------------------------------------------------------
 * Text content survives through the inline box tree, including across an inline
 * child element.
 *-------------------------------------------------------------------------*/
static void test_textcontent(struct ytest *test)
{
    struct yetty_ylexbor *yl =
        load(test, "<html><body><p>Hello <b>bold</b> World</p></body></html>");

    char joined[256] = {0};
    int total = yetty_ylexbor_test_box_count(yl);
    for (int i = 0; i < total; i++) {
        int kind = 0, weight = 0, italic = 0, underline = 0;
        char txt[128] = {0};
        if (yetty_ylexbor_test_box_info_at(yl, i, &kind, &weight, &italic, &underline, txt,
                                           sizeof(txt)) != 0) {
            continue;
        }
        if (kind == YETTY_YLEXBOR_BOX_KIND_INLINE_TEXT && txt[0]) {
            size_t used = strlen(joined);
            strncat(joined, txt, sizeof(joined) - used - 1);
        }
    }

    YTEST_CHECK(test, strstr(joined, "Hello") != NULL);
    YTEST_CHECK(test, strstr(joined, "bold") != NULL);
    YTEST_CHECK(test, strstr(joined, "World") != NULL);

    yetty_ylexbor_destroy(yl);
}

/*---------------------------------------------------------------------------
 * Element structure: block sibling elements each get their own box, and the
 * class attribute reads back per element. (Inline elements like <span> merge
 * into their parent's inline text runs and don't get an individually-tagged
 * box — that's the box model, so block <p>s are used for per-element counting.)
 *-------------------------------------------------------------------------*/
static void test_structure(struct ytest *test)
{
    struct yetty_ylexbor *yl = load(test, "<html><body><div id='root'>"
                                          "<p class='x'>1</p>"
                                          "<p class='x'>2</p>"
                                          "<p class='y'>3</p>"
                                          "</div></body></html>");

    YTEST_CHECK_EQ_INT(test, count_tag(yl, "p"), 3);
    YTEST_CHECK(test, find_tag(yl, "div", 0) >= 0);

    int p0 = find_tag(yl, "p", 0);
    YTEST_REQUIRE(test, p0 >= 0);
    char buf[32];
    int rc = yetty_ylexbor_test_box_attr_at(yl, p0, "class", buf, sizeof(buf));
    YTEST_CHECK_EQ_INT(test, rc, 0);
    YTEST_CHECK(test, strcmp(buf, "x") == 0);

    yetty_ylexbor_destroy(yl);
}

/*---------------------------------------------------------------------------
 * Two block siblings each get their own box.
 *-------------------------------------------------------------------------*/
static void test_siblings(struct ytest *test)
{
    struct yetty_ylexbor *yl =
        load(test, "<html><body><div id='box'><p>one</p><p>two</p></div></body></html>");
    YTEST_CHECK_EQ_INT(test, count_tag(yl, "p"), 2);
    yetty_ylexbor_destroy(yl);
}

/*---------------------------------------------------------------------------
 * Constraint Validation API (HTML forms) on form controls.
 *
 * Regression pin for the accounts.google.com / YouTube sign-in flow: the
 * GlifWebSignIn identifier step reads `input.validity.badInput`. When the whole
 * Constraint Validation API was missing, that read threw
 * `TypeError: cannot read property 'badInput' of undefined`; boq caught it
 * (reporting to /jserror) and the flow never advanced past the email screen —
 * clicking "Next" silently did nothing. The API is installed on
 * Element.prototype (gated to listed form controls). One JS program covers the
 * surface and semantics and returns "PASS" or a "FAIL …" reason.
 *-------------------------------------------------------------------------*/
static void test_constraint_validation(struct ytest *test)
{
    /* A <script> forces the JS context to initialize even on a static page. */
    struct yetty_ylexbor *yl = load(test, "<html><body><script>1;</script></body></html>");

    static const char *program =
        "(function(){"
        "  var i=document.createElement('input');"
        "  if(typeof i.validity!=='object')return 'FAIL validity-type '+typeof i.validity;"
        "  if(i.validity.badInput!==false)return 'FAIL badInput';"
        "  if(i.validity.valid!==true)return 'FAIL default-valid';"
        "  if(typeof i.checkValidity!=='function')return 'FAIL checkValidity-missing';"
        "  if(typeof i.setCustomValidity!=='function')return 'FAIL setCustomValidity-missing';"
        "  if(document.createElement('div').validity!==undefined)return 'FAIL div-gate';"
        "  var r=document.createElement('input');r.setAttribute('required','');"
        "  if(r.validity.valueMissing!==true||r.validity.valid!==false)return 'FAIL "
        "required-empty';"
        "  r.value='x';if(r.validity.valueMissing!==false||r.validity.valid!==true)"
        "    return 'FAIL required-filled';"
        "  var em=document.createElement('input');em.type='email';em.value='bad';"
        "  if(em.validity.typeMismatch!==true)return 'FAIL email-bad';"
        "  em.value='a@b.com';if(em.validity.typeMismatch!==false)return 'FAIL email-good';"
        "  var c=document.createElement('input');c.setCustomValidity('nope');"
        "  if(c.validity.customError!==true||c.validationMessage!=='nope')return 'FAIL custom-set';"
        "  var fired=false;c.addEventListener('invalid',function(){fired=true;});"
        "  if(c.checkValidity()!==false||fired!==true)return 'FAIL invalid-event';"
        "  c.setCustomValidity('');if(c.validity.customError!==false||c.validity.valid!==true)"
        "    return 'FAIL custom-clear';"
        "  return 'PASS';"
        "})()";

    struct yetty_ycore_char_ptr_result ev = yetty_ylexbor_eval_js(yl, program);
    YTEST_REQUIRE_OK(test, ev);
    YTEST_REQUIRE(test, ev.value != NULL);
    /* eval_js stringifies the JS result JSON-style, so a string result arrives
     * quoted (`"PASS"`). On any assertion miss the program returns a
     * "FAIL <reason>" string instead; matching on the PASS substring is robust
     * to the quoting and never matches a FAIL result. */
    YTEST_CHECK(test, strstr(ev.value, "PASS") != NULL);
    if (strstr(ev.value, "PASS") == NULL) {
        fprintf(stderr, "constraint-validation FAILED: %s\n", ev.value);
    }
    if (ev.value) {
        free(ev.value);
    }

    yetty_ylexbor_destroy(yl);
}

int main(void)
{
    struct ytest test = ytest_begin("ybrowser_dom");
    YTEST_RUN(&test, test_attributes);
    YTEST_RUN(&test, test_textcontent);
    YTEST_RUN(&test, test_structure);
    YTEST_RUN(&test, test_siblings);
    YTEST_RUN(&test, test_constraint_validation);
    return ytest_end(&test);
}
