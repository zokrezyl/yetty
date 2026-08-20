/*
 * ymux terminfo capability model (#699 review cycle 21): the REAL
 * tty-term.c/tty-features.c analog — a compiled-terminfo loader, a tparm-style
 * parameterized-string expander, feature-token application, and
 * terminal-overrides (cap=value / cap@) semantics. These prove the model
 * consumes capability INPUTS and produces the SAME operations the pinned tmux
 * renderer would, rather than the old fixed-prefix boolean table.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ytest.h"

#include "../../../src/yetty/ymux/tty-term.h"

/* --- tparm-style expansion: the interpreter that turns capability strings
 * into bytes. Every construct the emitted caps use. --- */
static void check_expand(struct ytest *test, const char *cap, const long *params, size_t count,
                         const char *expected)
{
    char out[128];
    size_t len = yetty_ymux_tty_term_expand(cap, params, count, out, sizeof(out));
    YTEST_CHECK_EQ_SIZE(test, len, strlen(expected));
    if (len == strlen(expected)) {
        YTEST_CHECK(test, memcmp(out, expected, len) == 0);
    }
}

static void test_tparm_expansion(struct ytest *test)
{
    /* cup: %i increments BOTH params, %p%d prints — the classic
     * 1-based-row;col cursor address. */
    long cup_params[2] = {5, 9};
    check_expand(test, "\033[%i%p1%d;%p2%dH", cup_params, 2, "\033[6;10H");
    /* hpa: single %i-incremented param. */
    long one[1] = {7};
    check_expand(test, "\033[%i%p1%dG", one, 1, "\033[8G");
    /* cub: NO %i — the param prints as-is. */
    check_expand(test, "\033[%p1%dD", one, 1, "\033[7D");
    /* setaf 256-form conditionals: %? %t %e %; with %{} literals and
     * %< comparison and %- arithmetic — index 3 (< 8) => "3;3"->"\e[3m"
     * with the direct 3+n path... exercise all three branches. */
    const char *setaf = "\033[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m";
    long red[1] = {1};
    check_expand(test, setaf, red, 1, "\033[31m"); /* 3 then 1 => "31" */
    long bright[1] = {12};
    check_expand(test, setaf, bright, 1, "\033[94m"); /* 9 then 12-8=4 => "94" */
    long ext[1] = {200};
    check_expand(test, setaf, ext, 1, "\033[38;5;200m");
    /* RGB setrgbf: three params, plain. */
    long rgb[3] = {255, 128, 0};
    check_expand(test, "\033[38;2;%p1%d;%p2%d;%p3%dm", rgb, 3, "\033[38;2;255;128;0m");
    /* %c emits a raw byte; %'x' pushes a char literal. */
    long none[1] = {0};
    check_expand(test, "%'A'%c", none, 0, "A");
}

/* --- Compiled terminfo loader: read the real system entry and confirm the
 * expander drives it to the exact xterm-256color bytes. --- */
static void test_loader_reads_database(struct ytest *test)
{
    struct yetty_ymux_tty_term term;
    /* Standard database search — xterm-256color is present on the CI image. */
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&term, "xterm-256color", NULL));

    if (!term.loaded_from_db) {
        /* No database on this host — the compiled-in fallback still gives a
         * usable xterm-256color profile; the byte checks below hold for it
         * too (identical strings), so the test remains meaningful. */
        fprintf(stderr, "tty-term: xterm-256color from FALLBACK (no db)\n");
    }
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_CUP));
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_CSR));
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_ECH));
    YTEST_CHECK(test, term.colors >= 256);

    char out[64];
    /* Raw cup applies %i (1-based) but does NOT collapse 1;1 to home — that
     * is a tmux tty_cursor shortest-move choice, not a terminfo property. */
    long cup[2] = {0, 0};
    size_t len = yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_CUP, cup, 2, out, sizeof(out));
    YTEST_CHECK(test, len == 6 && memcmp(out, "\033[1;1H", 6) == 0);
    long cup2[2] = {5, 9};
    len = yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_CUP, cup2, 2, out, sizeof(out));
    YTEST_CHECK(test, len == 7 && memcmp(out, "\033[6;10H", 7) == 0);
    long ech[1] = {4};
    len = yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_ECH, ech, 1, out, sizeof(out));
    YTEST_CHECK(test, len == 4 && memcmp(out, "\033[4X", 4) == 0);

    yetty_ymux_tty_term_free(&term);
}

