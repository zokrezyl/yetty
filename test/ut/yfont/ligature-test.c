/*
 * ligature-test.c - Programming-ligature table contract test
 *
 * Pure, font-free coverage of yetty_yfont_ligature_length_at: the deterministic
 * table the grid-suppression pass and the SDF-shaping pass share to agree on
 * which cells a ligature covers. Asserts detection, longest-match precedence,
 * position/bounds handling, and that ordinary text never matches.
 */

#include <ytest.h>

#include <yetty/yfont/font.h>

#include <string.h>

/* Build a codepoint array from an ASCII string and query the matcher at pos. */
static size_t ligature_len(const char *text, size_t pos)
{
    uint32_t codepoints[64];
    size_t count = strlen(text);
    if (count > 64) {
        count = 64;
    }
    for (size_t i = 0; i < count; i++) {
        codepoints[i] = (uint32_t)(unsigned char)text[i];
    }
    return yetty_yfont_ligature_length_at(codepoints, count, pos);
}

static void test_basic_detection(struct ytest *test)
{
    YTEST_CHECK_EQ_INT(test, ligature_len("=>", 0), 2);
    YTEST_CHECK_EQ_INT(test, ligature_len("->", 0), 2);
    YTEST_CHECK_EQ_INT(test, ligature_len("!=", 0), 2);
    YTEST_CHECK_EQ_INT(test, ligature_len("<-", 0), 2);
    YTEST_CHECK_EQ_INT(test, ligature_len("||", 0), 2);
    YTEST_CHECK_EQ_INT(test, ligature_len("//", 0), 2);
}

static void test_longest_match(struct ytest *test)
{
    /* "===" must win over the "==" prefix, "<==>" over "<==". */
    YTEST_CHECK_EQ_INT(test, ligature_len("===", 0), 3);
    YTEST_CHECK_EQ_INT(test, ligature_len("!==", 0), 3);
    YTEST_CHECK_EQ_INT(test, ligature_len("<==>", 0), 4);
    YTEST_CHECK_EQ_INT(test, ligature_len("<-->", 0), 4);
    /* A longer run than any table entry: match the longest prefix, not the run. */
    YTEST_CHECK_EQ_INT(test, ligature_len("====", 0), 3);
}

static void test_no_false_positives(struct ytest *test)
{
    /* Ordinary words, identifiers and numbers must never match. */
    YTEST_CHECK_EQ_INT(test, ligature_len("ab", 0), 0);
    YTEST_CHECK_EQ_INT(test, ligature_len("hello", 0), 0);
    YTEST_CHECK_EQ_INT(test, ligature_len("x1", 0), 0);
    YTEST_CHECK_EQ_INT(test, ligature_len("42", 0), 0);
    /* A single operator char is not a ligature (needs at least two cells). */
    YTEST_CHECK_EQ_INT(test, ligature_len("=", 0), 0);
    YTEST_CHECK_EQ_INT(test, ligature_len(">", 0), 0);
}

static void test_position_and_bounds(struct ytest *test)
{
    /* Matcher looks at the run starting exactly at pos. */
    YTEST_CHECK_EQ_INT(test, ligature_len("x => y", 2), 2); /* the "=>" */
    YTEST_CHECK_EQ_INT(test, ligature_len("x => y", 0), 0); /* the 'x'  */
    YTEST_CHECK_EQ_INT(test, ligature_len("a->b", 1), 2);   /* the "->" */

    /* A partial ligature at the very end must not over-read past count. */
    uint32_t two[] = {'=', '='};
    YTEST_CHECK_EQ_INT(test, yetty_yfont_ligature_length_at(two, 2, 0), 2);
    YTEST_CHECK_EQ_INT(test, yetty_yfont_ligature_length_at(two, 1, 0), 0); /* only "=" visible */

    /* pos at/after count, and NULL input, are safe no-ops. */
    YTEST_CHECK_EQ_INT(test, yetty_yfont_ligature_length_at(two, 2, 2), 0);
    YTEST_CHECK_EQ_INT(test, yetty_yfont_ligature_length_at(NULL, 0, 0), 0);
}

int main(void)
{
    struct ytest test = ytest_begin("yfont_ligature");
    test_basic_detection(&test);
    test_longest_match(&test);
    test_no_false_positives(&test);
    test_position_and_bounds(&test);
    return ytest_end(&test);
}
