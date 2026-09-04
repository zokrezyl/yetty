/*
 * ensure-test.c — yetty_ymsdf_generator_ensure_cdb through the CPU backend.
 *
 * Covers the cache contract the startup atlas step relies on: a miss builds
 * the atlas into place and leaves no scratch directory behind; a hit does no
 * work and needs no generator; a stale scratch directory from a crashed run
 * does not block the next build; a CDB stem that differs from the font stem
 * (the Emmentaler case) lands under the requested name; a font the backend
 * rejects leaves the cache path empty rather than poisoned.
 */

#include "ytest.h"

#include <yetty/ymsdf/generator.h>
#include <yetty/yplatform/fs.h>

#include <stdio.h>
#include <string.h>

#ifndef YMSDF_TEST_FONT
#error "YMSDF_TEST_FONT must name a small TTF to generate from"
#endif

enum {
    TEST_PATH_MAX = 1024,
};

/* Remove `path` and everything beneath it (files and nested directories). */
static void remove_tree(const char *path)
{
    struct yetty_yplatform_dir *handle = yetty_yplatform_dir_open(path);
    if (!handle) {
        yetty_yplatform_unlink(path);
        return;
    }
    struct yetty_yplatform_dir_entry entry;
    while (yetty_yplatform_dir_next(handle, &entry)) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) {
            continue;
        }
        char child[TEST_PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", path, entry.name);
        if (entry.is_dir) {
            remove_tree(child);
        } else {
            yetty_yplatform_unlink(child);
        }
    }
    yetty_yplatform_dir_close(handle);
    yetty_yplatform_rmdir(path);
}

/* Number of entries in `dir` whose name starts with `prefix`. */
static int count_entries_with_prefix(const char *dir, const char *prefix)
{
    struct yetty_yplatform_dir *handle = yetty_yplatform_dir_open(dir);
    if (!handle) {
        return 0;
    }
    int count = 0;
    struct yetty_yplatform_dir_entry entry;
    size_t prefix_len = strlen(prefix);
    while (yetty_yplatform_dir_next(handle, &entry)) {
        if (strncmp(entry.name, prefix, prefix_len) == 0) {
            count++;
        }
    }
    yetty_yplatform_dir_close(handle);
    return count;
}

/* A fresh work directory for one test, wiped of anything a previous run left. */
static void fresh_work_dir(struct ytest *test, const char *name, char *out, size_t out_size)
{
    snprintf(out, out_size, "ymsdf-ensure-test/%s", name);
    remove_tree(out);
    struct yetty_ycore_void_result mkdir_res = yetty_yplatform_mkdir_p(out);
    YTEST_REQUIRE_OK(test, mkdir_res);
}

static long file_size(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size;
}

static void write_text_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    if (file) {
        fputs(text, file);
        fclose(file);
    }
}

static void test_miss_builds_atlas_then_hits(struct ytest *test)
{
    char work[TEST_PATH_MAX];
    fresh_work_dir(test, "miss-then-hit", work, sizeof(work));
    char dest_dir[TEST_PATH_MAX];
    char cdb_path[TEST_PATH_MAX];
    snprintf(dest_dir, sizeof(dest_dir), "%s/msdf-fonts", work);
    snprintf(cdb_path, sizeof(cdb_path), "%s/Ahem.cdb", dest_dir);

    struct yetty_ymsdf_generator_ptr_result generator_res = yetty_ymsdf_generator_create_cpu();
    YTEST_REQUIRE_OK(test, generator_res);
    struct yetty_ymsdf_generator *generator = generator_res.value;

    /* Miss: the destination directory does not even exist yet. */
    int generated = -1;
    struct yetty_ycore_void_result miss_res =
        yetty_ymsdf_generator_ensure_cdb(generator, YMSDF_TEST_FONT, cdb_path, &generated);
    YTEST_REQUIRE_OK(test, miss_res);
    YTEST_CHECK_EQ_INT(test, generated, 1);
    YTEST_CHECK(test, yetty_yplatform_file_is_regular(cdb_path));
    long atlas_size = file_size(cdb_path);
    YTEST_CHECK(test, atlas_size > 0);
    int scratch_dirs = count_entries_with_prefix(dest_dir, ".building-");
    YTEST_CHECK_EQ_INT(test, scratch_dirs, 0);

    /* Hit: no generator needed, nothing rewritten. */
    generated = -1;
    struct yetty_ycore_void_result hit_res =
        yetty_ymsdf_generator_ensure_cdb(NULL, YMSDF_TEST_FONT, cdb_path, &generated);
    YTEST_REQUIRE_OK(test, hit_res);
    YTEST_CHECK_EQ_INT(test, generated, 0);
    long size_after_hit = file_size(cdb_path);
    YTEST_CHECK(test, size_after_hit == atlas_size);

    generator->ops->destroy(generator);
}

