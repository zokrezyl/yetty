/* repo.c — ygit repository object (yclass class `ygit:repo`).
 *
 * The reusable core of the ygit tool: a repository handle that exposes
 * read-only history and inspection queries (status, branches, log/DAG, commit
 * detail). Authored as a yclass class so codegen emits the dispatch glue,
 * model.yaml, RPC skeletons, and FFI/host-language bindings — i.e. so the same
 * object can be driven from C, proxied over RPC, or bound from another language
 * and embedded in a future interactive pane.
 *
 * The heavy lifting (libgit2) lives in git-backend.c; this class owns the
 * repository path and turns each query into a call there. The queries return
 * the owned structs declared in git-backend.h, which the generated public
 * header pulls in via the `include@` annotation below.
 */

#include <yetty/ygit/git-backend.h>

#include <yetty/yclass/class.h>

#include <stdlib.h>
#include <string.h>

/* This TU deliberately does NOT include its own generated header (repo.h) —
 * that header is a downstream artifact for other modules and would redefine the
 * YETTY_YRESULT_DECLARE this TU declares manually below. The class handle
 * Result plus the codegen accessor/downcast that the appended repo.gen.c
 * defines are declared here so the foot include and the impls have them in
 * scope. The generated public header publishes the identical declarations for
 * consumers. */
YETTY_YRESULT_DECLARE(yetty_ygit_repo_ptr, struct yetty_ygit_repo *);
struct yetty_yclass_ptr_result yetty_ygit_repo_class_get(void);
struct yetty_ygit_repo_ptr_result yetty_ygit_repo_from(struct yetty_yclass_object *obj);
/* Generated domain destructor dispatcher, invoked from the exposed destroy. */
struct yetty_ycore_void_result yetty_ygit_destructor(struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@ygit:repo") YETTY_ANNOTATE("include@yetty/ygit/git-backend.h")
    yetty_ygit_repo {
    char *path; /* absolute or relative path into the working tree / repo */
};

YETTY_ANNOTATE("virtual@ygit:repo:constructor")
static struct yetty_ycore_void_result repo_constructor(struct yetty_yclass_object *obj)
{
    struct yetty_ygit_repo_ptr_result repo_res = yetty_ygit_repo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, repo_res, "repo_constructor: from_obj");
    repo_res.value->path = NULL;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@ygit:repo:destructor")
static struct yetty_ycore_void_result repo_destructor(struct yetty_yclass_object *obj)
{
    struct yetty_ygit_repo_ptr_result repo_res = yetty_ygit_repo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, repo_res, "repo_destructor: from_obj");
    free(repo_res.value->path);
    repo_res.value->path = NULL;
    return YETTY_OK_VOID();
}

