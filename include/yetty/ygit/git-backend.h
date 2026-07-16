/* git-backend.h — read-only Git data access for the ygit tool.
 *
 * Backed by libgit2 (vendored under build-tools/3rdparty/libgit2, linked via
 * build-tools/yetty/libs/libgit2.cmake). This layer opens a repository on the
 * local filesystem and turns commit walks, status, branch, and diff queries
 * into small owned structs — no network transports, no working-tree mutation.
 *
 * This header is hand-written (a plain-C leaf helper with no object surface —
 * the sanctioned exception to the yclass-first rule). It deliberately does NOT
 * expose any libgit2 type; <git2.h> stays confined to git-backend.c. The
 * yclass `repo` class in repo.c returns these structs as its Result types and
 * pulls this file into its generated public header via an `include@`
 * annotation, so the data and Result types are declared exactly once here.
 */

#ifndef YETTY_YGIT_GIT_BACKEND_H
#define YETTY_YGIT_GIT_BACKEND_H

#include <yetty/ycore/result.h>

#include <stddef.h>
#include <stdint.h>

/* One commit as reported by a revision walk. All heap strings are owned by the
 * enclosing struct and released by its destroy function. */
struct yetty_ygit_commit {
    char full_hash[41];   /* 40 hex digits + NUL */
    char abbrev_hash[16]; /* abbreviated hash + NUL */
    char *author_name;    /* never NULL after a successful parse (may be "") */
    char *author_email;
    char *subject;        /* first line of the commit message */
    int64_t author_time;  /* author date, seconds since the Unix epoch */
    char **parent_hashes; /* full hashes of each parent, in parent order */
    size_t parent_count;
    char **ref_names; /* decoration: branch / tag / HEAD labels, if any */
    size_t ref_count;
    int lane; /* graph column, filled by commit-graph.c (-1 until then) */
};

struct yetty_ygit_log {
    struct yetty_ygit_commit *commits;
    size_t count;
};

/* One entry from the working-tree/index status. index_status and
 * worktree_status are porcelain-style single-column codes (e.g. 'M', 'A',
 * 'D', 'R', '?', or ' '). */
struct yetty_ygit_status_entry {
    char index_status;
    char worktree_status;
    char *path;
};

struct yetty_ygit_status {
    char *branch;   /* current branch, or a detached-HEAD description */
    char *upstream; /* upstream tracking ref short name, or NULL */
    int ahead;      /* commits ahead of upstream */
    int behind;     /* commits behind upstream */
    struct yetty_ygit_status_entry *entries;
    size_t entry_count;
};

struct yetty_ygit_branch {
    char *name;
    char *tip_abbrev_hash;
    char *subject; /* subject of the branch-tip commit */
    int is_head;   /* 1 for the currently checked-out branch */
};

struct yetty_ygit_branches {
    struct yetty_ygit_branch *branches;
    size_t count;
};

/* One changed file in a commit's diff against its first parent. added /
 * deleted are -1 for binary files. */
struct yetty_ygit_file_change {
    char *path;
    int added;
    int deleted;
};

/* A single commit inspected in full: its metadata, its message body, and the
 * files it touched relative to its first parent. */
struct yetty_ygit_commit_detail {
    struct yetty_ygit_commit commit;
    char *body; /* commit message body below the subject, or NULL */
    struct yetty_ygit_file_change *files;
    size_t file_count;
};

YETTY_YRESULT_DECLARE(yetty_ygit_log_ptr, struct yetty_ygit_log *);
YETTY_YRESULT_DECLARE(yetty_ygit_status_ptr, struct yetty_ygit_status *);
YETTY_YRESULT_DECLARE(yetty_ygit_branches_ptr, struct yetty_ygit_branches *);
YETTY_YRESULT_DECLARE(yetty_ygit_commit_detail_ptr, struct yetty_ygit_commit_detail *);

/* True when repo_path is inside a Git working tree / repository. */
struct yetty_ycore_int_result yetty_ygit_backend_is_repo(const char *repo_path);

/* Walk history from revision (NULL → HEAD), newest first, at most max_count
 * commits (<= 0 → no limit). Topological + date ordering. */
struct yetty_ygit_log_ptr_result yetty_ygit_backend_log(const char *repo_path, const char *revision,
                                                        int max_count);

struct yetty_ygit_status_ptr_result yetty_ygit_backend_status(const char *repo_path);

struct yetty_ygit_branches_ptr_result yetty_ygit_backend_branches(const char *repo_path);

struct yetty_ygit_commit_detail_ptr_result yetty_ygit_backend_show(const char *repo_path,
                                                                   const char *revision);

void yetty_ygit_commit_release(struct yetty_ygit_commit *commit);
void yetty_ygit_log_destroy(struct yetty_ygit_log *log);
void yetty_ygit_status_destroy(struct yetty_ygit_status *status);
void yetty_ygit_branches_destroy(struct yetty_ygit_branches *branches);
void yetty_ygit_commit_detail_destroy(struct yetty_ygit_commit_detail *detail);

#endif /* YETTY_YGIT_GIT_BACKEND_H */
