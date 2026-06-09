/*
 * ylexbor-css-vars — CSS custom-property collector + var() resolver.
 *
 * lexbor's CSS cascade hands us the *raw* declared value when we
 * read a property — it doesn't substitute `var(--foo)` for us. Modern
 * sites (every Tailwind v4 / MUI / Bootstrap 5 build) put their entire
 * design token system in custom properties, so unresolved var() refs
 * were our number-one source of "the page renders blank/white".
 *
 * Strategy:
 *   1. Every time a stylesheet source string is added (load_html's
 *      external <link> + <style> walker), scan for declarations of the
 *      form `--name: value;` whose enclosing rule looks like a global
 *      target (:root / html / body / *). Store name→value.
 *   2. When the box-build reads a value (color, currently), pipe it
 *      through resolve_vars() first. We substitute every var(--name)
 *      occurrence with its stored value, recursively (with a depth
 *      limit to break cycles).
 *
 * Skips:
 *   - Per-element custom properties (rare on real sites; would need a
 *     per-element table indexed by lxb pointer). The :root path covers
 *     the >95% case.
 *   - Whitespace-trim / case-fold inside calc() — pass-through.
 */

#include "ybrowser-internal.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h> /* strncasecmp */

/* ===========================================================================
 * Storage — linear scan; tens to low hundreds of entries on real sites.
 * ===========================================================================*/

static int customs_set(struct yetty_ylexbor_customs *t, const char *name, size_t nlen,
                       const char *value, size_t vlen)
{
    for (int i = 0; i < t->size; i++) {
        if (strlen(t->data[i].name) == nlen && memcmp(t->data[i].name, name, nlen) == 0) {
            char *nv = malloc(vlen + 1);
            if (!nv) {
                return -1;
            }
            memcpy(nv, value, vlen);
            nv[vlen] = '\0';
            free(t->data[i].value);
            t->data[i].value = nv;
            return 0;
        }
    }
    if (t->size == t->cap) {
        int nc = t->cap ? t->cap * 2 : 32;
        struct yetty_ylexbor_custom_prop *p = realloc(t->data, nc * sizeof(*p));
        if (!p) {
            return -1;
        }
        t->data = p;
        t->cap = nc;
    }
    char *nm = malloc(nlen + 1);
    char *vl = malloc(vlen + 1);
    if (!nm || !vl) {
        free(nm);
        free(vl);
        return -1;
    }
    memcpy(nm, name, nlen);
    nm[nlen] = '\0';
    memcpy(vl, value, vlen);
    vl[vlen] = '\0';
    t->data[t->size].name = nm;
    t->data[t->size].value = vl;
    t->size++;
    return 0;
}

static const char *customs_get(const struct yetty_ylexbor_customs *t, const char *name, size_t nlen)
{
    for (int i = 0; i < t->size; i++) {
        if (strlen(t->data[i].name) == nlen && memcmp(t->data[i].name, name, nlen) == 0) {
            return t->data[i].value;
        }
    }
    return NULL;
}

void yetty_ylexbor_css_vars_destroy(struct yetty_ylexbor *r)
{
    for (int i = 0; i < r->customs.size; i++) {
        free(r->customs.data[i].name);
        free(r->customs.data[i].value);
    }
    free(r->customs.data);
    r->customs.data = NULL;
    r->customs.size = r->customs.cap = 0;
}

/* Scan for the `minmax(0, <len>)` grid content-column idiom (e.g.
 * `grid-template-columns: ... minmax(0, 59.25rem) ...` used by modern
 * skins to cap the readable column) and record the widest such track in
 * the readable range into r->grid_content_max_px. minmax() only appears
 * in grid track lists, so matching the call form alone is safe. We accept
 * rem / em (×16) and px; anything else (1fr, %, auto) is not a fixed cap
 * and is ignored. */
