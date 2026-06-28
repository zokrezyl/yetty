/*
 * codex-quota.c — read codex's plan rate-limits from its rollout file.
 *
 * Codex on a ChatGPT plan sends its model /responses call straight to
 * chatgpt.com (no base-URL knob redirects it), so the usage proxy never sees
 * the rate-limit data. Codex does, however, persist it: every turn it appends
 * an `event_msg` line with payload.type == "token_count" to its per-session
 * rollout file, and that payload carries a `rate_limits` object — the same
 * 5h/weekly windows the codex `/status` screen shows.
 *
 *   $CODEX_HOME/sessions/YYYY/MM/DD/rollout-<ts>-<thread_id>.jsonl
 *     {"type":"event_msg","payload":{"type":"token_count","rate_limits":{
 *        "primary":  {"used_percent":65,"window_minutes":300,  "resets_at":...},
 *        "secondary":{"used_percent":13,"window_minutes":10080,"resets_at":...}}}}
 *
 * CODEX_HOME defaults to ~/.codex; the thread_id is yai's app->session_id for a
 * codex session. We tail the rollout (the token_count is appended every turn,
 * so the latest sits near the end) and fill struct yai_quota: primary -> the
 * 5h "session" window, secondary -> the weekly window. used_percent maps to the
 * utilization fields directly (matching claude's proxy-sourced quota, which is
 * also utilization, not remaining).
 */

#include "app.h"

#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

/* Tail size: the token_count event is appended each turn, so the most recent
 * one is always within the last few KB. 256 KiB is generous headroom and bounds
 * the work per refresh regardless of how long the session has run. */
enum { YAI_CODEX_ROLLOUT_TAIL = 256 * 1024 };

/* Resolve the rollout file for this session via $CODEX_HOME (default ~/.codex).
 * The filename ends with the thread_id (== app->session_id). Returns 1 and
 * writes the path into `out` on success, 0 if no session id or no match. */
static int yai_codex_rollout_path(const struct yai_app *app, char *out, size_t out_size)
{
    if (!app->session_id[0]) {
        return 0;
    }
    const char *codex_home = getenv("CODEX_HOME");
    char home_buf[PATH_MAX];
    if (!codex_home || !codex_home[0]) {
        const char *home = getenv("HOME");
        if (!home || !home[0]) {
            return 0;
        }
        snprintf(home_buf, sizeof(home_buf), "%s/.codex", home);
        codex_home = home_buf;
    }

    /* sessions/YYYY/MM/DD/rollout-<ts>-<session_id>.jsonl */
    char pattern[PATH_MAX];
    int written = snprintf(pattern, sizeof(pattern), "%s/sessions/*/*/*/rollout-*%s.jsonl",
                           codex_home, app->session_id);
    if (written < 0 || (size_t)written >= sizeof(pattern)) {
        return 0;
    }

    glob_t globbed;
    if (glob(pattern, 0, NULL, &globbed) != 0) {
        globfree(&globbed);
        return 0;
    }
    int found = 0;
    if (globbed.gl_pathc > 0) {
        /* session_id is unique; if more than one matched, the last (glob sorts
         * lexically, so latest date) is the right one. */
        const char *match = globbed.gl_pathv[globbed.gl_pathc - 1];
        if (snprintf(out, out_size, "%s", match) < (int)out_size) {
            found = 1;
        }
    }
    globfree(&globbed);
    return found;
}

/* Pull a numeric field out of a rate-limit window object. */
static double window_num(yyjson_val *window, const char *key)
{
    yyjson_val *value = yyjson_obj_get(window, key);
    return value ? yyjson_get_num(value) : 0.0;
}

/* Parse one rate_limits object into the quota struct. Returns 1 if at least one
 * window was present. primary -> 5h "session", secondary -> weekly. */
static int yai_codex_fill_from_rate_limits(yyjson_val *rate_limits, struct yai_quota *out)
{
    yyjson_val *primary = yyjson_obj_get(rate_limits, "primary");
    yyjson_val *secondary = yyjson_obj_get(rate_limits, "secondary");
    if (!yyjson_is_obj(primary) && !yyjson_is_obj(secondary)) {
        return 0;
    }
    if (yyjson_is_obj(primary)) {
        out->session_pct = (int)(window_num(primary, "used_percent") + 0.5);
        out->session_reset = (long long)window_num(primary, "resets_at");
    }
    if (yyjson_is_obj(secondary)) {
        out->week_pct = (int)(window_num(secondary, "used_percent") + 0.5);
        out->week_reset = (long long)window_num(secondary, "resets_at");
    }
    out->valid = 1;
    return 1;
}

int yai_codex_quota_read(const struct yai_app *app, struct yai_quota *out)
{
    memset(out, 0, sizeof(*out));

    char path[PATH_MAX];
    if (!yai_codex_rollout_path(app, path, sizeof(path))) {
        return 0;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    long size = ftell(file);
    if (size <= 0) {
        fclose(file);
        return 0;
    }
    long start = size > YAI_CODEX_ROLLOUT_TAIL ? size - YAI_CODEX_ROLLOUT_TAIL : 0;
    if (fseek(file, start, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    size_t to_read = (size_t)(size - start);
    char *buf = malloc(to_read + 1);
    if (!buf) {
        fclose(file);
        return 0;
    }
    size_t got = fread(buf, 1, to_read, file);
    fclose(file);
    buf[got] = '\0';

    /* If we started mid-file, the first line is probably partial — skip to the
     * first newline so we only parse whole JSONL records. */
    char *cursor = buf;
    if (start > 0) {
        char *newline = memchr(buf, '\n', got);
        cursor = newline ? newline + 1 : buf + got;
    }

    /* Scan every whole line; keep the LAST token_count.rate_limits we see. */
    int found = 0;
    char *line = cursor;
    while (line < buf + got) {
        char *newline = memchr(line, '\n', (size_t)(buf + got - line));
        size_t line_len = newline ? (size_t)(newline - line) : (size_t)(buf + got - line);
        if (line_len > 1) {
            yyjson_doc *doc = yyjson_read(line, line_len, 0);
            if (doc) {
                yyjson_val *root = yyjson_doc_get_root(doc);
                yyjson_val *payload = yyjson_obj_get(root, "payload");
                yyjson_val *type = payload ? yyjson_obj_get(payload, "type") : NULL;
                const char *type_str = type ? yyjson_get_str(type) : NULL;
                if (type_str && strcmp(type_str, "token_count") == 0) {
                    yyjson_val *rate_limits = yyjson_obj_get(payload, "rate_limits");
                    if (yyjson_is_obj(rate_limits)) {
                        struct yai_quota parsed = {0};
                        if (yai_codex_fill_from_rate_limits(rate_limits, &parsed)) {
                            *out = parsed;
                            found = 1;
                        }
                    }
                }
                yyjson_doc_free(doc);
            }
        }
        if (!newline) {
            break;
        }
        line = newline + 1;
    }

    free(buf);
    return found;
}