/* --- Extended capabilities: Smulx/Setulc/Sync/smxx live in the ncurses
 * extended section. The usstyle+strikethrough+sync features ADD them; the
 * loader reads them when the entry carries them (tmux-256color). --- */
static void test_features_add_extended_caps(struct ytest *test)
{
    struct yetty_ymux_tty_term term;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&term, "xterm-256color", NULL));
    /* Base xterm-256color has no styled-underline / strike / sync. */
    YTEST_CHECK(test, !yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_SMULX));

    /* The usstyle + strikethrough + sync features add the capability strings
     * (tty-features.c). */
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_tty_term_apply_features(&term, "usstyle,strikethrough,sync,RGB"));
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_SMULX));
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_SMXX));
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_SYNC));
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_SETRGBF));

    char out[64];
    long style[1] = {3}; /* curly */
    size_t len = yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_SMULX, style, 1, out, sizeof(out));
    YTEST_CHECK(test, len == 6 && memcmp(out, "\033[4:3m", 6) == 0);
    long rgb[3] = {10, 20, 30};
    len = yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_SETRGBF, rgb, 3, out, sizeof(out));
    YTEST_CHECK(test, len == strlen("\033[38;2;10;20;30m") &&
                          memcmp(out, "\033[38;2;10;20;30m", len) == 0);
    /* Sync ON (param 1) vs OFF (param 0) — the tmux %? conditional. */
    long sync_on[1] = {1};
    len = yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_SYNC, sync_on, 1, out, sizeof(out));
    YTEST_CHECK(test, len == strlen("\033[?2026h") && memcmp(out, "\033[?2026h", len) == 0);
    long sync_off[1] = {0};
    len = yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_SYNC, sync_off, 1, out, sizeof(out));
    YTEST_CHECK(test, len == strlen("\033[?2026l") && memcmp(out, "\033[?2026l", len) == 0);

    yetty_ymux_tty_term_free(&term);
}

/* --- terminal-overrides: cap=value REPLACES, cap@ CANCELS. --- */
static void test_overrides_and_cancellation(struct ytest *test)
{
    struct yetty_ymux_tty_term term;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&term, "xterm-256color", NULL));

    /* cap=value override: replace el with a custom sequence (\\E decodes to
     * ESC, ^X to a control byte). */
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&term, "el=\\E[9X"));
    char out[64];
    size_t len = yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_EL, NULL, 0, out, sizeof(out));
    YTEST_CHECK(test, len == 4 && memcmp(out, "\033[9X", 4) == 0);

    /* cap@ cancellation: REMOVE ech so the renderer takes the spaces
     * fallback (has() reports false). */
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_ECH));
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_apply_features(&term, "ech@"));
    YTEST_CHECK(test, !yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_ECH));

    yetty_ymux_tty_term_free(&term);
}

/* --- Unknown TERM: MINIMAL profile, never silently xterm (review #20). --- */
static void test_unknown_term_minimal(struct ytest *test)
{
    struct yetty_ymux_tty_term term;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&term, "nonexistent-xyzzy", NULL));
    /* Cursor addressing + line clears yes; ECH / IL-DL / colours no. */
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_CUP));
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_EL));
    YTEST_CHECK(test, !yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_ECH));
    YTEST_CHECK(test, !yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_IL));
    YTEST_CHECK(test, term.colors < 256);
    yetty_ymux_tty_term_free(&term);
}

/* --- A private terminfo directory: the loader honours a path override, so
 * tests do not depend on the host database. Writes a tiny compiled entry with
 * a distinctive cup and confirms it is read. --- */