void yetty_ylexbor_css_scan_grid_content_width(struct yetty_ylexbor *r, const char *src, size_t len)
{
    if (r == NULL || src == NULL || len < 8) {
        return;
    }
    static const char needle[] = "minmax(";
    const size_t nlen = sizeof(needle) - 1;
    for (size_t i = 0; i + nlen < len;) {
        if (memcmp(src + i, needle, nlen) != 0) {
            i++;
            continue;
        }
        size_t j = i + nlen;
        /* Skip the min argument up to the separating comma. */
        while (j < len && src[j] != ',' && src[j] != ')') {
            j++;
        }
        if (j >= len || src[j] != ',') {
            i += nlen;
            continue;
        }
        j++; /* past the comma */
        while (j < len && (src[j] == ' ' || src[j] == '\t')) {
            j++;
        }
        /* Parse the max-track number. */
        char numbuf[32];
        size_t k = 0;
        while (j < len && k < sizeof(numbuf) - 1 &&
               (src[j] == '.' || src[j] == '+' || src[j] == '-' ||
                (src[j] >= '0' && src[j] <= '9'))) {
            numbuf[k++] = src[j++];
        }
        numbuf[k] = '\0';
        if (k > 0) {
            double value = atof(numbuf);
            double px = -1.0;
            if (j + 3 <= len && strncmp(src + j, "rem", 3) == 0) {
                px = value * 16.0;
            } else if (j + 2 <= len && strncmp(src + j, "em", 2) == 0) {
                px = value * 16.0;
            } else if (j + 2 <= len && strncmp(src + j, "px", 2) == 0) {
                px = value;
            }
            /* Only a "content column" sized track — between ~30em and
             * ~90em — is treated as the readable cap; small tracks are
             * gallery/grid cells and full-page tracks aren't a cap. */
            if (px >= 480.0 && px <= 1440.0 && (float)px > r->grid_content_max_px) {
                r->grid_content_max_px = (float)px;
            }
        }
        i += nlen;
    }
}

/* ===========================================================================
 * Scanner — accept declarations from rules whose selector list contains
 * any of `:root`, `html`, `body`, `*`. We skip @media / @supports
 * blocks for now (they'd need media-query evaluation which we don't do).
 * ===========================================================================*/

static int sel_is_global_target_simple(const char *sel, size_t len)
{
    while (len > 0 && (*sel == ' ' || *sel == '\t' || *sel == '\n')) {
        sel++;
        len--;
    }
    while (len > 0 && (sel[len - 1] == ' ' || sel[len - 1] == '\t' || sel[len - 1] == '\n')) {
        len--;
    }
    if (len == 0) {
        return 0;
    }
    if (len == 5 && strncasecmp(sel, ":root", 5) == 0) {
        return 1;
    }
    if (len == 4 && strncasecmp(sel, "html", 4) == 0) {
        return 1;
    }
    if (len == 4 && strncasecmp(sel, "body", 4) == 0) {
        return 1;
    }
    if (len == 1 && sel[0] == '*') {
        return 1;
    }
    return 0;
}

static int sel_is_global_target(const char *sel, size_t len)
{
    /* For practical fidelity on real-world themes, we accept custom
	 * properties from *any* rule and treat them as global. The
	 * spec-correct approach scopes per-element, but every modern
	 * site's design tokens live on attribute selectors like
	 * `[data-color-mode="light"]` or `[data-theme="dark"]`. We
	 * can't evaluate those at parse-time without running the full
	 * cascade per element, so we collect everything; last-write-
	 * wins. The user can still flip themes via JS — that mutates
	 * the cascade output we read through computed style. */
    (void)sel;
    (void)len;
    (void)sel_is_global_target_simple;
    return 1;
}

/* Walk `src` looking for top-level rules. For each rule whose selector
 * looks like :root / html / body / asterisk-wildcard, scan its
 * declaration block for `--…` declarations and stash. */
