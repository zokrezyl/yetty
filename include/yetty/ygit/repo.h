/* GENERATED — do not edit. */
/* Public interface for regular class(es) `repo` (module: ygit).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGIT_REPO_H
#define YETTY_YCLASSGEN_YGIT_REPO_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygit/git-backend.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygit_repo_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygit_repo;
struct yetty_ygit_repo_ptr_result {
    int ok;
    union {
        struct yetty_ygit_repo *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ygit_repo_ptr_result yetty_ygit_repo_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygit_repo_to(struct yetty_ygit_repo *data);

struct yetty_ycore_void_result yetty_ygit_constructor(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ygit_destructor(struct yetty_yclass_object * obj);

typedef struct yetty_ycore_void_result (*yetty_ygit_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygit_destructor_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ygit_repo_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygit_register(void);

/* Point the repository at `path`. Does not open it eagerly — each query opens
 * and closes the repository so a long-lived handle never pins an fd. */
struct yetty_ycore_void_result yetty_ygit_repo_open(struct yetty_yclass_object *obj, const char *path);
struct yetty_ycore_const_char_ptr_result yetty_ygit_repo_path(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygit_repo_is_repo(struct yetty_yclass_object *obj);
struct yetty_ygit_log_ptr_result yetty_ygit_repo_log(struct yetty_yclass_object *obj, const char *revision, int max_count);
struct yetty_ygit_status_ptr_result yetty_ygit_repo_status(struct yetty_yclass_object *obj);
struct yetty_ygit_branches_ptr_result yetty_ygit_repo_branches(struct yetty_yclass_object *obj);
struct yetty_ygit_commit_detail_ptr_result yetty_ygit_repo_show(struct yetty_yclass_object *obj, const char *revision);
/* Run the destructor slot, then release the object storage. Mirrors the
 * create/destroy symmetry other yclass modules expose. */
struct yetty_ycore_void_result yetty_ygit_repo_destroy(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
