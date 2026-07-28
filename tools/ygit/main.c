/*
 * ygit — read-only Git history explorer.
 *
 * This is the thin CLI entry (the structured/textual consumption mode of the
 * tool): it drives the reusable `ygit:repo` yclass component in
 * src/yetty/ygit and prints the result. The same component backs the future
 * interactive pane and scrollback-figure modes.
 *
 *   ygit [-C DIR] status            repository status + branch overview
 *   ygit [-C DIR] log   [-n N] [REV] commit list (metadata + refs)
 *   ygit [-C DIR] graph [-n N] [REV] commit DAG with lane layout
 *   ygit [-C DIR] branches           local branches
 *   ygit [-C DIR] show  REV [-p]     one commit: metadata, body, file changes
 *   ygit [-C DIR] diff  [REV]        a commit's diff, rendered (assets before/
 *                                    after; source syntax-highlighted)
 *   ygit [-C DIR] view  REV:PATH     render a file at a revision inline
 *
 * With no subcommand it prints status. REV defaults to HEAD.
 */

#include "yetty/gen/impl/ygit/repo.h"
#include <yetty/ygit/git-backend.h>
#include <yetty/ygit/commit-graph.h>
#include <yetty/yclass/class.h>
#include <yetty/ycore/terminal-detect.h>

#include "render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

/* Terminal width in columns, defaulting to 80 when stdout is not a tty. */
static int ygit_terminal_cols(void)
{
    struct winsize window;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col > 0) {
        return window.ws_col;
    }
    return 80;
}

/* ANSI colouring, only when stdout is a terminal. */
struct ygit_style {
    const char *dim;
    const char *hash;
    const char *ref;
    const char *head;
    const char *reset;
};

static struct ygit_style ygit_style_for(FILE *stream)
{
    if (isatty(fileno(stream))) {
        return (struct ygit_style){
            .dim = "\x1b[90m",
            .hash = "\x1b[33m",
            .ref = "\x1b[32m",
            .head = "\x1b[36m",
            .reset = "\x1b[0m",
        };
    }
    return (struct ygit_style){.dim = "", .hash = "", .ref = "", .head = "", .reset = ""};
}

static void ygit_format_time(int64_t epoch_seconds, char *buffer, size_t size)
{
    time_t when = (time_t)epoch_seconds;
    struct tm broken;
    if (localtime_r(&when, &broken)) {
        strftime(buffer, size, "%Y-%m-%d %H:%M", &broken);
    } else {
        snprintf(buffer, size, "(unknown time)");
    }
}

static void ygit_print_refs(const struct yetty_ygit_commit *commit, const struct ygit_style *style)
{
    if (commit->ref_count == 0) {
        return;
    }
    printf(" %s(", style->ref);
    for (size_t index = 0; index < commit->ref_count; index++) {
        printf("%s%s", index ? ", " : "", commit->ref_names[index]);
    }
    printf(")%s", style->reset);
}

static void ygit_report_error(struct yetty_ycore_error error)
{
    yetty_ycore_error_print(stderr, "ygit", error);
    yetty_ycore_error_destroy(error);
}

/* --- subcommands -------------------------------------------------------- */

static int ygit_cmd_status(struct yetty_yclass_object *repo, const struct ygit_style *style)
{
    struct yetty_ygit_status_ptr_result status_res = yetty_ygit_repo_status(repo);
    if (YETTY_IS_ERR(status_res)) {
        ygit_report_error(status_res.error);
        return 1;
    }
    struct yetty_ygit_status *status = status_res.value;

    printf("On branch %s%s%s", style->head, status->branch ? status->branch : "(unknown)",
           style->reset);
    if (status->upstream) {
        printf(" %s->%s %s", style->dim, style->reset, status->upstream);
        if (status->ahead || status->behind) {
            printf(" [");
            if (status->ahead) {
                printf("ahead %d", status->ahead);
            }
            if (status->ahead && status->behind) {
                printf(", ");
            }
            if (status->behind) {
                printf("behind %d", status->behind);
            }
            printf("]");
        }
    }
    printf("\n");

    if (status->entry_count == 0) {
        printf("%snothing to commit, working tree clean%s\n", style->dim, style->reset);
    } else {
        printf("\n%zu changed path%s:\n", status->entry_count, status->entry_count == 1 ? "" : "s");
        for (size_t index = 0; index < status->entry_count; index++) {
            const struct yetty_ygit_status_entry *entry = &status->entries[index];
            printf("  %c%c  %s\n", entry->index_status, entry->worktree_status, entry->path);
        }
    }
    yetty_ygit_status_destroy(status);
    return 0;
}

