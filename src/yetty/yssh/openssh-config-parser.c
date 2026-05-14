/* openssh-config-parser.c
 *
 * Minimal parser for ~/.ssh/config and /etc/ssh/ssh_config.
 *
 * Supported keywords (drawn from openssh readconf.c):
 *   HostName, Port, User, IdentityFile, ProxyJump,
 *   ServerAliveInterval, ServerAliveCountMax, ConnectTimeout,
 *   UserKnownHostsFile, StrictHostKeyChecking, ForwardAgent, BatchMode,
 *   Include.
 *
 * All other keywords are silently ignored.
 * Match blocks are not supported (skipped).
 */

#include "openssh-config-parser.h"

#include <yetty/ytrace/ytrace.h>

#include <ctype.h>
#include <fnmatch.h>
#include <glob.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define LINE_BUF  2048
#define MAX_DEPTH 8

enum {
    SET_HOSTNAME    = 1 << 0,
    SET_PORT        = 1 << 1,
    SET_USER        = 1 << 2,
    SET_PROXY_JUMP  = 1 << 3,
    SET_SA_INTERVAL = 1 << 4,
    SET_SA_COUNT    = 1 << 5,
    SET_TIMEOUT     = 1 << 6,
    SET_KNOWN_HOSTS = 1 << 7,
    SET_STRICT_HKC  = 1 << 8,
    SET_FWD_AGENT   = 1 << 9,
    SET_BATCH_MODE  = 1 << 10,
};

struct parser_ctx {
    struct yetty_yssh_openssh_config *cfg;
    const char *host_arg;
    unsigned int set;
    int depth;
};

static char *get_home(void)
{
    const char *h = getenv("HOME");
    if (h && h[0])
        return (char *)h;
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_dir : NULL;
}

/* Expand a leading ~ to the home directory. Returns heap-allocated string. */
static char *expand_tilde(const char *path)
{
    if (path[0] != '~')
        return strdup(path);
    const char *rest = path + 1;
    if (rest[0] != '/' && rest[0] != '\0')
        return strdup(path); /* ~user form — leave as-is */
    char *home = get_home();
    if (!home)
        return strdup(path);
    size_t hlen = strlen(home);
    size_t rlen = strlen(rest);
    char *out = malloc(hlen + rlen + 1);
    if (!out)
        return NULL;
    memcpy(out, home, hlen);
    memcpy(out + hlen, rest, rlen + 1);
    return out;
}

static char *ltrim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    return s;
}

static void rtrim(char *s)
{
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        e--;
    *e = '\0';
}

static char *trim(char *s)
{
    s = ltrim(s);
    rtrim(s);
    return s;
}