void yetty_ylexbor_css_vars_scan(struct yetty_ylexbor *r, const char *src, size_t len)
{
    if (!src || len == 0) {
        return;
    }
    const char *p = src;
    const char *end = src + len;

    while (p < end) {
        /* Skip whitespace + comments. */
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            p++;
        }
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) {
                p++;
            }
            if (p + 1 < end) {
                p += 2;
            } else {
                p = end;
            }
            continue;
        }
        if (p >= end) {
            break;
        }

        /* Skip @-rule blocks. We used to descend recursively so that
		 * :root vars inside @media still got captured — but that's
		 * the wrong call for theme-aware sites like Wikipedia. Their
		 * stylesheet defines a default light palette in :root then a
		 * dark palette inside @media (prefers-color-scheme: dark). The
		 * dark block comes LATER in source order, so last-write-wins
		 * left us with dark-theme values (--color-base: #eaecf0, near-
		 * white text). On a default-light terminal that means the
		 * whole article renders in near-invisible light gray.
		 *
		 * We DO descend into @supports / @document / similar conditional
		 * blocks that aren't user-preference gated. The @media rule is
		 * the noisy one — explicitly skip it. */
        if (*p == '@') {
            /* Identify the @-rule keyword to decide whether to
			 * descend or skip. */
            const char *kw_start = p + 1;
            const char *kw_end = kw_start;
            while (kw_end < end && ((*kw_end >= 'a' && *kw_end <= 'z') ||
                                    (*kw_end >= 'A' && *kw_end <= 'Z') || *kw_end == '-')) {
                kw_end++;
            }
            size_t kw_len = (size_t)(kw_end - kw_start);
            int is_media = (kw_len == 5 && strncasecmp(kw_start, "media", 5) == 0);
            int is_keyframes = (kw_len == 9 && strncasecmp(kw_start, "keyframes", 9) == 0);
            int is_font_face = (kw_len == 9 && strncasecmp(kw_start, "font-face", 9) == 0);

            /* Find { or ; */
            while (p < end && *p != '{' && *p != ';') {
                p++;
            }
            if (p < end && *p == ';') {
                p++;
                continue;
            }
            if (p < end && *p == '{') {
                p++;
                /* Find matching close — balance braces. */
                int depth = 1;
                const char *body_start = p;
                while (p < end && depth > 0) {
                    if (*p == '{') {
                        depth++;
                    } else if (*p == '}') {
                        depth--;
                    }
                    if (depth > 0) {
                        p++;
                    }
                }
                /* Skip @media / @keyframes / @font-face entirely.
				 * Recurse into other @-rules (@supports etc.) so
				 * :root vars defined there still get captured. */
                if (!is_media && !is_keyframes && !is_font_face) {
                    yetty_ylexbor_css_vars_scan(r, body_start, (size_t)(p - body_start));
                }
                if (p < end) {
                    p++; /* skip closing } */
                }
            }
            continue;
        }

        /* Selector list runs up to '{'. */
        const char *sel = p;
        while (p < end && *p != '{') {
            p++;
        }
        if (p >= end) {
            break;
        }
        size_t sel_len = (size_t)(p - sel);
        p++; /* past { */
        const char *block = p;
        int depth = 1;
        while (p < end && depth > 0) {
            if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
            }
            if (depth > 0) {
                p++;
            }
        }
        size_t block_len = (size_t)(p - block);
        if (p < end) {
            p++;
        }

        if (!sel_is_global_target(sel, sel_len)) {
            continue;
        }

        /* Walk declarations: NAME : VALUE ; — but watch for
		 * nested () (var(...) values), [], "...", '...'. */
        const char *q = block;
        const char *bend = block + block_len;
        while (q < bend) {
            while (q < bend && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r' || *q == ';')) {
                q++;
            }
            if (q + 1 < bend && q[0] == '/' && q[1] == '*') {
                q += 2;
                while (q + 1 < bend && !(q[0] == '*' && q[1] == '/')) {
                    q++;
                }
                if (q + 1 < bend) {
                    q += 2;
                } else {
                    q = bend;
                }
                continue;
            }
            if (q >= bend) {
                break;
            }

            /* Property name. */
            const char *nm = q;
            while (q < bend && *q != ':' && *q != ';') {
                q++;
            }
            if (q >= bend || *q == ';') {
                continue;
            }
            size_t nlen = (size_t)(q - nm);
            while (nlen > 0 && (nm[nlen - 1] == ' ' || nm[nlen - 1] == '\t')) {
                nlen--;
            }
            q++; /* past : */
            while (q < bend && (*q == ' ' || *q == '\t')) {
                q++;
            }
            const char *vstart = q;
            /* Value runs to ; (top level only). */
            int paren = 0, bracket = 0;
            char quote = 0;
            while (q < bend) {
                char c = *q;
                if (quote) {
                    if (c == quote) {
                        quote = 0;
                    }
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '(') {
                    paren++;
                } else if (c == ')') {
                    paren--;
                } else if (c == '[') {
                    bracket++;
                } else if (c == ']') {
                    bracket--;
                } else if (c == ';' && paren == 0 && bracket == 0) {
                    break;
                }
                q++;
            }
            size_t vlen = (size_t)(q - vstart);
            while (vlen > 0 && (vstart[vlen - 1] == ' ' || vstart[vlen - 1] == '\t' ||
                                vstart[vlen - 1] == '\n' || vstart[vlen - 1] == '\r')) {
                vlen--;
            }
            if (q < bend) {
                q++; /* skip ; */
            }

            if (nlen >= 2 && nm[0] == '-' && nm[1] == '-' && vlen > 0) {
                (void)customs_set(&r->customs, nm, nlen, vstart, vlen);
            }
        }
    }
}

