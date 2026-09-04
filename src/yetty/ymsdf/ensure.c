/*
 * ensure.c - generate-unless-cached front for the polymorphic generator.
 *
 * A CDB is a cache keyed by its path: consumers gate on file existence and
 * never re-check contents. Both backends write their output in place,
 * truncating first, so a crash mid-generation would leave a short file at
 * the cache path that every later run mistakes for a hit. This front builds
 * the atlas inside a private scratch directory next to the destination and
 * renames the finished file into place, so the cache path only ever holds a
 * complete atlas. The scratch directory is claimed with an exclusive mkdir,
 * so two processes racing on the same first run each build their own copy
 * and the last rename wins with a complete file either way.
 *
 * The batch form builds several atlases at once. With a staged backend the
 * CPU stages (outlines, layout; the CDB write) of every miss run on their
 * own threads while the render stage — the only one that touches the
 * device — runs on the calling thread, one font after the other. Used at
 * yetty startup to build the default terminal faces from the raw TTF/OTF
 * fonts when the install (yinstall-min) ships no pre-generated CDBs.
 */

#include <yetty/ymsdf/generator.h>
#include <yetty/yplatform/fs.h>
#include <yetty/yplatform/thread.h>
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    YMSDF_ENSURE_PATH_MAX = 1024,
    YMSDF_ENSURE_STEM_MAX = 256,
    YMSDF_ENSURE_CLAIM_ATTEMPTS = 32,
};

/* The part of `path` after the last '/'. */
static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Basename of `path` with its extension dropped, into `out`; 0 on success. */
static int path_stem(const char *path, char *out, size_t out_size)
{
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len + 1 > out_size) {
        return -1;
    }
    memcpy(out, base, len);
    out[len] = '\0';
    return 0;
}

/* Claim a fresh scratch directory beside the destination. The exclusive
 * mkdir is the claim: a stale directory left by a crashed run, or one held
 * by a concurrent process, fails with EEXIST and the next suffix is tried. */