static int ygit_cmd_log(struct yetty_yclass_object *repo, const char *revision, int max_count,
                        int all_refs, const struct ygit_style *style)
{
    struct yetty_ygit_log_ptr_result log_res = all_refs
                                                   ? yetty_ygit_repo_log_all(repo, max_count)
                                                   : yetty_ygit_repo_log(repo, revision, max_count);
    if (YETTY_IS_ERR(log_res)) {
        ygit_report_error(log_res.error);
        return 1;
    }
    struct yetty_ygit_log *log = log_res.value;
    for (size_t index = 0; index < log->count; index++) {
        const struct yetty_ygit_commit *commit = &log->commits[index];
        char when[32];
        ygit_format_time(commit->author_time, when, sizeof(when));
        printf("%s%s%s", style->hash, commit->abbrev_hash, style->reset);
        ygit_print_refs(commit, style);
        printf(" %s\n", commit->subject);
        printf("    %s%s · %s%s\n", style->dim, commit->author_name, when, style->reset);
    }
    if (log->count == 0) {
        printf("%s(no commits)%s\n", style->dim, style->reset);
    }
    yetty_ygit_log_destroy(log);
    return 0;
}

static int ygit_cmd_graph(struct yetty_yclass_object *repo, const char *revision, int max_count,
                          int all_refs, const struct ygit_style *style)
{
    struct yetty_ygit_log_ptr_result log_res = all_refs
                                                   ? yetty_ygit_repo_log_all(repo, max_count)
                                                   : yetty_ygit_repo_log(repo, revision, max_count);
    if (YETTY_IS_ERR(log_res)) {
        ygit_report_error(log_res.error);
        return 1;
    }
    struct yetty_ygit_log *log = log_res.value;

    struct yetty_ygit_graph_ptr_result graph_res = yetty_ygit_graph_build(log);
    if (YETTY_IS_ERR(graph_res)) {
        ygit_report_error(graph_res.error);
        yetty_ygit_log_destroy(log);
        return 1;
    }
    struct yetty_ygit_graph *graph = graph_res.value;

    /* Inside yetty, draw the DAG as a GPU figure (coloured lanes + circle
     * nodes). Anywhere else, fall back to the Unicode lane view below. */
    if (yetty_running_under_yetty()) {
        int figure_status = ygit_render_graph_figure(log, graph, ygit_terminal_cols());
        yetty_ygit_graph_destroy(graph);
        yetty_ygit_log_destroy(log);
        return figure_status;
    }

    for (size_t index = 0; index < log->count; index++) {
        const struct yetty_ygit_commit *commit = &log->commits[index];
        const struct yetty_ygit_graph_row *row = &graph->rows[index];
        /* Lane art, padded to the graph width so commit text aligns. */
        for (int lane = 0; lane < graph->width; lane++) {
            int drawn = lane < row->lane_count && row->occupied[lane];
            if (lane == row->column) {
                printf("%s●%s ", style->hash, style->reset);
            } else if (drawn) {
                printf("%s│%s ", style->dim, style->reset);
            } else {
                printf("  ");
            }
        }
        printf("%s%s%s", style->hash, commit->abbrev_hash, style->reset);
        ygit_print_refs(commit, style);
        printf(" %s\n", commit->subject);
    }
    if (log->count == 0) {
        printf("%s(no commits)%s\n", style->dim, style->reset);
    }
    yetty_ygit_graph_destroy(graph);
    yetty_ygit_log_destroy(log);
    return 0;
}

static int ygit_cmd_branches(struct yetty_yclass_object *repo, const struct ygit_style *style)
{
    struct yetty_ygit_branches_ptr_result branches_res = yetty_ygit_repo_branches(repo);
    if (YETTY_IS_ERR(branches_res)) {
        ygit_report_error(branches_res.error);
        return 1;
    }
    struct yetty_ygit_branches *branches = branches_res.value;
    for (size_t index = 0; index < branches->count; index++) {
        const struct yetty_ygit_branch *branch = &branches->branches[index];
        const char *marker = branch->is_head ? "*" : " ";
        const char *name_color = branch->is_head ? style->head : "";
        printf("%s %s%-24s%s %s%s%s  %s\n", marker, name_color, branch->name, style->reset,
               style->hash, branch->tip_abbrev_hash, style->reset, branch->subject);
    }
    if (branches->count == 0) {
        printf("%s(no local branches)%s\n", style->dim, style->reset);
    }
    yetty_ygit_branches_destroy(branches);
    return 0;
}