static void test_missing_font_fails_cleanly(struct ytest *test)
{
    char work[TEST_PATH_MAX];
    fresh_work_dir(test, "missing-font", work, sizeof(work));
    char dest_dir[TEST_PATH_MAX];
    char cdb_path[TEST_PATH_MAX];
    char font_path[TEST_PATH_MAX];
    snprintf(dest_dir, sizeof(dest_dir), "%s/msdf-fonts", work);
    snprintf(cdb_path, sizeof(cdb_path), "%s/Nope.cdb", dest_dir);
    snprintf(font_path, sizeof(font_path), "%s/Nope.ttf", work);

    struct yetty_ymsdf_generator_ptr_result generator_res = yetty_ymsdf_generator_create_cpu();
    YTEST_REQUIRE_OK(test, generator_res);
    struct yetty_ymsdf_generator *generator = generator_res.value;

    int generated = -1;
    struct yetty_ycore_void_result res =
        yetty_ymsdf_generator_ensure_cdb(generator, font_path, cdb_path, &generated);
    YTEST_CHECK_ERR(test, res); /* the harness releases the error chain */
    YTEST_CHECK_EQ_INT(test, generated, 0);
    YTEST_CHECK(test, !yetty_yplatform_file_exists(cdb_path));

    /* A miss with no generator is an error too, not a silent no-op. */
    struct yetty_ycore_void_result no_generator_res =
        yetty_ymsdf_generator_ensure_cdb(NULL, YMSDF_TEST_FONT, cdb_path, &generated);
    YTEST_CHECK_ERR(test, no_generator_res);
    YTEST_CHECK(test, !yetty_yplatform_file_exists(cdb_path));

    generator->ops->destroy(generator);
}

static void test_bad_font_leaves_no_trace(struct ytest *test)
{
    char work[TEST_PATH_MAX];
    fresh_work_dir(test, "bad-font", work, sizeof(work));
    char dest_dir[TEST_PATH_MAX];
    char cdb_path[TEST_PATH_MAX];
    char font_path[TEST_PATH_MAX];
    snprintf(dest_dir, sizeof(dest_dir), "%s/msdf-fonts", work);
    snprintf(cdb_path, sizeof(cdb_path), "%s/Junk.cdb", dest_dir);
    snprintf(font_path, sizeof(font_path), "%s/Junk.ttf", work);
    write_text_file(font_path, "this is not a font\n");

    struct yetty_ymsdf_generator_ptr_result generator_res = yetty_ymsdf_generator_create_cpu();
    YTEST_REQUIRE_OK(test, generator_res);
    struct yetty_ymsdf_generator *generator = generator_res.value;

    int generated = -1;
    struct yetty_ycore_void_result res =
        yetty_ymsdf_generator_ensure_cdb(generator, font_path, cdb_path, &generated);
    YTEST_CHECK_ERR(test, res); /* the harness releases the error chain */
    YTEST_CHECK_EQ_INT(test, generated, 0);
    /* The cache path must stay empty — never a truncated atlas — and the
     * scratch directory must be gone. */
    YTEST_CHECK(test, !yetty_yplatform_file_exists(cdb_path));
    int scratch_dirs = count_entries_with_prefix(dest_dir, ".building-");
    YTEST_CHECK_EQ_INT(test, scratch_dirs, 0);

    generator->ops->destroy(generator);
}