static int claim_scratch_dir(const char *dest_dir, const char *cdb_name, char *out, size_t out_size)
{
    for (int attempt = 0; attempt < YMSDF_ENSURE_CLAIM_ATTEMPTS; attempt++) {
        snprintf(out, out_size, "%s/.building-%s-%d", dest_dir, cdb_name, attempt);
        if (yetty_yplatform_mkdir(out) == 0) {
            return 0;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    return -1;
}

/* Best-effort removal of the scratch directory and whatever a generator left
 * in it: the requested name and, for the CPU backend, the TTF-named file it
 * writes before renaming to the requested stem. */
static void discard_scratch_dir(const char *scratch_dir, const char *cdb_name, const char *ttf_path)
{
    char path[YMSDF_ENSURE_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", scratch_dir, cdb_name);
    yetty_yplatform_unlink(path);

    char ttf_stem[YMSDF_ENSURE_STEM_MAX];
    if (path_stem(ttf_path, ttf_stem, sizeof(ttf_stem)) == 0) {
        snprintf(path, sizeof(path), "%s/%s.cdb", scratch_dir, ttf_stem);
        yetty_yplatform_unlink(path);
    }
    yetty_yplatform_rmdir(scratch_dir);
}

/* One atlas of a batch while it is being built. */
struct ensure_work {
    struct yetty_ymsdf_generator *generator;
    struct yetty_ymsdf_ensure_item *item;
    int building; /* a miss with a claimed scratch directory */
    char scratch_dir[YMSDF_ENSURE_PATH_MAX];
    char scratch_cdb[YMSDF_ENSURE_PATH_MAX];
    struct yetty_ymsdf_generator_config config; /* ttf_path → scratch_cdb */
    struct yetty_ymsdf_job *job;
    struct yetty_yplatform_ythread *thread;
};

/* Cache check, source check, scratch claim. Leaves `building` set when the
 * atlas has to be built; otherwise the item's outcome is final. */
static void ensure_begin(struct ensure_work *work)
{
    struct yetty_ymsdf_ensure_item *item = work->item;
    item->generated = 0;
    item->result = YETTY_OK_VOID();
    work->building = 0;

    if (!item->ttf_path || !*item->ttf_path || !item->cdb_path || !*item->cdb_path) {
        item->result =
            YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: ttf_path and cdb_path are required");
        return;
    }
    if (yetty_yplatform_file_is_regular(item->cdb_path)) {
        return; /* cache hit */
    }
    if (!work->generator) {
        item->result =
            YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: no generator to build the atlas");
        return;
    }
    if (!yetty_yplatform_file_is_regular(item->ttf_path)) {
        yerror("ymsdf ensure_cdb: source font not found: %s", item->ttf_path);
        item->result = YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: source font not found");
        return;
    }

    char dest_dir[YMSDF_ENSURE_PATH_MAX];
    if (yetty_yplatform_path_dirname(item->cdb_path, dest_dir, sizeof(dest_dir)) != 0) {
        item->result = YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: cdb_path too long");
        return;
    }
    struct yetty_ycore_void_result mkdir_res = yetty_yplatform_mkdir_p(dest_dir);
    if (YETTY_IS_ERR(mkdir_res)) {
        item->result = YETTY_ERR(
            yetty_ycore_void, "ymsdf ensure_cdb: cannot create destination directory", mkdir_res);
        return;
    }
    const char *cdb_name = path_basename(item->cdb_path);
    if (claim_scratch_dir(dest_dir, cdb_name, work->scratch_dir, sizeof(work->scratch_dir)) != 0) {
        yerror("ymsdf ensure_cdb: cannot create a scratch directory under %s: %s", dest_dir,
               strerror(errno));
        item->result =
            YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: cannot create scratch directory");
        return;
    }
    snprintf(work->scratch_cdb, sizeof(work->scratch_cdb), "%s/%s", work->scratch_dir, cdb_name);
    work->config.ttf_path = item->ttf_path;
    work->config.cdb_path = work->scratch_cdb;
    work->building = 1;
    yinfo("ymsdf: building atlas %s from %s (%s generator)", item->cdb_path, item->ttf_path,
          work->generator->ops->name(work->generator));
}

/* Move a finished atlas into place (or drop a failed one) and release the
 * scratch directory. */
static void ensure_end(struct ensure_work *work)
{
    struct yetty_ymsdf_ensure_item *item = work->item;
    if (!work->building) {
        return;
    }
    const char *cdb_name = path_basename(item->cdb_path);
    if (YETTY_IS_OK(item->result)) {
        if (rename(work->scratch_cdb, item->cdb_path) != 0) {
            /* rename refuses to replace an existing file on Windows: a
             * concurrent run may have installed its own complete copy first,
             * in which case ours is simply redundant. Anything else is a
             * real failure. */
            int rename_errno = errno;
            if (!yetty_yplatform_file_is_regular(item->cdb_path)) {
                yerror("ymsdf ensure_cdb: cannot move %s into place: %s", item->cdb_path,
                       strerror(rename_errno));
                item->result =
                    YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: cannot move atlas into place");
            }
        }
        if (YETTY_IS_OK(item->result)) {
            item->generated = 1;
        }
    }
    discard_scratch_dir(work->scratch_dir, cdb_name, item->ttf_path);
    work->building = 0;
}

/* Stage entry points, each also the body of one worker thread. */
static int ensure_prepare_run(void *arg)
{
    struct ensure_work *work = arg;
    struct yetty_ymsdf_job_ptr_result prepared =
        work->generator->ops->prepare(work->generator, &work->config);
    if (YETTY_IS_ERR(prepared)) {
        work->item->result =
            YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: prepare stage failed", prepared);
        return 0;
    }
    work->job = prepared.value;
    return 0;
}

static int ensure_finish_run(void *arg)
{
    struct ensure_work *work = arg;
    struct yetty_ycore_void_result finished =
        work->generator->ops->finish(work->generator, work->job);
    if (YETTY_IS_ERR(finished)) {
        work->item->result =
            YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: write stage failed", finished);
    }
    return 0;
}

/* Run `stage` for every item still on track, each on its own thread (inline
 * when a thread cannot be created), and wait for all of them. */
static void ensure_run_threaded(struct ensure_work *works, size_t count, int (*stage)(void *))
{
    for (size_t index = 0; index < count; index++) {
        struct ensure_work *work = &works[index];
        work->thread = NULL;
        if (!work->building || YETTY_IS_ERR(work->item->result)) {
            continue;
        }
        work->thread = yetty_yplatform_ythread_create(stage, work);
        if (!work->thread) {
            stage(work);
        }
    }
    for (size_t index = 0; index < count; index++) {
        struct ensure_work *work = &works[index];
        if (work->thread) {
            yetty_yplatform_ythread_join(work->thread);
            work->thread = NULL;
        }
    }
}

struct yetty_ycore_void_result yetty_ymsdf_generator_ensure_cdb_batch(
    struct yetty_ymsdf_generator *generator, struct yetty_ymsdf_ensure_item *items, size_t count)
{
    if (!items && count > 0) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb_batch: items are required");
    }
    if (count == 0) {
        return YETTY_OK_VOID();
    }
    struct ensure_work *works = calloc(count, sizeof(*works));
    if (!works) {
        return YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb_batch: alloc work list");
    }
    for (size_t index = 0; index < count; index++) {
        works[index].generator = generator;
        works[index].item = &items[index];
        ensure_begin(&works[index]);
    }

    int staged = generator && generator->ops->prepare && generator->ops->submit &&
                 generator->ops->readback && generator->ops->finish && generator->ops->job_destroy;
    if (staged) {
        /* CPU stage of every miss concurrently; then, on this thread, every
         * font's GPU work submitted before any is read back, so the GPU runs
         * the next font while the CPU copies the previous one; then the
         * writes concurrently. */
        ensure_run_threaded(works, count, ensure_prepare_run);
        for (size_t index = 0; index < count; index++) {
            struct ensure_work *work = &works[index];
            if (!work->building || YETTY_IS_ERR(work->item->result)) {
                continue;
            }
            struct yetty_ycore_void_result submitted = generator->ops->submit(generator, work->job);
            if (YETTY_IS_ERR(submitted)) {
                work->item->result =
                    YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: submit stage failed", submitted);
            }
        }
        for (size_t index = 0; index < count; index++) {
            struct ensure_work *work = &works[index];
            if (!work->building || YETTY_IS_ERR(work->item->result)) {
                continue;
            }
            struct yetty_ycore_void_result read = generator->ops->readback(generator, work->job);
            if (YETTY_IS_ERR(read)) {
                work->item->result =
                    YETTY_ERR(yetty_ycore_void, "ymsdf ensure_cdb: readback stage failed", read);
            }
        }
        ensure_run_threaded(works, count, ensure_finish_run);
        for (size_t index = 0; index < count; index++) {
            if (works[index].job) {
                generator->ops->job_destroy(generator, works[index].job);
                works[index].job = NULL;
            }
        }
    } else {
        for (size_t index = 0; index < count; index++) {
            struct ensure_work *work = &works[index];
            if (!work->building || YETTY_IS_ERR(work->item->result)) {
                continue;
            }
            struct yetty_ycore_void_result generated =
                generator->ops->generate(generator, &work->config);
            if (YETTY_IS_ERR(generated)) {
                work->item->result = YETTY_ERR(
                    yetty_ycore_void, "ymsdf ensure_cdb: atlas generation failed", generated);
            }
        }
    }

    for (size_t index = 0; index < count; index++) {
        ensure_end(&works[index]);
    }
    free(works);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymsdf_generator_ensure_cdb(
    struct yetty_ymsdf_generator *generator, const char *ttf_path, const char *cdb_path,
    int *out_generated)
{
    struct yetty_ymsdf_ensure_item item = {.ttf_path = ttf_path, .cdb_path = cdb_path};
    struct yetty_ycore_void_result batch =
        yetty_ymsdf_generator_ensure_cdb_batch(generator, &item, 1);
    if (out_generated) {
        *out_generated = item.generated;
    }
    if (YETTY_IS_ERR(batch)) {
        if (YETTY_IS_ERR(item.result)) {
            yetty_ycore_error_destroy(item.result.error);
        }
        return batch;
    }
    return item.result;
}
