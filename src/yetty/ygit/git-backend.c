/* git-backend.c — read-only Git data access backed by libgit2.
 *
 * Plain-C leaf helper (no object surface): opens a repository on the local
 * filesystem and turns commit walks, status, branch, and diff queries into the
 * owned structs declared in git-backend.h. <git2.h> is confined to this file.
 *
 * libgit2 error strings live in a thread-local buffer that the next call
 * overwrites, and struct yetty_ycore_error stores its .msg by pointer (not a
 * copy) — so error messages here are stable string literals naming the failing
 * operation rather than the transient libgit2 detail.
 *
 * Each public entry point brackets its work with git_libgit2_init /
 * git_libgit2_shutdown (both refcounted), so the backend needs no
 * program-lifetime state and is safe to call in any order.
 */

#include <yetty/ygit/git-backend.h>

#include <git2.h>

#include <stdlib.h>
#include <string.h>

/* strdup that never returns NULL for a NULL input — optional text fields
 * (author email, subject) become "" rather than NULL. Returns NULL only on a
 * genuine allocation failure. */
static char *ygit_strdup(const char *text)
{
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static void ygit_string_array_free(char **items, size_t count)
{
    if (!items) {
        return;
    }
    for (size_t index = 0; index < count; index++) {
        free(items[index]);
    }
    free(items);
}

/* --- commit struct helpers ---------------------------------------------- */

void yetty_ygit_commit_release(struct yetty_ygit_commit *commit)
{
    if (!commit) {
        return;
    }
    free(commit->author_name);
    free(commit->author_email);
    free(commit->subject);
    ygit_string_array_free(commit->parent_hashes, commit->parent_count);
    ygit_string_array_free(commit->ref_names, commit->ref_count);
    memset(commit, 0, sizeof(*commit));
}

static void ygit_commit_fill_ids(struct yetty_ygit_commit *commit, const git_oid *oid)
{
    git_oid_tostr(commit->full_hash, sizeof(commit->full_hash), oid);
    git_oid_tostr(commit->abbrev_hash, 11, oid); /* 10 hex chars + NUL */
}

/* Populate metadata + parent hashes from a libgit2 commit. Does NOT set
 * ref_names (decoration is attached separately from a ref map). Returns -1 on
 * allocation failure, leaving *out safe to release. */
/* Takes a non-const commit: git_commit_summary lazily computes and caches the
 * summary, so libgit2 declares it (and _body) as taking git_commit *. */
static int ygit_commit_fill(struct yetty_ygit_commit *out, git_commit *commit)
{
    out->lane = -1;
    ygit_commit_fill_ids(out, git_commit_id(commit));

    const git_signature *author = git_commit_author(commit);
    out->author_name = ygit_strdup(author ? author->name : "");
    out->author_email = ygit_strdup(author ? author->email : "");
    out->subject = ygit_strdup(git_commit_summary(commit));
    out->author_time = author ? (int64_t)author->when.time : 0;
    if (!out->author_name || !out->author_email || !out->subject) {
        return -1;
    }

    unsigned int parent_count = git_commit_parentcount(commit);
    if (parent_count > 0) {
        out->parent_hashes = calloc(parent_count, sizeof(char *));
        if (!out->parent_hashes) {
            return -1;
        }
        for (unsigned int index = 0; index < parent_count; index++) {
            char parent_hash[41];
            git_oid_tostr(parent_hash, sizeof(parent_hash), git_commit_parent_id(commit, index));
            out->parent_hashes[index] = ygit_strdup(parent_hash);
            out->parent_count = index + 1;
            if (!out->parent_hashes[index]) {
                return -1;
            }
        }
    }
    return 0;
}

/* --- ref decoration map ------------------------------------------------- */

struct ygit_ref_entry {
    git_oid target;   /* commit oid the ref resolves to */
    char *short_name; /* "main", "v1.0", "origin/main", … */
};

struct ygit_ref_map {
    struct ygit_ref_entry *entries;
    size_t count;
};

static void ygit_ref_map_release(struct ygit_ref_map *map)
{
    if (!map) {
        return;
    }
    for (size_t index = 0; index < map->count; index++) {
        free(map->entries[index].short_name);
    }
    free(map->entries);
    map->entries = NULL;
    map->count = 0;
}

/* Build a (commit-oid → short ref name) table from every branch and tag, so
 * each walked commit can be decorated. Peels annotated tags to their commit.
 * Best-effort: refs that fail to peel are skipped, not fatal. */
static int ygit_ref_map_build(git_repository *repo, struct ygit_ref_map *map)
{
    map->entries = NULL;
    map->count = 0;

    git_reference_iterator *iterator = NULL;
    if (git_reference_iterator_new(&iterator, repo) < 0) {
        return -1;
    }

    size_t capacity = 0;
    git_reference *reference = NULL;
    int status = 0;
    while ((status = git_reference_next(&reference, iterator)) == 0) {
        const char *short_name = git_reference_shorthand(reference);
        git_object *peeled = NULL;
        if (short_name && strcmp(short_name, "HEAD") != 0 &&
            git_reference_peel(&peeled, reference, GIT_OBJECT_COMMIT) == 0) {
            if (map->count == capacity) {
                size_t new_capacity = capacity ? capacity * 2 : 16;
                struct ygit_ref_entry *grown = realloc(map->entries, new_capacity * sizeof(*grown));
                if (!grown) {
                    git_object_free(peeled);
                    git_reference_free(reference);
                    git_reference_iterator_free(iterator);
                    return -1;
                }
                map->entries = grown;
                capacity = new_capacity;
            }
            map->entries[map->count].target = *git_object_id(peeled);
            map->entries[map->count].short_name = ygit_strdup(short_name);
            if (!map->entries[map->count].short_name) {
                git_object_free(peeled);
                git_reference_free(reference);
                git_reference_iterator_free(iterator);
                return -1;
            }
            map->count++;
        }
        git_object_free(peeled);
        git_reference_free(reference);
    }
    git_reference_iterator_free(iterator);
    return 0;
}

/* Attach any ref names pointing at commit->full_hash. Returns -1 on OOM. */
static int ygit_commit_attach_refs(struct yetty_ygit_commit *commit, const git_oid *oid,
                                   const struct ygit_ref_map *map)
{
    size_t matches = 0;
    for (size_t index = 0; index < map->count; index++) {
        if (git_oid_equal(&map->entries[index].target, oid)) {
            matches++;
        }
    }
    if (matches == 0) {
        return 0;
    }
    commit->ref_names = calloc(matches, sizeof(char *));
    if (!commit->ref_names) {
        return -1;
    }
    for (size_t index = 0; index < map->count; index++) {
        if (git_oid_equal(&map->entries[index].target, oid)) {
            commit->ref_names[commit->ref_count] = ygit_strdup(map->entries[index].short_name);
            if (!commit->ref_names[commit->ref_count]) {
                return -1;
            }
            commit->ref_count++;
        }
    }
    return 0;
}

/* --- destroy functions -------------------------------------------------- */

void yetty_ygit_log_destroy(struct yetty_ygit_log *log)
{
    if (!log) {
        return;
    }
    for (size_t index = 0; index < log->count; index++) {
        yetty_ygit_commit_release(&log->commits[index]);
    }
    free(log->commits);
    free(log);
}

void yetty_ygit_status_destroy(struct yetty_ygit_status *status)
{
    if (!status) {
        return;
    }
    free(status->branch);
    free(status->upstream);
    for (size_t index = 0; index < status->entry_count; index++) {
        free(status->entries[index].path);
    }
    free(status->entries);
    free(status);
}

void yetty_ygit_branches_destroy(struct yetty_ygit_branches *branches)
{
    if (!branches) {
        return;
    }
    for (size_t index = 0; index < branches->count; index++) {
        free(branches->branches[index].name);
        free(branches->branches[index].tip_abbrev_hash);
        free(branches->branches[index].subject);
    }
    free(branches->branches);
    free(branches);
}

void yetty_ygit_commit_detail_destroy(struct yetty_ygit_commit_detail *detail)
{
    if (!detail) {
        return;
    }
    yetty_ygit_commit_release(&detail->commit);
    free(detail->body);
    for (size_t index = 0; index < detail->file_count; index++) {
        free(detail->files[index].path);
    }
    free(detail->files);
    free(detail);
}

void yetty_ygit_diff_destroy(struct yetty_ygit_diff *diff)
{
    if (!diff) {
        return;
    }
    yetty_ygit_commit_release(&diff->commit);
    for (size_t file_index = 0; file_index < diff->file_count; file_index++) {
        struct yetty_ygit_diff_file *file = &diff->files[file_index];
        free(file->old_path);
        free(file->new_path);
        free(file->old_data);
        free(file->new_data);
        for (size_t hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
            struct yetty_ygit_diff_hunk *hunk = &file->hunks[hunk_index];
            free(hunk->header);
            for (size_t line_index = 0; line_index < hunk->line_count; line_index++) {
                free(hunk->lines[line_index].content);
            }
            free(hunk->lines);
        }
        free(file->hunks);
    }
    free(diff->files);
    free(diff);
}

/* --- is_repo ------------------------------------------------------------ */

static struct yetty_ycore_int_result is_repo_impl(const char *repo_path)
{
    git_repository *repo = NULL;
    int open_status = git_repository_open_ext(&repo, repo_path, 0, NULL);
    if (open_status == 0) {
        git_repository_free(repo);
        return YETTY_OK(yetty_ycore_int, 1);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

struct yetty_ycore_int_result yetty_ygit_backend_is_repo(const char *repo_path)
{
    if (!repo_path) {
        return YETTY_ERR(yetty_ycore_int, "ygit is_repo: NULL path");
    }
    if (git_libgit2_init() < 0) {
        return YETTY_ERR(yetty_ycore_int, "ygit: libgit2 init failed");
    }
    struct yetty_ycore_int_result result = is_repo_impl(repo_path);
    git_libgit2_shutdown();
    return result;
}

/* --- log ---------------------------------------------------------------- */

static struct yetty_ygit_log_ptr_result log_impl(const char *repo_path, const char *revision,
                                                 int max_count, int all_refs)
{
    struct yetty_ygit_log_ptr_result result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: unreached");
    git_repository *repo = NULL;
    git_revwalk *walk = NULL;
    struct ygit_ref_map ref_map = {0};
    struct yetty_ygit_log *log = NULL;

    if (git_repository_open_ext(&repo, repo_path, 0, NULL) < 0) {
        result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: cannot open repository");
        goto cleanup;
    }
    if (git_revwalk_new(&walk, repo) < 0) {
        result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: revwalk alloc failed");
        goto cleanup;
    }
    git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);

    if (all_refs) {
        /* Every branch tip, so the walk covers the whole DAG (including lanes
         * that never merged). No branches (unborn HEAD) is an empty log. */
        if (git_revwalk_push_glob(walk, "refs/heads/*") < 0) {
            log = calloc(1, sizeof(*log));
            if (!log) {
                result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: out of memory");
                goto cleanup;
            }
            result = YETTY_OK(yetty_ygit_log_ptr, log);
            log = NULL;
            goto cleanup;
        }
    } else if (revision && revision[0]) {
        git_object *object = NULL;
        git_object *commit_object = NULL;
        if (git_revparse_single(&object, repo, revision) < 0) {
            result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: cannot resolve revision");
            goto cleanup;
        }
        int peel_status = git_object_peel(&commit_object, object, GIT_OBJECT_COMMIT);
        git_object_free(object);
        if (peel_status < 0) {
            result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: revision is not a commit");
            goto cleanup;
        }
        int push_status = git_revwalk_push(walk, git_object_id(commit_object));
        git_object_free(commit_object);
        if (push_status < 0) {
            result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: revwalk push failed");
            goto cleanup;
        }
    } else if (git_revwalk_push_head(walk) < 0) {
        /* An empty repository (unborn HEAD) is an empty log, not an error. */
        log = calloc(1, sizeof(*log));
        if (!log) {
            result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: out of memory");
            goto cleanup;
        }
        result = YETTY_OK(yetty_ygit_log_ptr, log);
        log = NULL;
        goto cleanup;
    }

    if (ygit_ref_map_build(repo, &ref_map) < 0) {
        result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: ref decoration scan failed");
        goto cleanup;
    }

    log = calloc(1, sizeof(*log));
    if (!log) {
        result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: out of memory");
        goto cleanup;
    }

    size_t capacity = 0;
    git_oid oid;
    int walk_status = 0;
    while ((max_count <= 0 || (int)log->count < max_count) &&
           (walk_status = git_revwalk_next(&oid, walk)) == 0) {
        git_commit *commit = NULL;
        if (git_commit_lookup(&commit, repo, &oid) < 0) {
            result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: commit lookup failed");
            goto cleanup;
        }
        if (log->count == capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 64;
            struct yetty_ygit_commit *grown = realloc(log->commits, new_capacity * sizeof(*grown));
            if (!grown) {
                git_commit_free(commit);
                result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: out of memory");
                goto cleanup;
            }
            log->commits = grown;
            capacity = new_capacity;
        }
        struct yetty_ygit_commit *slot = &log->commits[log->count];
        memset(slot, 0, sizeof(*slot));
        int fill_status = ygit_commit_fill(slot, commit);
        git_commit_free(commit);
        if (fill_status < 0) {
            yetty_ygit_commit_release(slot);
            result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: out of memory");
            goto cleanup;
        }
        if (ygit_commit_attach_refs(slot, &oid, &ref_map) < 0) {
            yetty_ygit_commit_release(slot);
            result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: out of memory");
            goto cleanup;
        }
        log->count++;
    }
    if (walk_status < 0 && walk_status != GIT_ITEROVER) {
        result = YETTY_ERR(yetty_ygit_log_ptr, "ygit log: revwalk iteration failed");
        goto cleanup;
    }

    result = YETTY_OK(yetty_ygit_log_ptr, log);
    log = NULL;

cleanup:
    yetty_ygit_log_destroy(log);
    ygit_ref_map_release(&ref_map);
    git_revwalk_free(walk);
    git_repository_free(repo);
    return result;
}

struct yetty_ygit_log_ptr_result yetty_ygit_backend_log(const char *repo_path, const char *revision,
                                                        int max_count)
{
    if (!repo_path) {
        return YETTY_ERR(yetty_ygit_log_ptr, "ygit log: NULL path");
    }
    if (git_libgit2_init() < 0) {
        return YETTY_ERR(yetty_ygit_log_ptr, "ygit: libgit2 init failed");
    }
    struct yetty_ygit_log_ptr_result result = log_impl(repo_path, revision, max_count, 0);
    git_libgit2_shutdown();
    return result;
}

struct yetty_ygit_log_ptr_result yetty_ygit_backend_log_all(const char *repo_path, int max_count)
{
    if (!repo_path) {
        return YETTY_ERR(yetty_ygit_log_ptr, "ygit log: NULL path");
    }
    if (git_libgit2_init() < 0) {
        return YETTY_ERR(yetty_ygit_log_ptr, "ygit: libgit2 init failed");
    }
    struct yetty_ygit_log_ptr_result result = log_impl(repo_path, NULL, max_count, 1);
    git_libgit2_shutdown();
    return result;
}

/* --- status ------------------------------------------------------------- */

static char ygit_index_status_char(git_status_t status)
{
    if (status & GIT_STATUS_INDEX_NEW) {
        return 'A';
    }
    if (status & GIT_STATUS_INDEX_MODIFIED) {
        return 'M';
    }
    if (status & GIT_STATUS_INDEX_DELETED) {
        return 'D';
    }
    if (status & GIT_STATUS_INDEX_RENAMED) {
        return 'R';
    }
    if (status & GIT_STATUS_INDEX_TYPECHANGE) {
        return 'T';
    }
    return ' ';
}

static char ygit_worktree_status_char(git_status_t status)
{
    if (status & GIT_STATUS_WT_NEW) {
        return '?';
    }
    if (status & GIT_STATUS_WT_MODIFIED) {
        return 'M';
    }
    if (status & GIT_STATUS_WT_DELETED) {
        return 'D';
    }
    if (status & GIT_STATUS_WT_RENAMED) {
        return 'R';
    }
    if (status & GIT_STATUS_WT_TYPECHANGE) {
        return 'T';
    }
    return ' ';
}

static const char *ygit_status_entry_path(const git_status_entry *entry)
{
    if (entry->index_to_workdir && entry->index_to_workdir->new_file.path) {
        return entry->index_to_workdir->new_file.path;
    }
    if (entry->head_to_index && entry->head_to_index->new_file.path) {
        return entry->head_to_index->new_file.path;
    }
    return "";
}

/* Fill branch name, upstream, and ahead/behind. Best-effort: a detached or
 * unborn HEAD leaves branch as a description and no upstream. */
static int ygit_status_fill_branch(git_repository *repo, struct yetty_ygit_status *status)
{
    if (git_repository_head_detached(repo) == 1) {
        status->branch = ygit_strdup("HEAD (detached)");
        return status->branch ? 0 : -1;
    }
    git_reference *head = NULL;
    int head_status = git_repository_head(&head, repo);
    if (head_status < 0) {
        status->branch = ygit_strdup(head_status == GIT_EUNBORNBRANCH ? "(unborn)" : "(unknown)");
        return status->branch ? 0 : -1;
    }
    status->branch = ygit_strdup(git_reference_shorthand(head));
    if (!status->branch) {
        git_reference_free(head);
        return -1;
    }

    git_reference *upstream = NULL;
    if (git_branch_upstream(&upstream, head) == 0) {
        status->upstream = ygit_strdup(git_reference_shorthand(upstream));
        const git_oid *local_oid = git_reference_target(head);
        const git_oid *upstream_oid = git_reference_target(upstream);
        if (local_oid && upstream_oid) {
            size_t ahead = 0;
            size_t behind = 0;
            if (git_graph_ahead_behind(&ahead, &behind, repo, local_oid, upstream_oid) == 0) {
                status->ahead = (int)ahead;
                status->behind = (int)behind;
            }
        }
        git_reference_free(upstream);
        if (!status->upstream) {
            git_reference_free(head);
            return -1;
        }
    }
    git_reference_free(head);
    return 0;
}

static struct yetty_ygit_status_ptr_result status_impl(const char *repo_path)
{
    struct yetty_ygit_status_ptr_result result =
        YETTY_ERR(yetty_ygit_status_ptr, "ygit status: unreached");
    git_repository *repo = NULL;
    git_status_list *status_list = NULL;
    struct yetty_ygit_status *status = NULL;

    if (git_repository_open_ext(&repo, repo_path, 0, NULL) < 0) {
        result = YETTY_ERR(yetty_ygit_status_ptr, "ygit status: cannot open repository");
        goto cleanup;
    }

    git_status_options options;
    git_status_options_init(&options, GIT_STATUS_OPTIONS_VERSION);
    options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX |
                    GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;
    if (git_status_list_new(&status_list, repo, &options) < 0) {
        result = YETTY_ERR(yetty_ygit_status_ptr, "ygit status: status scan failed");
        goto cleanup;
    }

    status = calloc(1, sizeof(*status));
    if (!status) {
        result = YETTY_ERR(yetty_ygit_status_ptr, "ygit status: out of memory");
        goto cleanup;
    }
    if (ygit_status_fill_branch(repo, status) < 0) {
        result = YETTY_ERR(yetty_ygit_status_ptr, "ygit status: out of memory");
        goto cleanup;
    }

    size_t entry_count = git_status_list_entrycount(status_list);
    if (entry_count > 0) {
        status->entries = calloc(entry_count, sizeof(*status->entries));
        if (!status->entries) {
            result = YETTY_ERR(yetty_ygit_status_ptr, "ygit status: out of memory");
            goto cleanup;
        }
        for (size_t index = 0; index < entry_count; index++) {
            const git_status_entry *entry = git_status_byindex(status_list, index);
            if (!entry || entry->status == GIT_STATUS_CURRENT) {
                continue;
            }
            struct yetty_ygit_status_entry *out = &status->entries[status->entry_count];
            out->index_status = ygit_index_status_char(entry->status);
            out->worktree_status = ygit_worktree_status_char(entry->status);
            out->path = ygit_strdup(ygit_status_entry_path(entry));
            if (!out->path) {
                result = YETTY_ERR(yetty_ygit_status_ptr, "ygit status: out of memory");
                goto cleanup;
            }
            status->entry_count++;
        }
    }

    result = YETTY_OK(yetty_ygit_status_ptr, status);
    status = NULL;

cleanup:
    yetty_ygit_status_destroy(status);
    git_status_list_free(status_list);
    git_repository_free(repo);
    return result;
}

struct yetty_ygit_status_ptr_result yetty_ygit_backend_status(const char *repo_path)
{
    if (!repo_path) {
        return YETTY_ERR(yetty_ygit_status_ptr, "ygit status: NULL path");
    }
    if (git_libgit2_init() < 0) {
        return YETTY_ERR(yetty_ygit_status_ptr, "ygit: libgit2 init failed");
    }
    struct yetty_ygit_status_ptr_result result = status_impl(repo_path);
    git_libgit2_shutdown();
    return result;
}

/* --- branches ----------------------------------------------------------- */

static struct yetty_ygit_branches_ptr_result branches_impl(const char *repo_path)
{
    struct yetty_ygit_branches_ptr_result result =
        YETTY_ERR(yetty_ygit_branches_ptr, "ygit branches: unreached");
    git_repository *repo = NULL;
    git_branch_iterator *iterator = NULL;
    struct yetty_ygit_branches *branches = NULL;

    if (git_repository_open_ext(&repo, repo_path, 0, NULL) < 0) {
        result = YETTY_ERR(yetty_ygit_branches_ptr, "ygit branches: cannot open repository");
        goto cleanup;
    }
    if (git_branch_iterator_new(&iterator, repo, GIT_BRANCH_LOCAL) < 0) {
        result = YETTY_ERR(yetty_ygit_branches_ptr, "ygit branches: iterator alloc failed");
        goto cleanup;
    }

    branches = calloc(1, sizeof(*branches));
    if (!branches) {
        result = YETTY_ERR(yetty_ygit_branches_ptr, "ygit branches: out of memory");
        goto cleanup;
    }

    size_t capacity = 0;
    git_reference *reference = NULL;
    git_branch_t branch_type;
    int next_status = 0;
    while ((next_status = git_branch_next(&reference, &branch_type, iterator)) == 0) {
        if (branches->count == capacity) {
            size_t new_capacity = capacity ? capacity * 2 : 16;
            struct yetty_ygit_branch *grown =
                realloc(branches->branches, new_capacity * sizeof(*grown));
            if (!grown) {
                git_reference_free(reference);
                result = YETTY_ERR(yetty_ygit_branches_ptr, "ygit branches: out of memory");
                goto cleanup;
            }
            branches->branches = grown;
            capacity = new_capacity;
        }
        struct yetty_ygit_branch *slot = &branches->branches[branches->count];
        memset(slot, 0, sizeof(*slot));

        const char *name = NULL;
        git_branch_name(&name, reference);
        slot->name = ygit_strdup(name);
        slot->is_head = git_branch_is_head(reference) == 1 ? 1 : 0;

        const git_oid *target = git_reference_target(reference);
        if (target) {
            char abbrev[16];
            git_oid_tostr(abbrev, 11, target);
            slot->tip_abbrev_hash = ygit_strdup(abbrev);
            git_commit *commit = NULL;
            if (git_commit_lookup(&commit, repo, target) == 0) {
                slot->subject = ygit_strdup(git_commit_summary(commit));
                git_commit_free(commit);
            } else {
                slot->subject = ygit_strdup("");
            }
        } else {
            slot->tip_abbrev_hash = ygit_strdup("");
            slot->subject = ygit_strdup("");
        }
        git_reference_free(reference);

        if (!slot->name || !slot->tip_abbrev_hash || !slot->subject) {
            free(slot->name);
            free(slot->tip_abbrev_hash);
            free(slot->subject);
            result = YETTY_ERR(yetty_ygit_branches_ptr, "ygit branches: out of memory");
            goto cleanup;
        }
        branches->count++;
    }

    result = YETTY_OK(yetty_ygit_branches_ptr, branches);
    branches = NULL;

cleanup:
    yetty_ygit_branches_destroy(branches);
    git_branch_iterator_free(iterator);
    git_repository_free(repo);
    return result;
}

struct yetty_ygit_branches_ptr_result yetty_ygit_backend_branches(const char *repo_path)
{
    if (!repo_path) {
        return YETTY_ERR(yetty_ygit_branches_ptr, "ygit branches: NULL path");
    }
    if (git_libgit2_init() < 0) {
        return YETTY_ERR(yetty_ygit_branches_ptr, "ygit: libgit2 init failed");
    }
    struct yetty_ygit_branches_ptr_result result = branches_impl(repo_path);
    git_libgit2_shutdown();
    return result;
}

/* --- show (commit detail + file changes) -------------------------------- */

static int ygit_detail_collect_files(git_repository *repo, git_commit *commit,
                                     struct yetty_ygit_commit_detail *detail)
{
    git_tree *commit_tree = NULL;
    git_tree *parent_tree = NULL;
    git_diff *diff = NULL;
    int outcome = -1;

    if (git_commit_tree(&commit_tree, commit) < 0) {
        goto cleanup;
    }
    if (git_commit_parentcount(commit) > 0) {
        git_commit *parent = NULL;
        if (git_commit_parent(&parent, commit, 0) == 0) {
            git_commit_tree(&parent_tree, parent);
            git_commit_free(parent);
        }
    }
    if (git_diff_tree_to_tree(&diff, repo, parent_tree, commit_tree, NULL) < 0) {
        goto cleanup;
    }

    size_t delta_count = git_diff_num_deltas(diff);
    if (delta_count > 0) {
        detail->files = calloc(delta_count, sizeof(*detail->files));
        if (!detail->files) {
            goto cleanup;
        }
        for (size_t index = 0; index < delta_count; index++) {
            git_patch *patch = NULL;
            if (git_patch_from_diff(&patch, diff, index) < 0) {
                goto cleanup;
            }
            const git_diff_delta *delta = git_patch_get_delta(patch);
            struct yetty_ygit_file_change *change = &detail->files[detail->file_count];
            change->path =
                ygit_strdup(delta->new_file.path ? delta->new_file.path : delta->old_file.path);
            if (delta->flags & GIT_DIFF_FLAG_BINARY) {
                change->added = -1;
                change->deleted = -1;
            } else {
                size_t context = 0;
                size_t additions = 0;
                size_t deletions = 0;
                git_patch_line_stats(&context, &additions, &deletions, patch);
                change->added = (int)additions;
                change->deleted = (int)deletions;
            }
            git_patch_free(patch);
            if (!change->path) {
                goto cleanup;
            }
            detail->file_count++;
        }
    }
    outcome = 0;

cleanup:
    git_diff_free(diff);
    git_tree_free(parent_tree);
    git_tree_free(commit_tree);
    return outcome;
}

static struct yetty_ygit_commit_detail_ptr_result show_impl(const char *repo_path,
                                                            const char *revision)
{
    struct yetty_ygit_commit_detail_ptr_result result =
        YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: unreached");
    git_repository *repo = NULL;
    git_object *object = NULL;
    git_object *commit_object = NULL;
    struct yetty_ygit_commit_detail *detail = NULL;

    if (git_repository_open_ext(&repo, repo_path, 0, NULL) < 0) {
        result = YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: cannot open repository");
        goto cleanup;
    }
    if (git_revparse_single(&object, repo, revision) < 0) {
        result = YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: cannot resolve revision");
        goto cleanup;
    }
    if (git_object_peel(&commit_object, object, GIT_OBJECT_COMMIT) < 0) {
        result = YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: revision is not a commit");
        goto cleanup;
    }
    git_commit *commit = (git_commit *)commit_object;

    detail = calloc(1, sizeof(*detail));
    if (!detail) {
        result = YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: out of memory");
        goto cleanup;
    }
    if (ygit_commit_fill(&detail->commit, commit) < 0) {
        result = YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: out of memory");
        goto cleanup;
    }
    const char *body = git_commit_body(commit);
    if (body) {
        detail->body = ygit_strdup(body);
        if (!detail->body) {
            result = YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: out of memory");
            goto cleanup;
        }
    }
    if (ygit_detail_collect_files(repo, commit, detail) < 0) {
        result = YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: diff collection failed");
        goto cleanup;
    }

    result = YETTY_OK(yetty_ygit_commit_detail_ptr, detail);
    detail = NULL;

cleanup:
    yetty_ygit_commit_detail_destroy(detail);
    git_object_free(commit_object);
    git_object_free(object);
    git_repository_free(repo);
    return result;
}

struct yetty_ygit_commit_detail_ptr_result yetty_ygit_backend_show(const char *repo_path,
                                                                   const char *revision)
{
    if (!repo_path || !revision) {
        return YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit show: NULL argument");
    }
    if (git_libgit2_init() < 0) {
        return YETTY_ERR(yetty_ygit_commit_detail_ptr, "ygit: libgit2 init failed");
    }
    struct yetty_ygit_commit_detail_ptr_result result = show_impl(repo_path, revision);
    git_libgit2_shutdown();
    return result;
}

/* --- diff (commit vs first parent, with blob images + text hunks) ------- */

/* Duplicate `text[0..len)` with any trailing CR/LF stripped, NUL-terminated. */
static char *ygit_trim_newline_dup(const char *text, size_t len)
{
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        len--;
    }
    char *copy = malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    if (len) {
        memcpy(copy, text, len);
    }
    copy[len] = '\0';
    return copy;
}

/* Copy a blob's raw bytes by oid. A zero oid (the missing side of an add or
 * delete) yields NULL/0 and success. Returns -1 on lookup / allocation failure. */
static int ygit_copy_blob(git_repository *repo, const git_oid *oid, unsigned char **data_out,
                          size_t *size_out)
{
    *data_out = NULL;
    *size_out = 0;
    if (git_oid_is_zero(oid)) {
        return 0;
    }
    git_blob *blob = NULL;
    if (git_blob_lookup(&blob, repo, oid) < 0) {
        return -1;
    }
    const void *raw = git_blob_rawcontent(blob);
    size_t raw_size = (size_t)git_blob_rawsize(blob);
    unsigned char *copy = malloc(raw_size ? raw_size : 1);
    if (!copy) {
        git_blob_free(blob);
        return -1;
    }
    if (raw_size) {
        memcpy(copy, raw, raw_size);
    }
    git_blob_free(blob);
    *data_out = copy;
    *size_out = raw_size;
    return 0;
}

static char ygit_delta_status_char(git_delta_t status)
{
    switch (status) {
    case GIT_DELTA_ADDED:
        return 'A';
    case GIT_DELTA_DELETED:
        return 'D';
    case GIT_DELTA_RENAMED:
        return 'R';
    case GIT_DELTA_COPIED:
        return 'C';
    case GIT_DELTA_TYPECHANGE:
        return 'T';
    case GIT_DELTA_MODIFIED:
    default:
        return 'M';
    }
}

/* Pull one file's structured hunks (headers + origin-tagged lines) out of its
 * patch. Hunk/line counts are advanced as each is populated so a mid-way
 * failure leaves the file safe for yetty_ygit_diff_destroy. Returns -1 on OOM. */
static int ygit_collect_hunks(git_patch *patch, struct yetty_ygit_diff_file *file)
{
    size_t hunk_total = git_patch_num_hunks(patch);
    if (hunk_total == 0) {
        return 0;
    }
    file->hunks = calloc(hunk_total, sizeof(*file->hunks));
    if (!file->hunks) {
        return -1;
    }
    for (size_t hunk_index = 0; hunk_index < hunk_total; hunk_index++) {
        const git_diff_hunk *git_hunk = NULL;
        size_t lines_in_hunk = 0;
        if (git_patch_get_hunk(&git_hunk, &lines_in_hunk, patch, hunk_index) < 0) {
            return -1;
        }
        struct yetty_ygit_diff_hunk *hunk = &file->hunks[file->hunk_count];
        hunk->header = ygit_trim_newline_dup(git_hunk->header, git_hunk->header_len);
        if (!hunk->header) {
            return -1;
        }
        file->hunk_count++; /* header tracked → destroy will free from here on */

        if (lines_in_hunk > 0) {
            hunk->lines = calloc(lines_in_hunk, sizeof(*hunk->lines));
            if (!hunk->lines) {
                return -1;
            }
        }
        for (size_t line_index = 0; line_index < lines_in_hunk; line_index++) {
            const git_diff_line *git_line = NULL;
            if (git_patch_get_line_in_hunk(&git_line, patch, hunk_index, line_index) < 0) {
                return -1;
            }
            if (git_line->origin != GIT_DIFF_LINE_CONTEXT &&
                git_line->origin != GIT_DIFF_LINE_ADDITION &&
                git_line->origin != GIT_DIFF_LINE_DELETION) {
                continue; /* skip the "\ No newline at end of file" markers */
            }
            struct yetty_ygit_diff_line *line = &hunk->lines[hunk->line_count];
            line->origin = git_line->origin;
            line->old_lineno = git_line->old_lineno;
            line->new_lineno = git_line->new_lineno;
            line->content = ygit_trim_newline_dup(git_line->content, git_line->content_len);
            if (!line->content) {
                return -1;
            }
            hunk->line_count++;
        }
    }
    return 0;
}

static struct yetty_ygit_diff_ptr_result diff_impl(const char *repo_path, const char *revision)
{
    struct yetty_ygit_diff_ptr_result result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: unreached");
    git_repository *repo = NULL;
    git_object *object = NULL;
    git_object *commit_object = NULL;
    git_tree *commit_tree = NULL;
    git_tree *parent_tree = NULL;
    git_diff *diff = NULL;
    struct yetty_ygit_diff *out = NULL;

    if (git_repository_open_ext(&repo, repo_path, 0, NULL) < 0) {
        result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: cannot open repository");
        goto cleanup;
    }
    const char *rev = (revision && revision[0]) ? revision : "HEAD";
    if (git_revparse_single(&object, repo, rev) < 0) {
        result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: cannot resolve revision");
        goto cleanup;
    }
    if (git_object_peel(&commit_object, object, GIT_OBJECT_COMMIT) < 0) {
        result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: revision is not a commit");
        goto cleanup;
    }
    git_commit *commit = (git_commit *)commit_object;

    if (git_commit_tree(&commit_tree, commit) < 0) {
        result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: cannot read commit tree");
        goto cleanup;
    }
    if (git_commit_parentcount(commit) > 0) {
        git_commit *parent = NULL;
        if (git_commit_parent(&parent, commit, 0) == 0) {
            git_commit_tree(&parent_tree, parent);
            git_commit_free(parent);
        }
    }
    if (git_diff_tree_to_tree(&diff, repo, parent_tree, commit_tree, NULL) < 0) {
        result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: tree diff failed");
        goto cleanup;
    }

    out = calloc(1, sizeof(*out));
    if (!out) {
        result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: out of memory");
        goto cleanup;
    }
    if (ygit_commit_fill(&out->commit, commit) < 0) {
        result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: out of memory");
        goto cleanup;
    }

    size_t delta_count = git_diff_num_deltas(diff);
    if (delta_count > 0) {
        out->files = calloc(delta_count, sizeof(*out->files));
        if (!out->files) {
            result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: out of memory");
            goto cleanup;
        }
        for (size_t index = 0; index < delta_count; index++) {
            git_patch *patch = NULL;
            if (git_patch_from_diff(&patch, diff, index) < 0) {
                result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: patch build failed");
                goto cleanup;
            }
            const git_diff_delta *delta = git_patch_get_delta(patch);
            struct yetty_ygit_diff_file *file = &out->files[out->file_count];
            out->file_count++; /* slot is zeroed; track before filling */

            file->status = ygit_delta_status_char(delta->status);
            file->is_binary = (delta->flags & GIT_DIFF_FLAG_BINARY) ? 1 : 0;

            int old_present = !git_oid_is_zero(&delta->old_file.id);
            int new_present = !git_oid_is_zero(&delta->new_file.id);
            if (old_present && delta->old_file.path) {
                file->old_path = ygit_strdup(delta->old_file.path);
                if (!file->old_path) {
                    git_patch_free(patch);
                    result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: out of memory");
                    goto cleanup;
                }
            }
            if (new_present && delta->new_file.path) {
                file->new_path = ygit_strdup(delta->new_file.path);
                if (!file->new_path) {
                    git_patch_free(patch);
                    result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: out of memory");
                    goto cleanup;
                }
            }
            if (ygit_copy_blob(repo, &delta->old_file.id, &file->old_data, &file->old_size) < 0 ||
                ygit_copy_blob(repo, &delta->new_file.id, &file->new_data, &file->new_size) < 0) {
                git_patch_free(patch);
                result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: blob read failed");
                goto cleanup;
            }
            if (!file->is_binary && ygit_collect_hunks(patch, file) < 0) {
                git_patch_free(patch);
                result = YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: out of memory");
                goto cleanup;
            }
            git_patch_free(patch);
        }
    }

    result = YETTY_OK(yetty_ygit_diff_ptr, out);
    out = NULL;

cleanup:
    yetty_ygit_diff_destroy(out);
    git_diff_free(diff);
    git_tree_free(parent_tree);
    git_tree_free(commit_tree);
    git_object_free(commit_object);
    git_object_free(object);
    git_repository_free(repo);
    return result;
}

struct yetty_ygit_diff_ptr_result yetty_ygit_backend_diff(const char *repo_path,
                                                          const char *revision)
{
    if (!repo_path) {
        return YETTY_ERR(yetty_ygit_diff_ptr, "ygit diff: NULL path");
    }
    if (git_libgit2_init() < 0) {
        return YETTY_ERR(yetty_ygit_diff_ptr, "ygit: libgit2 init failed");
    }
    struct yetty_ygit_diff_ptr_result result = diff_impl(repo_path, revision);
    git_libgit2_shutdown();
    return result;
}

/* --- read_blob (file contents at a revision) ---------------------------- */

void yetty_ygit_blob_destroy(struct yetty_ygit_blob *blob)
{
    if (!blob) {
        return;
    }
    free(blob->data);
    free(blob->name);
    free(blob);
}

/* Base name of the path portion of a "<rev>:<path>" spec (or of a bare path):
 * the text after the last '/', itself after the last ':'. */
static const char *ygit_spec_basename(const char *spec)
{
    const char *path = strrchr(spec, ':');
    path = path ? path + 1 : spec;
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static struct yetty_ygit_blob_ptr_result read_blob_impl(const char *repo_path, const char *spec)
{
    struct yetty_ygit_blob_ptr_result result =
        YETTY_ERR(yetty_ygit_blob_ptr, "ygit view: unreached");
    git_repository *repo = NULL;
    git_object *object = NULL;
    struct yetty_ygit_blob *blob = NULL;

    /* A spec with no ':' names a path in the working revision — default to HEAD.
     * git_revparse_single resolves "<rev>:<path>" to the blob directly. */
    char *owned_spec = NULL;
    const char *effective_spec = spec;
    if (!strchr(spec, ':')) {
        size_t length = strlen(spec);
        owned_spec = malloc(length + 6); /* "HEAD:" + spec + NUL */
        if (!owned_spec) {
            result = YETTY_ERR(yetty_ygit_blob_ptr, "ygit view: out of memory");
            goto cleanup;
        }
        memcpy(owned_spec, "HEAD:", 5);
        memcpy(owned_spec + 5, spec, length + 1);
        effective_spec = owned_spec;
    }

    if (git_repository_open_ext(&repo, repo_path, 0, NULL) < 0) {
        result = YETTY_ERR(yetty_ygit_blob_ptr, "ygit view: cannot open repository");
        goto cleanup;
    }
    if (git_revparse_single(&object, repo, effective_spec) < 0) {
        result = YETTY_ERR(yetty_ygit_blob_ptr, "ygit view: no such path at that revision");
        goto cleanup;
    }
    if (git_object_type(object) != GIT_OBJECT_BLOB) {
        result = YETTY_ERR(yetty_ygit_blob_ptr, "ygit view: path is a directory, not a file");
        goto cleanup;
    }

    const git_blob *git_blob_object = (const git_blob *)object;
    const void *raw = git_blob_rawcontent(git_blob_object);
    size_t raw_size = (size_t)git_blob_rawsize(git_blob_object);

    blob = calloc(1, sizeof(*blob));
    if (!blob) {
        result = YETTY_ERR(yetty_ygit_blob_ptr, "ygit view: out of memory");
        goto cleanup;
    }
    blob->size = raw_size;
    blob->data = malloc(raw_size ? raw_size : 1);
    blob->name = ygit_strdup(ygit_spec_basename(spec));
    if (!blob->data || !blob->name) {
        result = YETTY_ERR(yetty_ygit_blob_ptr, "ygit view: out of memory");
        goto cleanup;
    }
    if (raw_size) {
        memcpy(blob->data, raw, raw_size);
    }

    result = YETTY_OK(yetty_ygit_blob_ptr, blob);
    blob = NULL;

cleanup:
    yetty_ygit_blob_destroy(blob);
    git_object_free(object);
    git_repository_free(repo);
    free(owned_spec);
    return result;
}

struct yetty_ygit_blob_ptr_result yetty_ygit_backend_read_blob(const char *repo_path,
                                                               const char *spec)
{
    if (!repo_path || !spec) {
        return YETTY_ERR(yetty_ygit_blob_ptr, "ygit view: NULL argument");
    }
    if (git_libgit2_init() < 0) {
        return YETTY_ERR(yetty_ygit_blob_ptr, "ygit: libgit2 init failed");
    }
    struct yetty_ygit_blob_ptr_result result = read_blob_impl(repo_path, spec);
    git_libgit2_shutdown();
    return result;
}