/* ===========================================================================
 * Resolver — substitute `var(--name [, fallback])` recursively.
 *
 * The CSS spec allows arbitrary token substitution; we only handle
 * what's actually used to gate page colors. Specifically:
 *   - var(--name) → looked up; missing falls through to "".
 *   - var(--name, fallback) → fallback expression (which may itself
 *     contain var()) when --name is absent or empty.
 *   - Nested calls are resolved depth-first.
 *   - A depth limit (32) breaks cycles.
 * ===========================================================================*/

static int append_str(char **buf, size_t *len, size_t *cap, const char *src, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap * 2 : 256;
        while (nc < *len + n + 1) {
            nc *= 2;
        }
        char *p = realloc(*buf, nc);
        if (!p) {
            return -1;
        }
        *buf = p;
        *cap = nc;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

static int resolve_into(struct yetty_ylexbor *r, const char *src, size_t n, char **out,
                        size_t *olen, size_t *ocap, int depth);

static int resolve_one_var(struct yetty_ylexbor *r, const char *body, size_t blen, char **out,
                           size_t *olen, size_t *ocap, int depth)
{
    /* body = "--name" or "--name , fallback expr". */
    size_t i = 0;
    while (i < blen && (body[i] == ' ' || body[i] == '\t')) {
        i++;
    }
    const char *name = body + i;
    size_t name_len = 0;
    while (i < blen && body[i] != ',' && body[i] != ' ' && body[i] != '\t') {
        i++;
        name_len++;
    }
    while (i < blen && (body[i] == ' ' || body[i] == '\t')) {
        i++;
    }
    const char *fallback = NULL;
    size_t fallback_len = 0;
    if (i < blen && body[i] == ',') {
        i++;
        while (i < blen && (body[i] == ' ' || body[i] == '\t')) {
            i++;
        }
        fallback = body + i;
        fallback_len = blen - i;
        while (fallback_len > 0 &&
               (fallback[fallback_len - 1] == ' ' || fallback[fallback_len - 1] == '\t')) {
            fallback_len--;
        }
    }

    const char *val = customs_get(&r->customs, name, name_len);
    if (val && val[0]) {
        return resolve_into(r, val, strlen(val), out, olen, ocap, depth + 1);
    }
    if (fallback && fallback_len > 0) {
        return resolve_into(r, fallback, fallback_len, out, olen, ocap, depth + 1);
    }
    return 0;
}

static int resolve_into(struct yetty_ylexbor *r, const char *src, size_t n, char **out,
                        size_t *olen, size_t *ocap, int depth)
{
    if (depth > 32) {
        /* Cycle / pathological input — give up. */
        return append_str(out, olen, ocap, src, n);
    }
    size_t i = 0;
    while (i < n) {
        /* Find next "var(" not inside string. We don't currently
		 * track strings here because var() inside strings is
		 * not substituted by the spec; matter of taste. */
        if (i + 4 < n && memcmp(src + i, "var(", 4) == 0) {
            i += 4;
            /* Find matching ')' tracking nested parens. */
            size_t start = i;
            int paren = 1;
            while (i < n && paren > 0) {
                if (src[i] == '(') {
                    paren++;
                } else if (src[i] == ')') {
                    paren--;
                }
                if (paren > 0) {
                    i++;
                }
            }
            size_t blen = i - start;
            (void)resolve_one_var(r, src + start, blen, out, olen, ocap, depth);
            if (i < n) {
                i++; /* skip ) */
            }
            continue;
        }
        if (append_str(out, olen, ocap, src + i, 1) != 0) {
            return -1;
        }
        i++;
    }
    return 0;
}

char *yetty_ylexbor_css_vars_resolve(struct yetty_ylexbor *r, const char *value, size_t len)
{
    char *out = NULL;
    size_t olen = 0, ocap = 0;
    if (resolve_into(r, value, len, &out, &olen, &ocap, 0) != 0) {
        free(out);
        out = malloc(len + 1);
        if (!out) {
            return NULL;
        }
        memcpy(out, value, len);
        out[len] = '\0';
    }
    if (!out) {
        out = malloc(1);
        if (out) {
            out[0] = '\0';
        }
    }
    return out;
}

/* ===========================================================================
 * Class-scoped CSS Grid template scanning.
 *
 * libcss parses none of `grid-template-columns` / `gap` / `grid-column`, so we
 * extract the templates ourselves from the author CSS, keyed by class. This
 * covers the dominant real-world idiom — a card laid out as
 * `.cls{display:grid;grid-template-columns:1fr 80px;column-gap:12px}` — which
 * is what makes news sites' story cards (thumbnail + text) compact. We only
 * parse the column track list; row tracks and spans are not modelled.
 * ===========================================================================*/

/* Copy [s, s+n) into buf (NUL-terminated, truncated to bufsz-1). */
static void grid_tok_copy(const char *s, size_t n, char *buf, size_t bufsz)
{
    size_t k = 0;
    while (k < n && k < bufsz - 1) {
        buf[k] = s[k];
        k++;
    }
    buf[k] = '\0';
}

/* Parse one track spec (already isolated): `<n>fr`, `<n>px`/`<n>`,
 * `minmax(a,b)` (uses b), `auto`/`min-content`/`max-content` (≈ 1fr). */
static int grid_parse_one_track(const char *s, size_t n, struct yl_grid_track *out)
{
    while (n > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        n--;
    }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        n--;
    }
    if (n == 0) {
        return 0;
    }
    if (n >= 7 && strncmp(s, "minmax(", 7) == 0) {
        const char *comma = memchr(s, ',', n);
        if (!comma) {
            return 0;
        }
        const char *b = comma + 1;
        size_t bn = (size_t)((s + n) - b);
        while (bn > 0 && (b[bn - 1] == ')' || b[bn - 1] == ' ')) {
            bn--;
        }
        return grid_parse_one_track(b, bn, out);
    }
    if (n >= 2 && s[n - 1] == 'r' && s[n - 2] == 'f') {
        char buf[32];
        grid_tok_copy(s, n - 2, buf, sizeof(buf));
        float v = (float)atof(buf);
        out->is_fr = 1;
        out->value = v > 0.0f ? v : 1.0f;
        return 1;
    }
    if ((n >= 4 && strncmp(s, "auto", 4) == 0) || (n >= 11 && strstr(s, "content") != NULL)) {
        out->is_fr = 1;
        out->value = 1.0f; /* approximate intrinsic tracks as 1fr */
        return 1;
    }
    {
        char buf[32];
        grid_tok_copy(s, n, buf, sizeof(buf));
        float v = (float)atof(buf);
        if (v <= 0.0f) {
            return 0;
        }
        out->is_fr = 0;
        out->value = v;
        return 1;
    }
}