static int ygit_cmd_show(struct yetty_yclass_object *repo, const char *revision,
                         const struct ygit_style *style)
{
    struct yetty_ygit_commit_detail_ptr_result detail_res = yetty_ygit_repo_show(repo, revision);
    if (YETTY_IS_ERR(detail_res)) {
        ygit_report_error(detail_res.error);
        return 1;
    }
    struct yetty_ygit_commit_detail *detail = detail_res.value;
    const struct yetty_ygit_commit *commit = &detail->commit;
    char when[32];
    ygit_format_time(commit->author_time, when, sizeof(when));

    printf("%scommit %s%s", style->hash, commit->full_hash, style->reset);
    ygit_print_refs(commit, style);
    printf("\n");
    printf("Author: %s <%s>\n", commit->author_name, commit->author_email);
    printf("Date:   %s\n\n", when);
    printf("    %s\n", commit->subject);
    if (detail->body && detail->body[0]) {
        printf("\n    %s\n", detail->body);
    }
    if (detail->file_count > 0) {
        printf("\n%s%zu file%s changed:%s\n", style->dim, detail->file_count,
               detail->file_count == 1 ? "" : "s", style->reset);
        for (size_t index = 0; index < detail->file_count; index++) {
            const struct yetty_ygit_file_change *change = &detail->files[index];
            if (change->added < 0) {
                printf("  %-8s %s\n", "bin", change->path);
            } else {
                printf("  %s+%d%s %s-%d%s\t%s\n", style->ref, change->added, style->reset,
                       style->head, change->deleted, style->reset, change->path);
            }
        }
    }
    yetty_ygit_commit_detail_destroy(detail);
    return 0;
}

/* Render a file as it existed at a revision, inline. This is the view git
 * cannot give you: an image, a PDF, an SVG, rendered Markdown, or highlighted
 * source, drawn from history. */
static int ygit_cmd_view(struct yetty_yclass_object *repo, const char *spec)
{
    struct yetty_ygit_blob_ptr_result blob_res = yetty_ygit_repo_read_blob(repo, spec);
    if (YETTY_IS_ERR(blob_res)) {
        ygit_report_error(blob_res.error);
        return 1;
    }
    struct yetty_ygit_blob *blob = blob_res.value;
    int status = ygit_render_blob(blob->data, blob->size, blob->name, ygit_terminal_cols());
    yetty_ygit_blob_destroy(blob);
    return status;
}

/* Render a commit's diff against its first parent. Renderable assets are drawn
 * as a visual before/after; text/source as a syntax-highlighted unified diff.
 * This is the diff git cannot draw. */
static int ygit_cmd_diff(struct yetty_yclass_object *repo, const char *revision)
{
    struct yetty_ygit_diff_ptr_result diff_res = yetty_ygit_repo_diff(repo, revision);
    if (YETTY_IS_ERR(diff_res)) {
        ygit_report_error(diff_res.error);
        return 1;
    }
    struct yetty_ygit_diff *diff = diff_res.value;
    int status = 0;
    if (diff->file_count == 0) {
        printf("(no changes in this commit)\n");
    } else {
        status = ygit_render_diff(diff, ygit_terminal_cols());
    }
    yetty_ygit_diff_destroy(diff);
    return status;
}

static void ygit_usage(FILE *stream)
{
    fprintf(stream, "usage: ygit [-C DIR] <command> [args]\n"
                    "  status                 repository status + branch overview\n"
                    "  log   [-n N] [--all] [REV]  commit list (metadata + refs)\n"
                    "  graph [-n N] [--all] [REV]  commit DAG (drawn inside yetty)\n"
                    "  branches               local branches\n"
                    "  show  REV [-p]         one commit: metadata, body, file changes\n"
                    "                         (-p also renders the diff)\n"
                    "  diff  [REV]            a commit's diff against its parent, rendered\n"
                    "                         (assets drawn before/after; source highlighted)\n"
                    "  view  REV:PATH         render a file at a revision inline\n"
                    "                         (image / PDF / SVG / Markdown / source)\n");
}

