/* GENERATED — do not edit. */
/* Object API for regular class(es) `repo` (implementation module: ygit).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGIT_REPO_H
#define YETTY_YCLASSGEN_API_YGIT_REPO_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygit/git-backend.h>

#ifdef __cplusplus
extern "C" {
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygit_repo;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGIT_REPO_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGIT_REPO_PTR_RESULT
struct yetty_ygit_repo_ptr_result {
    int ok;
    union {
        struct yetty_ygit_repo *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygit_repo_ptr_result yetty_ygit_repo_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygit_repo_to(struct yetty_ygit_repo *data);

struct yetty_ycore_void_result yetty_ygit_destructor(struct yetty_yclass_object * obj);

struct yetty_yclass_object_ptr_result yetty_ygit_repo_create(struct yetty_yclass_ctx *ctx);



/* Point the repository at `path`. Does not open it eagerly — each query opens
 * and closes the repository so a long-lived handle never pins an fd. */
struct yetty_ycore_void_result yetty_ygit_repo_open(struct yetty_yclass_object *obj, const char *path);
struct yetty_ycore_const_char_ptr_result yetty_ygit_repo_path(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygit_repo_is_repo(struct yetty_yclass_object *obj);
struct yetty_ygit_log_ptr_result yetty_ygit_repo_log(struct yetty_yclass_object *obj, const char *revision, int max_count);
/* Like log, but from every branch tip — the whole DAG, unmerged lanes and all. */
struct yetty_ygit_log_ptr_result yetty_ygit_repo_log_all(struct yetty_yclass_object *obj, int max_count);
struct yetty_ygit_status_ptr_result yetty_ygit_repo_status(struct yetty_yclass_object *obj);
struct yetty_ygit_branches_ptr_result yetty_ygit_repo_branches(struct yetty_yclass_object *obj);
struct yetty_ygit_commit_detail_ptr_result yetty_ygit_repo_show(struct yetty_yclass_object *obj, const char *revision);
/* Read a file blob at a revision — `spec` is "<rev>:<path>" (or a bare path,
 * defaulting to HEAD). The reusable hook a rich renderer uses to show a file as
 * it existed at any point in history. */
struct yetty_ygit_blob_ptr_result yetty_ygit_repo_read_blob(struct yetty_yclass_object *obj, const char *spec);
/* Diff a commit (revision, NULL → HEAD) against its first parent. Each changed
 * file carries both blob images and, for text, the structured hunks — enough
 * for a renderer to draw a visual before/after or a syntax-highlighted patch. */
struct yetty_ygit_diff_ptr_result yetty_ygit_repo_diff(struct yetty_yclass_object *obj, const char *revision);
/* Run the destructor slot, then release the object storage. Mirrors the
 * create/destroy symmetry other yclass modules expose. */
struct yetty_ycore_void_result yetty_ygit_repo_destroy(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