/* Parse a full grid-template-columns value into tracks. Expands repeat(). */
static int grid_parse_tracks(const char *v, size_t n, struct yl_grid_track *out, int maxn)
{
    int count = 0;
    size_t i = 0;
    while (i < n && count < maxn) {
        while (i < n && (v[i] == ' ' || v[i] == '\t' || v[i] == ',')) {
            i++;
        }
        if (i >= n) {
            break;
        }
        if (i + 7 <= n && strncmp(v + i, "repeat(", 7) == 0) {
            i += 7;
            int rep = atoi(v + i);
            while (i < n && v[i] != ',') {
                i++;
            }
            if (i < n) {
                i++; /* comma */
            }
            while (i < n && v[i] == ' ') {
                i++;
            }
            size_t xstart = i;
            int depth = 1;
            while (i < n && depth > 0) {
                if (v[i] == '(') {
                    depth++;
                } else if (v[i] == ')') {
                    depth--;
                    if (depth == 0) {
                        break;
                    }
                }
                i++;
            }
            struct yl_grid_track t = {0};
            if (rep > 0 && grid_parse_one_track(v + xstart, i - xstart, &t)) {
                for (int k = 0; k < rep && count < maxn; k++) {
                    out[count++] = t;
                }
            }
            if (i < n) {
                i++; /* past ')' */
            }
        } else {
            size_t tstart = i;
            int depth = 0;
            while (i < n && (depth > 0 || (v[i] != ' ' && v[i] != ',' && v[i] != '\t'))) {
                if (v[i] == '(') {
                    depth++;
                } else if (v[i] == ')') {
                    depth--;
                }
                i++;
            }
            struct yl_grid_track t = {0};
            if (grid_parse_one_track(v + tstart, i - tstart, &t) && count < maxn) {
                out[count++] = t;
            }
        }
    }
    return count;
}