/* Point the repository at `path`. Does not open it eagerly — each query opens
 * and closes the repository so a long-lived handle never pins an fd. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygit_repo_open(struct yetty_yclass_object *obj,
                                                    const char *path)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygit_repo_open: NULL object");
    }
    if (!path) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygit_repo_open: NULL path");
    }
    struct yetty_ygit_repo_ptr_result repo_res = yetty_ygit_repo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, repo_res, "yetty_ygit_repo_open: from_obj");
    struct yetty_ygit_repo *repo = repo_res.value;

    size_t length = strlen(path);
    char *copy = malloc(length + 1);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygit_repo_open: out of memory");
    }
    memcpy(copy, path, length + 1);
    free(repo->path);
    repo->path = copy;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_const_char_ptr_result yetty_ygit_repo_path(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ygit_repo_path: NULL object");
    }
    struct yetty_ygit_repo_ptr_result repo_res = yetty_ygit_repo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, repo_res, "yetty_ygit_repo_path: from_obj");
    return YETTY_OK(yetty_ycore_const_char_ptr, repo_res.value->path);
}

/* Small helper: fetch the (already-open) repo path, erroring if unset. */
static struct yetty_ycore_const_char_ptr_result repo_require_path(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "ygit repo: NULL object");
    }
    struct yetty_ygit_repo_ptr_result repo_res = yetty_ygit_repo_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, repo_res, "ygit repo: from_obj");
    if (!repo_res.value->path) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "ygit repo: open() not called yet");
    }
    return YETTY_OK(yetty_ycore_const_char_ptr, repo_res.value->path);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygit_repo_is_repo(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_const_char_ptr_result path_res = repo_require_path(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, path_res, "yetty_ygit_repo_is_repo");
    return yetty_ygit_backend_is_repo(path_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ygit_log_ptr_result yetty_ygit_repo_log(struct yetty_yclass_object *obj,
                                                     const char *revision, int max_count)
{
    struct yetty_ycore_const_char_ptr_result path_res = repo_require_path(obj);
    YETTY_RETURN_IF_ERR(yetty_ygit_log_ptr, path_res, "yetty_ygit_repo_log");
    return yetty_ygit_backend_log(path_res.value, revision, max_count);
}

/* Like log, but from every branch tip — the whole DAG, unmerged lanes and all. */
YETTY_ANNOTATE("expose")
struct yetty_ygit_log_ptr_result yetty_ygit_repo_log_all(struct yetty_yclass_object *obj,
                                                         int max_count)
{
    struct yetty_ycore_const_char_ptr_result path_res = repo_require_path(obj);
    YETTY_RETURN_IF_ERR(yetty_ygit_log_ptr, path_res, "yetty_ygit_repo_log_all");
    return yetty_ygit_backend_log_all(path_res.value, max_count);
}

YETTY_ANNOTATE("expose")
struct yetty_ygit_status_ptr_result yetty_ygit_repo_status(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_const_char_ptr_result path_res = repo_require_path(obj);
    YETTY_RETURN_IF_ERR(yetty_ygit_status_ptr, path_res, "yetty_ygit_repo_status");
    return yetty_ygit_backend_status(path_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ygit_branches_ptr_result yetty_ygit_repo_branches(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_const_char_ptr_result path_res = repo_require_path(obj);
    YETTY_RETURN_IF_ERR(yetty_ygit_branches_ptr, path_res, "yetty_ygit_repo_branches");
    return yetty_ygit_backend_branches(path_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ygit_commit_detail_ptr_result yetty_ygit_repo_show(struct yetty_yclass_object *obj,
                                                                const char *revision)
{
    struct yetty_ycore_const_char_ptr_result path_res = repo_require_path(obj);
    YETTY_RETURN_IF_ERR(yetty_ygit_commit_detail_ptr, path_res, "yetty_ygit_repo_show");
    return yetty_ygit_backend_show(path_res.value, revision);
}

/* Read a file blob at a revision — `spec` is "<rev>:<path>" (or a bare path,
 * defaulting to HEAD). The reusable hook a rich renderer uses to show a file as
 * it existed at any point in history. */
YETTY_ANNOTATE("expose")
struct yetty_ygit_blob_ptr_result yetty_ygit_repo_read_blob(struct yetty_yclass_object *obj,
                                                            const char *spec)
{
    struct yetty_ycore_const_char_ptr_result path_res = repo_require_path(obj);
    YETTY_RETURN_IF_ERR(yetty_ygit_blob_ptr, path_res, "yetty_ygit_repo_read_blob");
    if (!spec) {
        return YETTY_ERR(yetty_ygit_blob_ptr, "yetty_ygit_repo_read_blob: NULL spec");
    }
    return yetty_ygit_backend_read_blob(path_res.value, spec);
}

/* Diff a commit (revision, NULL → HEAD) against its first parent. Each changed
 * file carries both blob images and, for text, the structured hunks — enough
 * for a renderer to draw a visual before/after or a syntax-highlighted patch. */
YETTY_ANNOTATE("expose")
struct yetty_ygit_diff_ptr_result yetty_ygit_repo_diff(struct yetty_yclass_object *obj,
                                                       const char *revision)
{
    struct yetty_ycore_const_char_ptr_result path_res = repo_require_path(obj);
    YETTY_RETURN_IF_ERR(yetty_ygit_diff_ptr, path_res, "yetty_ygit_repo_diff");
    return yetty_ygit_backend_diff(path_res.value, revision);
}

/* Run the destructor slot, then release the object storage. Mirrors the
 * create/destroy symmetry other yclass modules expose. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygit_repo_destroy(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result destructor_res = yetty_ygit_destructor(obj);
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    if (YETTY_IS_ERR(destructor_res)) {
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_ycore_void, "yetty_ygit_repo_destroy: destructor", destructor_res);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, free_res, "yetty_ygit_repo_destroy: object free");
    return YETTY_OK_VOID();
}

#include "repo.gen.c"