static int streqi(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Returns 1 if host_arg matches a space-separated list of glob patterns.
 * A negated pattern (! prefix) forces a no-match. */
static int host_matches_patterns(const char *host_arg, const char *patterns_str)
{
    char buf[512];
    strncpy(buf, patterns_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int matched = 0;
    char *save = NULL;
    char *tok = strtok_r(buf, " \t", &save);
    while (tok) {
        int negate = (tok[0] == '!');
        const char *pat = negate ? tok + 1 : tok;
        if (fnmatch(pat, host_arg, 0) == 0) {
            if (negate)
                return 0;
            matched = 1;
        }
        tok = strtok_r(NULL, " \t", &save);
    }
    return matched;
}

/* yes/no → 1/0, ask → 2, unrecognised → -1 */
static int parse_tristate(const char *val)
{
    if (streqi(val, "yes"))
        return 1;
    if (streqi(val, "no"))
        return 0;
    if (streqi(val, "ask"))
        return 2;
    return -1;
}

static void apply_keyword(struct parser_ctx *ctx, const char *key, const char *val)
{
    struct yetty_yssh_openssh_config *cfg = ctx->cfg;

    if (streqi(key, "hostname")) {
        if (!(ctx->set & SET_HOSTNAME)) {
            free(cfg->hostname);
            cfg->hostname = strdup(val);
            ctx->set |= SET_HOSTNAME;
        }
    } else if (streqi(key, "port")) {
        if (!(ctx->set & SET_PORT)) {
            int p = atoi(val);
            if (p > 0 && p <= 65535) {
                cfg->port = p;
                ctx->set |= SET_PORT;
            }
        }
    } else if (streqi(key, "user")) {
        if (!(ctx->set & SET_USER)) {
            free(cfg->user);
            cfg->user = strdup(val);
            ctx->set |= SET_USER;
        }
    } else if (streqi(key, "identityfile")) {
        /* Accumulates across all matching blocks, up to the cap. */
        if (cfg->num_identity_files < YETTY_YSSH_OPENSSH_CONFIG_MAX_IDENTITY_FILES) {
            char *expanded = expand_tilde(val);
            if (expanded)
                cfg->identity_files[cfg->num_identity_files++] = expanded;
        }
    } else if (streqi(key, "proxyjump")) {
        if (!(ctx->set & SET_PROXY_JUMP)) {
            free(cfg->proxy_jump);
            cfg->proxy_jump = strdup(val);
            ctx->set |= SET_PROXY_JUMP;
        }
    } else if (streqi(key, "serveraliveinterval")) {
        if (!(ctx->set & SET_SA_INTERVAL)) {
            cfg->server_alive_interval = atoi(val);
            ctx->set |= SET_SA_INTERVAL;
        }
    } else if (streqi(key, "serveralivecountmax")) {
        if (!(ctx->set & SET_SA_COUNT)) {
            cfg->server_alive_count_max = atoi(val);
            ctx->set |= SET_SA_COUNT;
        }
    } else if (streqi(key, "connecttimeout")) {
        if (!(ctx->set & SET_TIMEOUT)) {
            cfg->connect_timeout = atoi(val);
            ctx->set |= SET_TIMEOUT;
        }
    } else if (streqi(key, "userknownhostsfile")) {
        if (!(ctx->set & SET_KNOWN_HOSTS)) {
            char *expanded = expand_tilde(val);
            if (expanded) {
                free(cfg->known_hosts_file);
                cfg->known_hosts_file = expanded;
                ctx->set |= SET_KNOWN_HOSTS;
            }
        }
    } else if (streqi(key, "stricthostkeychecking")) {
        if (!(ctx->set & SET_STRICT_HKC)) {
            int v = parse_tristate(val);
            if (v >= 0) {
                cfg->strict_host_key_checking = v;
                ctx->set |= SET_STRICT_HKC;
            }
        }
    } else if (streqi(key, "forwardagent")) {
        if (!(ctx->set & SET_FWD_AGENT)) {
            int v = parse_tristate(val);
            if (v >= 0) {
                cfg->forward_agent = v;
                ctx->set |= SET_FWD_AGENT;
            }
        }
    } else if (streqi(key, "batchmode")) {
        if (!(ctx->set & SET_BATCH_MODE)) {
            int v = parse_tristate(val);
            if (v >= 0) {
                cfg->batch_mode = v;
                ctx->set |= SET_BATCH_MODE;
            }
        }
    }
}

static void parse_file(struct parser_ctx *ctx, const char *path);

static void handle_include(struct parser_ctx *ctx, const char *pattern)
{
    if (ctx->depth >= MAX_DEPTH) {
        ywarn("ssh config: Include depth limit at '%s'", pattern);
        return;
    }
    char *expanded = expand_tilde(pattern);
    if (!expanded)
        return;

    glob_t gl;
    memset(&gl, 0, sizeof(gl));
    if (glob(expanded, GLOB_NOCHECK, NULL, &gl) == 0) {
        for (size_t i = 0; i < gl.gl_pathc; i++) {
            ctx->depth++;
            parse_file(ctx, gl.gl_pathv[i]);
            ctx->depth--;
        }
    }
    globfree(&gl);
    free(expanded);
}

/* Strip an inline comment from val (respects double-quoted sections). */
static void strip_inline_comment(char *val)
{
    int in_quote = 0;
    for (char *p = val; *p; p++) {
        if (*p == '"')
            in_quote = !in_quote;
        if (!in_quote && *p == '#') {
            *p = '\0';
            break;
        }
    }
    rtrim(val);
}

static void parse_file(struct parser_ctx *ctx, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;

    char line[LINE_BUF];
    int in_match = 0;
    int global_section = 1; /* lines before first Host/Match apply to all hosts */

    while (fgets(line, sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '#' || *p == '\0')
            continue;

        /* Split key from value at first whitespace or '='. */
        char *key = p;
        char *val = p;
        while (*val && !isspace((unsigned char)*val) && *val != '=')
            val++;
        if (*val == '=')
            *val++ = '\0';
        else if (*val)
            *val++ = '\0';
        val = ltrim(val);
        /* Remove optional leading '=' after whitespace (key = value) */
        if (*val == '=')
            val = ltrim(val + 1);

        strip_inline_comment(val);

        if (!*key || !*val)
            continue;

        if (streqi(key, "host")) {
            global_section = 0;
            in_match = host_matches_patterns(ctx->host_arg, val);
            continue;
        }

        if (streqi(key, "match")) {
            /* Match blocks are not implemented — skip the block. */
            global_section = 0;
            in_match = 0;
            continue;
        }

        if (streqi(key, "include")) {
            if (global_section || in_match)
                handle_include(ctx, val);
            continue;
        }

        if (global_section || in_match)
            apply_keyword(ctx, key, val);
    }

    fclose(f);
}

struct yetty_yssh_openssh_config *yetty_yssh_openssh_config_resolve(const char *host_arg)
{
    struct yetty_yssh_openssh_config *cfg = calloc(1, sizeof(*cfg));
    if (!cfg)
        return NULL;

    cfg->port                     = 0;
    cfg->server_alive_interval    = -1;
    cfg->server_alive_count_max   = -1;
    cfg->connect_timeout          = -1;
    cfg->strict_host_key_checking = -1;
    cfg->forward_agent            = -1;
    cfg->batch_mode               = -1;

    struct parser_ctx ctx = {
        .cfg      = cfg,
        .host_arg = host_arg,
        .set      = 0,
        .depth    = 0,
    };

    char *home = get_home();
    if (home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.ssh/config", home);
        parse_file(&ctx, path);
    }
    parse_file(&ctx, "/etc/ssh/ssh_config");

    ydebug("ssh config '%s': host=%s port=%d user=%s identities=%d",
           host_arg,
           cfg->hostname ? cfg->hostname : "(none)",
           cfg->port,
           cfg->user ? cfg->user : "(none)",
           cfg->num_identity_files);

    return cfg;
}

void yetty_yssh_openssh_config_free(struct yetty_yssh_openssh_config *cfg)
{
    if (!cfg)
        return;
    free(cfg->hostname);
    free(cfg->user);
    for (int i = 0; i < cfg->num_identity_files; i++)
        free(cfg->identity_files[i]);
    free(cfg->proxy_jump);
    free(cfg->known_hosts_file);
    free(cfg);
}