/* Read a px length declaration `prop:Npx` inside [block, block+blen). Returns
 * the px value, or -1 if absent. For two-value forms (`gap:R C`) `which`
 * selects 0=first(row) or 1=second(col). */
static float grid_find_len(const char *block, size_t blen, const char *prop, int which)
{
    size_t plen = strlen(prop);
    for (size_t i = 0; i + plen < blen; i++) {
        if ((i == 0 || block[i - 1] == ';' || block[i - 1] == '{' || block[i - 1] == ' ') &&
            strncmp(block + i, prop, plen) == 0 && block[i + plen] == ':') {
            size_t j = i + plen + 1;
            for (int idx = 0;; idx++) {
                while (j < blen && (block[j] == ' ' || block[j] == '\t')) {
                    j++;
                }
                char buf[32];
                size_t k = 0;
                while (j < blen && block[j] != ';' && block[j] != '}' && block[j] != ' ' &&
                       k < sizeof(buf) - 1) {
                    buf[k++] = block[j++];
                }
                buf[k] = '\0';
                if (idx == which || (which == 1 && (j >= blen || block[j] == ';' || block[j] == '}'))) {
                    return (float)atof(buf);
                }
                if (j >= blen || block[j] == ';' || block[j] == '}') {
                    return (float)atof(buf); /* only one value present */
                }
            }
        }
    }
    return -1.0f;
}