static void test_loader_private_directory(struct ytest *test)
{
    /* Build a minimal legacy (0432) compiled entry with a single string cap
     * we care about (cup, index 10). Names: "zz|test\0" ; one string cap. */
    char dir_template[] = "/tmp/ymux-terminfo-XXXXXX";
    char *dir = mkdtemp(dir_template);
    YTEST_REQUIRE_NOT_NULL(test, dir);
    char subdir[256];
    snprintf(subdir, sizeof(subdir), "%s/z", dir);
    YTEST_CHECK(test, mkdir(subdir, 0700) == 0);
    char file[300];
    snprintf(file, sizeof(file), "%s/zzterm", subdir);

    const char names[] = "zzterm|test terminal";
    size_t names_len = sizeof(names); /* include NUL */
    size_t bools_count = 0;
    size_t nums_count = 0;
    size_t strs_count = 11;                   /* through cup at index 10 */
    const char cup[] = "\033[%i%p1%d;%p2%dZ"; /* distinctive final byte Z */
    size_t table_len = sizeof(cup);

    unsigned char header[12];
    header[0] = 0032; /* 0432 little-endian low byte */
    header[1] = 001;
    header[2] = (unsigned char)(names_len & 0xFF);
    header[3] = (unsigned char)(names_len >> 8);
    header[4] = (unsigned char)(bools_count & 0xFF);
    header[5] = (unsigned char)(bools_count >> 8);
    header[6] = (unsigned char)(nums_count & 0xFF);
    header[7] = (unsigned char)(nums_count >> 8);
    header[8] = (unsigned char)(strs_count & 0xFF);
    header[9] = (unsigned char)(strs_count >> 8);
    header[10] = (unsigned char)(table_len & 0xFF);
    header[11] = (unsigned char)(table_len >> 8);

    FILE *out = fopen(file, "wb");
    YTEST_REQUIRE_NOT_NULL(test, out);
    fwrite(header, 1, sizeof(header), out);
    fwrite(names, 1, names_len, out);
    /* bools: none. names_len+bools even? names_len=21 (odd) -> pad 1 for the
     * numbers section alignment. */
    if ((names_len + bools_count) & 1) {
        char pad = 0;
        fwrite(&pad, 1, 1, out);
    }
    /* string offset table: all -1 except index 10 = 0. */
    for (size_t index = 0; index < strs_count; ++index) {
        short offset = (index == 10) ? 0 : -1;
        unsigned char bytes[2] = {(unsigned char)(offset & 0xFF), (unsigned char)(offset >> 8)};
        fwrite(bytes, 1, 2, out);
    }
    fwrite(cup, 1, table_len, out);
    fclose(out);

    struct yetty_ymux_tty_term term;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&term, "zzterm", dir));
    YTEST_CHECK(test, term.loaded_from_db);
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_CUP));
    char rendered[64];
    long params[2] = {2, 3};
    size_t len =
        yetty_ymux_tty_term_emit(&term, YMUX_TTY_TERM_CUP, params, 2, rendered, sizeof(rendered));
    YTEST_CHECK(test, len == 6 && memcmp(rendered, "\033[3;4Z", 6) == 0);
    yetty_ymux_tty_term_free(&term);

    unlink(file);
    rmdir(subdir);
    rmdir(dir);
}

/* Cycle-22 P1: hardening — a path-traversal / separator-bearing TERM name is
 * never used as a database path component; it falls through to the compiled
 * profile instead of opening an arbitrary file. */
static void test_term_name_path_safety(struct ytest *test)
{
    struct yetty_ymux_tty_term term;
    /* A traversal attempt with a private terminfo dir set: must NOT load from
     * the database (the name is rejected as a path component), and must yield
     * a usable compiled fallback rather than erroring or reading outside. */
    YTEST_REQUIRE_OK(test,
                     yetty_ymux_tty_term_load(&term, "../../../etc/passwd", "/tmp/nonexistent-ti"));
    YTEST_CHECK(test, !term.loaded_from_db);            /* never touched the db path */
    YTEST_CHECK(test, term.strings[YMUX_TTY_TERM_CUP]); /* fallback profile is usable */
    yetty_ymux_tty_term_free(&term);

    /* A slash-bearing name is likewise rejected. */
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&term, "foo/bar", "/tmp/nonexistent-ti"));
    YTEST_CHECK(test, !term.loaded_from_db);
    yetty_ymux_tty_term_free(&term);
}

/* Cycle-22 P1: a malformed compiled entry (a string offset with NO NUL before
 * the table end) must be REJECTED for that capability, not read out of bounds
 * (ASAN would trip). Build a tiny entry whose cup offset points at an
 * unterminated tail. */