int main(int argc, char **argv)
{
    const char *repo_path = ".";
    int arg = 1;

    /* Leading -C DIR (repository path) before the subcommand. */
    while (arg < argc && argv[arg][0] == '-') {
        if (strcmp(argv[arg], "-C") == 0 && arg + 1 < argc) {
            repo_path = argv[arg + 1];
            arg += 2;
        } else if (strcmp(argv[arg], "-h") == 0 || strcmp(argv[arg], "--help") == 0) {
            ygit_usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "ygit: unknown option '%s'\n", argv[arg]);
            ygit_usage(stderr);
            return 2;
        }
    }

    const char *command = (arg < argc) ? argv[arg++] : "status";

    /* Optional -n N for log / graph, --all for log / graph, -p/--patch for show,
     * then an optional REV. */
    int max_count = 100;
    int show_patch = 0;
    int all_refs = 0;
    const char *revision = NULL;
    while (arg < argc) {
        if (strcmp(argv[arg], "-n") == 0 && arg + 1 < argc) {
            max_count = atoi(argv[arg + 1]);
            arg += 2;
        } else if (strcmp(argv[arg], "-p") == 0 || strcmp(argv[arg], "--patch") == 0) {
            show_patch = 1;
            arg += 1;
        } else if (strcmp(argv[arg], "-a") == 0 || strcmp(argv[arg], "--all") == 0) {
            all_refs = 1;
            arg += 1;
        } else {
            revision = argv[arg++];
        }
    }

    struct ygit_style style = ygit_style_for(stdout);

    struct yetty_ycore_void_result register_res = yetty_ygit_register();
    if (YETTY_IS_ERR(register_res)) {
        ygit_report_error(register_res.error);
        return 1;
    }

    struct yetty_yclass_ctx ctx = {0}; /* session NULL → local, in-process */
    struct yetty_yclass_object_ptr_result repo_res = yetty_ygit_repo_create(&ctx);
    if (YETTY_IS_ERR(repo_res)) {
        ygit_report_error(repo_res.error);
        return 1;
    }
    struct yetty_yclass_object *repo = repo_res.value;

    struct yetty_ycore_void_result open_res = yetty_ygit_repo_open(repo, repo_path);
    if (YETTY_IS_ERR(open_res)) {
        ygit_report_error(open_res.error);
        yetty_ygit_repo_destroy(repo);
        return 1;
    }

    struct yetty_ycore_int_result is_repo_res = yetty_ygit_repo_is_repo(repo);
    if (YETTY_IS_ERR(is_repo_res)) {
        ygit_report_error(is_repo_res.error);
        yetty_ygit_repo_destroy(repo);
        return 1;
    }
    if (!is_repo_res.value) {
        fprintf(stderr, "ygit: '%s' is not a Git repository\n", repo_path);
        yetty_ygit_repo_destroy(repo);
        return 1;
    }

    int status = 0;
    if (strcmp(command, "status") == 0) {
        status = ygit_cmd_status(repo, &style);
    } else if (strcmp(command, "log") == 0) {
        status = ygit_cmd_log(repo, revision, max_count, all_refs, &style);
    } else if (strcmp(command, "graph") == 0) {
        status = ygit_cmd_graph(repo, revision, max_count, all_refs, &style);
    } else if (strcmp(command, "branches") == 0) {
        status = ygit_cmd_branches(repo, &style);
    } else if (strcmp(command, "show") == 0) {
        if (!revision) {
            fprintf(stderr, "ygit show: a revision is required\n");
            status = 2;
        } else {
            status = ygit_cmd_show(repo, revision, &style);
            if (status == 0 && show_patch) {
                status = ygit_cmd_diff(repo, revision);
            }
        }
    } else if (strcmp(command, "diff") == 0) {
        status = ygit_cmd_diff(repo, revision);
    } else if (strcmp(command, "view") == 0) {
        if (!revision) {
            fprintf(stderr, "ygit view: a REV:PATH spec is required\n");
            status = 2;
        } else {
            status = ygit_cmd_view(repo, revision);
        }
    } else {
        fprintf(stderr, "ygit: unknown command '%s'\n", command);
        ygit_usage(stderr);
        status = 2;
    }

    yetty_ygit_repo_destroy(repo);
    return status;
}