void yetty_ylexbor_css_scan_grid_templates(struct yetty_ylexbor *r, const char *src, size_t len)
{
    if (r == NULL || src == NULL || len < 12) {
        return;
    }
    static const char needle[] = "grid-template-columns:";
    const size_t nlen = sizeof(needle) - 1;
    for (size_t i = 0; i + nlen < len; i++) {
        if (memcmp(src + i, needle, nlen) != 0) {
            continue;
        }
        /* Find the enclosing rule's `{` (scan back) and its selector. */
        size_t brace = i;
        while (brace > 0 && src[brace] != '{') {
            brace--;
        }
        if (src[brace] != '{') {
            continue;
        }
        size_t sel_end = brace;
        size_t sel_start = brace;
        while (sel_start > 0 && src[sel_start - 1] != '}' && src[sel_start - 1] != '{' &&
               src[sel_start - 1] != ';') {
            sel_start--;
        }
        /* Selector must be a single `.classname` (no combinators/compound).
         * Skip leading whitespace and any CSS comment block — a comment right
         * before the rule would otherwise make the selector look like it
         * starts with a slash. */
        for (;;) {
            while (sel_start < sel_end &&
                   (src[sel_start] == ' ' || src[sel_start] == '\n' || src[sel_start] == '\t' ||
                    src[sel_start] == '\r')) {
                sel_start++;
            }
            if (sel_start + 1 < sel_end && src[sel_start] == '/' && src[sel_start + 1] == '*') {
                sel_start += 2;
                while (sel_start + 1 < sel_end &&
                       !(src[sel_start] == '*' && src[sel_start + 1] == '/')) {
                    sel_start++;
                }
                sel_start += 2;
                continue;
            }
            break;
        }
        if (sel_start >= sel_end || src[sel_start] != '.') {
            continue;
        }
        size_t cls_start = sel_start + 1;
        size_t cls_end = cls_start;
        while (cls_end < sel_end &&
               (isalnum((unsigned char)src[cls_end]) || src[cls_end] == '-' || src[cls_end] == '_')) {
            cls_end++;
        }
        /* Reject compound/descendant selectors (anything after the class). */
        {
            size_t t = cls_end;
            while (t < sel_end && (src[t] == ' ' || src[t] == '\n')) {
                t++;
            }
            if (t != sel_end) {
                continue;
            }
        }
        if (cls_end == cls_start) {
            continue;
        }
        /* Value of grid-template-columns. */
        size_t val_start = i + nlen;
        size_t val_end = val_start;
        while (val_end < len && src[val_end] != ';' && src[val_end] != '}') {
            val_end++;
        }
        struct yl_grid_track tracks[YL_GRID_MAX_TRACKS];
        int ntracks = grid_parse_tracks(src + val_start, val_end - val_start, tracks,
                                        YL_GRID_MAX_TRACKS);
        if (ntracks < 2) {
            continue; /* a single column is just a block — nothing to gain */
        }
        /* Gaps from the same block. */
        size_t block_end = brace + 1;
        int depth = 1;
        while (block_end < len && depth > 0) {
            if (src[block_end] == '{') {
                depth++;
            } else if (src[block_end] == '}') {
                depth--;
            }
            block_end++;
        }
        const char *block = src + brace;
        size_t blen = block_end - brace;
        float col_gap = grid_find_len(block, blen, "column-gap", 0);
        float row_gap = -1.0f;
        if (col_gap < 0.0f) {
            col_gap = grid_find_len(block, blen, "gap", 1);
            row_gap = grid_find_len(block, blen, "gap", 0);
        }
        if (col_gap < 0.0f) {
            col_gap = grid_find_len(block, blen, "grid-gap", 1);
            row_gap = grid_find_len(block, blen, "grid-gap", 0);
        }

        /* Store (grow the array; skip if this class is already recorded). */
        char cls[64];
        grid_tok_copy(src + cls_start, cls_end - cls_start, cls, sizeof(cls));
        int dup = 0;
        for (int e = 0; e < r->grid_class_count; e++) {
            if (strcmp(r->grid_classes[e].cls, cls) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        if (r->grid_class_count == r->grid_class_cap) {
            int cap = r->grid_class_cap ? r->grid_class_cap * 2 : 16;
            struct yl_grid_class *grown =
                realloc(r->grid_classes, (size_t)cap * sizeof(struct yl_grid_class));
            if (!grown) {
                return;
            }
            r->grid_classes = grown;
            r->grid_class_cap = cap;
        }
        struct yl_grid_class *entry = &r->grid_classes[r->grid_class_count];
        entry->cls = strdup(cls);
        if (!entry->cls) {
            return;
        }
        memcpy(entry->tracks, tracks, sizeof(tracks));
        entry->ntracks = (uint8_t)ntracks;
        entry->col_gap = col_gap > 0.0f ? col_gap : 0.0f;
        entry->row_gap = row_gap > 0.0f ? row_gap : 0.0f;
        r->grid_class_count++;
    }
}

void yetty_ylexbor_grid_classes_free(struct yetty_ylexbor *r)
{
    if (r == NULL || r->grid_classes == NULL) {
        return;
    }
    for (int e = 0; e < r->grid_class_count; e++) {
        free(r->grid_classes[e].cls);
    }
    free(r->grid_classes);
    r->grid_classes = NULL;
    r->grid_class_count = 0;
    r->grid_class_cap = 0;
}

const struct yl_grid_class *yetty_ylexbor_grid_class_lookup(struct yetty_ylexbor *r,
                                                            const char *class_attr, size_t class_len)
{
    if (r == NULL || class_attr == NULL || r->grid_class_count == 0) {
        return NULL;
    }
    size_t i = 0;
    while (i < class_len) {
        while (i < class_len && (class_attr[i] == ' ' || class_attr[i] == '\t')) {
            i++;
        }
        size_t start = i;
        while (i < class_len && class_attr[i] != ' ' && class_attr[i] != '\t') {
            i++;
        }
        size_t tlen = i - start;
        if (tlen == 0) {
            continue;
        }
        for (int e = 0; e < r->grid_class_count; e++) {
            const char *cls = r->grid_classes[e].cls;
            if (strlen(cls) == tlen && strncmp(cls, class_attr + start, tlen) == 0) {
                return &r->grid_classes[e];
            }
        }
    }
    return NULL;
}