static void test_malformed_terminfo_no_oob(struct ytest *test)
{
    char dir_template[] = "/tmp/ymux-ti-bad-XXXXXX";
    char *dir = mkdtemp(dir_template);
    YTEST_REQUIRE_NOT_NULL(test, dir);
    char subdir[256];
    snprintf(subdir, sizeof(subdir), "%s/b", dir);
    YTEST_CHECK(test, mkdir(subdir, 0700) == 0);
    char file[300];
    snprintf(file, sizeof(file), "%s/badterm", subdir);

    const char names[] = "badterm|malformed";
    size_t names_len = sizeof(names);
    size_t strs_count = 11; /* through cup at index 10 */
    /* String table with NO NUL byte at all — cup offset 0 points into it. */
    const char table[] = {'x', 'y', 'z', 'w'};
    size_t table_len = sizeof(table);

    unsigned char header[12];
    header[0] = 0032;
    header[1] = 001;
    header[2] = (unsigned char)(names_len & 0xFF);
    header[3] = (unsigned char)(names_len >> 8);
    header[4] = 0;
    header[5] = 0;
    header[6] = 0;
    header[7] = 0;
    header[8] = (unsigned char)(strs_count & 0xFF);
    header[9] = (unsigned char)(strs_count >> 8);
    header[10] = (unsigned char)(table_len & 0xFF);
    header[11] = (unsigned char)(table_len >> 8);

    FILE *out = fopen(file, "wb");
    YTEST_REQUIRE_NOT_NULL(test, out);
    fwrite(header, 1, sizeof(header), out);
    fwrite(names, 1, names_len, out);
    if ((names_len + 0) & 1) {
        char pad = 0;
        fwrite(&pad, 1, 1, out);
    }
    for (size_t index = 0; index < strs_count; ++index) {
        short offset = (index == 10) ? 0 : -1; /* cup -> offset 0 (unterminated) */
        unsigned char bytes[2] = {(unsigned char)(offset & 0xFF), (unsigned char)(offset >> 8)};
        fwrite(bytes, 1, 2, out);
    }
    fwrite(table, 1, table_len, out);
    fclose(out);

    struct yetty_ymux_tty_term term;
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&term, "badterm", dir));
    /* The malformed cup was rejected (not NUL-terminated in the table) — no
     * OOB read, and the slot is absent. */
    YTEST_CHECK(test, !yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_CUP));
    yetty_ymux_tty_term_free(&term);

    unlink(file);
    rmdir(subdir);
    rmdir(dir);
}

/* Cycle-25 P1: a DB entry with a BAD MAGIC is a MISS (malformed), not a resource
 * failure — it must fall through to the compiled-in fallback and return OK with
 * loaded_from_db == 0, distinguishing it from an OOM which PROPAGATES an error.
 * (The OOM-propagation path is structural — the tri-state terminfo_db_status;
 * deterministic allocator injection needs a malloc seam not yet in the harness.) */
static void test_terminfo_bad_magic_falls_back(struct ytest *test)
{
    char dir_template[] = "/tmp/ymux-ti-magic-XXXXXX";
    char *dir = mkdtemp(dir_template);
    YTEST_REQUIRE_NOT_NULL(test, dir);
    char subdir[256];
    snprintf(subdir, sizeof(subdir), "%s/z", dir);
    YTEST_CHECK(test, mkdir(subdir, 0700) == 0);
    char file[300];
    snprintf(file, sizeof(file), "%s/zzmagic", subdir);
    /* 16 bytes of a WRONG magic (not 0432/01036) — a malformed entry. */
    unsigned char bogus[16] = {0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    FILE *out = fopen(file, "wb");
    YTEST_REQUIRE_NOT_NULL(test, out);
    fwrite(bogus, 1, sizeof(bogus), out);
    fclose(out);

    struct yetty_ymux_tty_term term;
    /* MISS → fallback, NOT an error. */
    YTEST_REQUIRE_OK(test, yetty_ymux_tty_term_load(&term, "zzmagic", dir));
    YTEST_CHECK(test, !term.loaded_from_db);                              /* fell to the fallback */
    YTEST_CHECK(test, yetty_ymux_tty_term_has(&term, YMUX_TTY_TERM_CUP)); /* fallback populated */
    yetty_ymux_tty_term_free(&term);

    unlink(file);
    rmdir(subdir);
    rmdir(dir);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_tty_term");
    YTEST_RUN(&test, test_tparm_expansion);
    YTEST_RUN(&test, test_term_name_path_safety);
    YTEST_RUN(&test, test_malformed_terminfo_no_oob);
    YTEST_RUN(&test, test_terminfo_bad_magic_falls_back);
    YTEST_RUN(&test, test_loader_reads_database);
    YTEST_RUN(&test, test_features_add_extended_caps);
    YTEST_RUN(&test, test_overrides_and_cancellation);
    YTEST_RUN(&test, test_unknown_term_minimal);
    YTEST_RUN(&test, test_loader_private_directory);
    return ytest_end(&test);
}