static void test_stale_scratch_dir_does_not_block(struct ytest *test)
{
    char work[TEST_PATH_MAX];
    fresh_work_dir(test, "stale-scratch", work, sizeof(work));
    char dest_dir[TEST_PATH_MAX];
    char cdb_path[TEST_PATH_MAX];
    char stale_dir[TEST_PATH_MAX];
    char stale_file[TEST_PATH_MAX];
    snprintf(dest_dir, sizeof(dest_dir), "%s/msdf-fonts", work);
    snprintf(cdb_path, sizeof(cdb_path), "%s/Ahem.cdb", dest_dir);
    /* What a crashed earlier run leaves behind: the first scratch slot, holding
     * a partial atlas. */
    snprintf(stale_dir, sizeof(stale_dir), "%s/.building-Ahem.cdb-0", dest_dir);
    snprintf(stale_file, sizeof(stale_file), "%s/Ahem.cdb", stale_dir);
    struct yetty_ycore_void_result stale_res = yetty_yplatform_mkdir_p(stale_dir);
    YTEST_REQUIRE_OK(test, stale_res);
    write_text_file(stale_file, "partial");

    struct yetty_ymsdf_generator_ptr_result generator_res = yetty_ymsdf_generator_create_cpu();
    YTEST_REQUIRE_OK(test, generator_res);
    struct yetty_ymsdf_generator *generator = generator_res.value;

    int generated = -1;
    struct yetty_ycore_void_result res =
        yetty_ymsdf_generator_ensure_cdb(generator, YMSDF_TEST_FONT, cdb_path, &generated);
    YTEST_REQUIRE_OK(test, res);
    YTEST_CHECK_EQ_INT(test, generated, 1);
    YTEST_CHECK(test, yetty_yplatform_file_is_regular(cdb_path));
    long atlas_size = file_size(cdb_path);
    YTEST_CHECK(test, atlas_size > (long)strlen("partial"));
    /* The stale slot is left alone (it may belong to a live process); only
     * the slot this run claimed is cleaned up. */
    YTEST_CHECK(test, yetty_yplatform_file_is_regular(stale_file));
    int scratch_dirs = count_entries_with_prefix(dest_dir, ".building-");
    YTEST_CHECK_EQ_INT(test, scratch_dirs, 1);

    generator->ops->destroy(generator);
}

static void test_cdb_stem_differs_from_font_stem(struct ytest *test)
{
    char work[TEST_PATH_MAX];
    fresh_work_dir(test, "renamed-stem", work, sizeof(work));
    char dest_dir[TEST_PATH_MAX];
    char cdb_path[TEST_PATH_MAX];
    char font_named_cdb[TEST_PATH_MAX];
    snprintf(dest_dir, sizeof(dest_dir), "%s/msdf-fonts", work);
    snprintf(cdb_path, sizeof(cdb_path), "%s/Music.cdb", dest_dir);
    snprintf(font_named_cdb, sizeof(font_named_cdb), "%s/Ahem.cdb", dest_dir);

    struct yetty_ymsdf_generator_ptr_result generator_res = yetty_ymsdf_generator_create_cpu();
    YTEST_REQUIRE_OK(test, generator_res);
    struct yetty_ymsdf_generator *generator = generator_res.value;

    int generated = -1;
    struct yetty_ycore_void_result res =
        yetty_ymsdf_generator_ensure_cdb(generator, YMSDF_TEST_FONT, cdb_path, &generated);
    YTEST_REQUIRE_OK(test, res);
    YTEST_CHECK_EQ_INT(test, generated, 1);
    YTEST_CHECK(test, yetty_yplatform_file_is_regular(cdb_path));
    /* The CPU backend first writes <font stem>.cdb; that intermediate must
     * never surface in the destination directory. */
    YTEST_CHECK(test, !yetty_yplatform_file_exists(font_named_cdb));
    int scratch_dirs = count_entries_with_prefix(dest_dir, ".building-");
    YTEST_CHECK_EQ_INT(test, scratch_dirs, 0);

    generator->ops->destroy(generator);
}

int main(void)
{
    struct ytest test = ytest_begin("ymsdf_ensure");
    YTEST_RUN(&test, test_miss_builds_atlas_then_hits);
    YTEST_RUN(&test, test_missing_font_fails_cleanly);
    YTEST_RUN(&test, test_bad_font_leaves_no_trace);
    YTEST_RUN(&test, test_stale_scratch_dir_does_not_block);
    YTEST_RUN(&test, test_cdb_stem_differs_from_font_stem);
    return ytest_end(&test);
}
