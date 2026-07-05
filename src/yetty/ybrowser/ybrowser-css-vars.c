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
    yetty_ylexbor_css_vars_reset_doc_classes(r);
}

/* Drop the cached document class set so the next scan rebuilds it from the
 * replacement DOM. Called on document load and from destroy. */
void yetty_ylexbor_css_vars_reset_doc_classes(struct yetty_ylexbor *r)
{
    for (int i = 0; i < r->doc_class_count; i++) {
        free(r->doc_classes[i]);
    }
    free(r->doc_classes);
    r->doc_classes = NULL;
    r->doc_class_count = 0;
    r->doc_classes_built = 0;
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
        while (
            j < len && k < sizeof(numbuf) - 1 &&
            (src[j] == '.' || src[j] == '+' || src[j] == '-' || (src[j] >= '0' && src[j] <= '9'))) {
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

static int css_var_is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int css_var_is_class_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_';
}

/* Compare for qsort/bsearch over the document class-name set. */
static int css_var_class_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Build (once) the sorted, de-duplicated set of every class token present in
 * the live DOM. Used to reject design tokens scoped under a class that isn't
 * on the page. Best-effort: on OOM it leaves the set empty, which makes the
 * scanner fall back to permissive capture. */
static void css_var_build_doc_classes(struct yetty_ylexbor *r)
{
    r->doc_classes_built = 1;
    if (!r->document) {
        return;
    }
    int cap = 0;
    lxb_dom_node_t *node = lxb_dom_interface_node(r->document);
    lxb_dom_node_t *root = node;
    while (node) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t *el = lxb_dom_interface_element(node);
            size_t alen = 0;
            const lxb_char_t *cv =
                lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &alen);
            if (cv && alen > 0) {
                const char *p = (const char *)cv;
                const char *end = p + alen;
                while (p < end) {
                    while (p < end && css_var_is_ws(*p)) {
                        p++;
                    }
                    const char *s = p;
                    while (p < end && !css_var_is_ws(*p)) {
                        p++;
                    }
                    if (p > s) {
                        if (r->doc_class_count == cap) {
                            int nc = cap ? cap * 2 : 64;
                            char **np = realloc(r->doc_classes, (size_t)nc * sizeof(char *));
                            if (!np) {
                                goto sort;
                            }
                            r->doc_classes = np;
                            cap = nc;
                        }
                        char *tok = strndup(s, (size_t)(p - s));
                        if (tok) {
                            r->doc_classes[r->doc_class_count++] = tok;
                        }
                    }
                }
            }
        }
        /* Pre-order descent: child, else sibling, else up-and-over. */
        if (node->first_child) {
            node = node->first_child;
        } else {
            while (node && node != root && !node->next) {
                node = node->parent;
            }
            if (!node || node == root) {
                break;
            }
            node = node->next;
        }
    }
sort:
    if (r->doc_class_count > 1) {
        qsort(r->doc_classes, (size_t)r->doc_class_count, sizeof(char *), css_var_class_cmp);
        int w = 1;
        for (int i = 1; i < r->doc_class_count; i++) {
            if (strcmp(r->doc_classes[i], r->doc_classes[w - 1]) != 0) {
                r->doc_classes[w++] = r->doc_classes[i];
            } else {
                free(r->doc_classes[i]);
            }
        }
        r->doc_class_count = w;
    }
}

/* True iff class token cls[0..clen) appears anywhere in the document. */
static int css_var_doc_has_class(struct yetty_ylexbor *r, const char *cls, size_t clen)
{
    if (!r->doc_classes_built) {
        css_var_build_doc_classes(r);
    }
    if (r->doc_class_count == 0 || clen == 0) {
        return 0;
    }
    int lo = 0;
    int hi = r->doc_class_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const char *m = r->doc_classes[mid];
        int c = strncmp(m, cls, clen);
        if (c == 0) {
            c = (m[clen] == '\0') ? 0 : 1; /* m longer than the token => greater */
        }
        if (c == 0) {
            return 1;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return 0;
}

/* Decide whether a rule's custom properties should be captured as global
 * design tokens. The guard: a rule is rejected iff its selector references a
 * `.class` token that does not appear anywhere in the document — i.e. it
 * belongs to an INACTIVE scope (typically a non-current theme, e.g. Google
 * News parks its dark palette under `.dm7YTc` / `body.dm7YTc`; capturing
 * `--gm3-sys-color-background:#131314` from those painted the fixed header
 * dark). Selectors with no class qualifier (`:root`, `html`, `body`, `*`,
 * `[data-theme=…]`) stay permissively global, as before. Class tokens inside
 * `[...]` attribute selectors and quoted strings are skipped. */
static int sel_is_global_target(struct yetty_ylexbor *r, const char *sel, size_t len)
{
    const char *p = sel;
    const char *end = sel + len;
    int in_attr = 0;
    char quote = 0;
    while (p < end) {
        char c = *p;
        if (quote) {
            if (c == quote) {
                quote = 0;
            }
            p++;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            p++;
            continue;
        }
        if (c == '[') {
            in_attr = 1;
            p++;
            continue;
        }
        if (c == ']') {
            in_attr = 0;
            p++;
            continue;
        }
        if (c == '.' && !in_attr) {
            p++;
            const char *s = p;
            while (p < end && css_var_is_class_char(*p)) {
                p++;
            }
            if (p > s && !css_var_doc_has_class(r, s, (size_t)(p - s))) {
                return 0; /* references a class absent from the document */
            }
            continue;
        }
        p++;
    }
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

        if (!sel_is_global_target(r, sel, sel_len)) {
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

/* Copy a CSS class-selector token, dropping backslash escapes so the stored
 * name matches the element's `class` attribute. Tailwind emits the selector
 * `.lg\:grid-cols-5` for the class token `lg:grid-cols-5`; the cascade matches
 * on the unescaped name, so the lookup table must store it unescaped too. */
static void grid_cls_copy_unescape(const char *s, size_t n, char *buf, size_t bufsz)
{
    size_t read = 0;
    size_t write = 0;
    while (read < n && write < bufsz - 1) {
        if (s[read] == '\\' && read + 1 < n) {
            read++; /* skip the backslash, keep the escaped char literally */
        }
        buf[write++] = s[read++];
    }
    buf[write] = '\0';
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
    if ((n == 4 && strncmp(s, "auto", 4) == 0) ||
        (n >= 11 && (strstr(s, "min-content") != NULL || strstr(s, "max-content") != NULL))) {
        out->is_fr = 0;
        out->is_auto = 1; /* sized to the column's max-content at layout time */
        out->value = 0.0f;
        return 1;
    }
    {
        /* Clean `<number>[px|em|rem]` only. atof() silently swallows garbage
		 * (`calc(...)`, `var(...)`, leftover named-line tokens), which let a
		 * mis-parsed sidebar track masquerade as a tiny ~16px column and
		 * collapsed the content auto-flowed into it (Wikipedia). Validate the
		 * whole token; reject anything else so the grid falls back to block. */
        size_t k = 0;
        int seen_digit = 0;
        while (k < n && (s[k] == '+' || s[k] == '-')) {
            k++;
        }
        while (k < n && ((s[k] >= '0' && s[k] <= '9') || s[k] == '.')) {
            if (s[k] >= '0' && s[k] <= '9') {
                seen_digit = 1;
            }
            k++;
        }
        size_t unit_len = n - k;
        int is_pct = (unit_len == 1 && s[k] == '%');
        int unit_ok = (unit_len == 0) || is_pct ||
                      (unit_len == 2 && strncmp(s + k, "px", 2) == 0) ||
                      (unit_len == 2 && strncmp(s + k, "em", 2) == 0) ||
                      (unit_len == 3 && strncmp(s + k, "rem", 3) == 0);
        if (!seen_digit || !unit_ok) {
            return 0;
        }
        char buf[32];
        grid_tok_copy(s, n, buf, sizeof(buf));
        float v = (float)atof(buf);
        if (unit_len == 2 && strncmp(s + k, "em", 2) == 0) {
            v *= 16.0f;
        } else if (unit_len == 3 && strncmp(s + k, "rem", 3) == 0) {
            v *= 16.0f;
        }
        /* Allow 0 (WPT uses `minmax(auto, 0px)` widely); only reject negative. */
        if (v < 0.0f) {
            return 0;
        }
        out->is_fr = 0;
        out->is_pct = (uint8_t)(is_pct ? 1 : 0);
        out->value = v;
        return 1;
    }
}

/* Parse a full grid-template-columns value into tracks. Expands repeat(). */
static int grid_parse_tracks_ex(const char *v, size_t n, struct yl_grid_track *out, int maxn,
                                int allow_named)
{
    /* Strip a trailing `!important` (Tailwind emits `…repeat(5,minmax(0,1fr))
	 * !important` for its grid utilities). The `!` cannot legitimately appear
	 * inside a track list, so cutting at the first `!` drops the priority
	 * marker; without this the marker parses as a bogus extra track and the
	 * whole template is rejected, collapsing the grid to block flow. */
    const char *bang = memchr(v, '!', n);
    if (bang != NULL) {
        n = (size_t)(bang - v);
    }
    /* Named grid lines (`[name]`) imply named-line / `grid-column:<name>`
	 * placement, which we don't model — auto-flowing the children would
	 * mis-place them into the wrong (often near-zero) track and collapse the
	 * content (Wikipedia's Vector shell nests such grids). Bail so the
	 * container falls back to block flow, where the content keeps full width
	 * and stays readable instead of wrapping one word per line. */
    if (!allow_named && memchr(v, '[', n) != NULL) {
        return 0;
    }
    int count = 0;
    size_t i = 0;
    while (i < n && count < maxn) {
        while (i < n && (v[i] == ' ' || v[i] == '\t' || v[i] == ',')) {
            i++;
        }
        if (i >= n) {
            break;
        }
        /* Skip a `[line-name ...]` token — it sits between tracks and does not
		 * consume a column. */
        if (v[i] == '[') {
            while (i < n && v[i] != ']') {
                i++;
            }
            if (i < n) {
                i++; /* past ']' */
            }
            continue;
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
            if (!grid_parse_one_track(v + tstart, i - tstart, &t)) {
                /* A track we can't cleanly resolve means the template is more
				 * complex than our simple model (calc/var/named/intrinsic).
				 * Reject the whole grid so it falls back to readable block
				 * flow rather than placing children into bogus tracks. */
                return 0;
            }
            if (count < maxn) {
                out[count++] = t;
            }
        }
    }
    return count;
}

/* Default track parse: reject named-line templates (the conservative path used
 * by the generic class/inline grid detection). */
static int grid_parse_tracks(const char *v, size_t n, struct yl_grid_track *out, int maxn)
{
    return grid_parse_tracks_ex(v, n, out, maxn, /*allow_named=*/0);
}

/* Resolve a grid line name to its line index within a grid-template-columns
 * value. Line index N sits before track N (so for a start-only
 * `grid-column:<name>` placement it is also the START TRACK index). Walks the
 * value counting tracks; each `[name ...]` token's names map to the current
 * track count. Returns the line index, or -1 if the name isn't found. */
int yetty_ylexbor_grid_resolve_line(const char *value, size_t n, const char *name, size_t name_len)
{
    if (value == NULL || name == NULL || name_len == 0) {
        return -1;
    }
    int line = 0; /* tracks seen so far == current line index */
    size_t i = 0;
    while (i < n) {
        while (i < n && (value[i] == ' ' || value[i] == '\t' || value[i] == ',')) {
            i++;
        }
        if (i >= n) {
            break;
        }
        if (value[i] == '[') {
            /* One or more whitespace-separated names for the current line. */
            i++;
            while (i < n && value[i] != ']') {
                while (i < n && (value[i] == ' ' || value[i] == '\t')) {
                    i++;
                }
                size_t t0 = i;
                while (i < n && value[i] != ' ' && value[i] != '\t' && value[i] != ']') {
                    i++;
                }
                size_t tlen = i - t0;
                if (tlen == name_len && strncmp(value + t0, name, name_len) == 0) {
                    return line;
                }
            }
            if (i < n) {
                i++; /* past ']' */
            }
            continue;
        }
        /* A track token (incl. repeat()/minmax()/func) — consume to the next
		 * top-level whitespace/comma and count it as one line advance. NB: a
		 * repeat(N, ...) counts as one here, which is fine for the single-name
		 * placements we resolve (named grids that also use repeat are rejected
		 * earlier). */
        int depth = 0;
        while (i < n && (depth > 0 || (value[i] != ' ' && value[i] != ',' && value[i] != '\t'))) {
            if (value[i] == '(') {
                depth++;
            } else if (value[i] == ')') {
                depth--;
            }
            i++;
        }
        line++;
    }
    return -1;
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
                if (idx == which ||
                    (which == 1 && (j >= blen || block[j] == ';' || block[j] == '}'))) {
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

/* Evaluate a single `@media` condition (the text between `@media` and `{`)
 * against the live viewport width. We model the width-based breakpoint idiom
 * only (`min-width` / `max-width`, possibly several ANDed, with leading
 * `screen and`); unknown features are treated as matching so a template is
 * never wrongly dropped. This exists because the grid-template scanner is a
 * substring pass with no notion of the cascade — without it, Tailwind's
 * `xl:grid-cols-2` (inside `@media (min-width:1280px)`) registers and applies
 * at a 1200px viewport, splitting a single-column block into two columns. */
static bool grid_media_condition_matches(const char *cond, size_t n, float viewport_w)
{
    bool ok = true;
    size_t i = 0;
    while (i < n) {
        if (i + 9 <= n && strncmp(cond + i, "min-width", 9) == 0) {
            size_t j = i + 9;
            while (j < n && (cond[j] == ':' || cond[j] == ' ')) {
                j++;
            }
            float v = (float)atof(cond + j);
            if (viewport_w + 0.5f < v) {
                ok = false;
            }
            i = j;
        } else if (i + 9 <= n && strncmp(cond + i, "max-width", 9) == 0) {
            size_t j = i + 9;
            while (j < n && (cond[j] == ':' || cond[j] == ' ')) {
                j++;
            }
            float v = (float)atof(cond + j);
            if (viewport_w - 0.5f > v) {
                ok = false;
            }
            i = j;
        } else {
            i++;
        }
    }
    return ok;
}

/* True if byte offset `pos` in `src` is reached through only matching `@media`
 * blocks (or none). A grid template nested in a non-matching media query must
 * not be registered. Forward single pass tracking brace nesting and, for each
 * open media block, whether its condition matches the viewport. */
static bool grid_media_active_at(const char *src, size_t len, size_t pos, float viewport_w)
{
    enum { MEDIA_STACK_MAX = 16 };
    bool media_match[MEDIA_STACK_MAX];
    int media_open_brace_depth[MEDIA_STACK_MAX];
    int media_depth = 0;
    int brace_depth = 0;

    size_t i = 0;
    while (i < pos && i < len) {
        if (src[i] == '@' && i + 6 <= len && strncmp(src + i, "@media", 6) == 0) {
            size_t cond_start = i + 6;
            size_t cursor = cond_start;
            while (cursor < len && src[cursor] != '{') {
                cursor++;
            }
            if (cursor < len) {
                bool matches =
                    grid_media_condition_matches(src + cond_start, cursor - cond_start, viewport_w);
                if (media_depth < MEDIA_STACK_MAX) {
                    media_match[media_depth] = matches;
                    media_open_brace_depth[media_depth] = brace_depth;
                    media_depth++;
                }
                brace_depth++; /* the media block's own `{` */
                i = cursor + 1;
                continue;
            }
        }
        if (src[i] == '{') {
            brace_depth++;
        } else if (src[i] == '}') {
            brace_depth--;
            if (media_depth > 0 && brace_depth == media_open_brace_depth[media_depth - 1]) {
                media_depth--;
            }
        }
        i++;
    }
    for (int m = 0; m < media_depth; m++) {
        if (!media_match[m]) {
            return false;
        }
    }
    return true;
}

static int grid_selector_alternative_classes(const char *src, size_t alt_start, size_t alt_end,
                                             char target[64],
                                             char context[YL_GRID_SPAN_CONTEXT_MAX][64],
                                             int *out_context_count);

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
        /* Skip templates inside a non-matching @media block (e.g. Tailwind's
         * `xl:` utilities behind `@media (min-width:1280px)` at a narrower
         * viewport). */
        if (!grid_media_active_at(src, len, i, (float)r->viewport_w)) {
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
        /* Value of grid-template-columns. */
        size_t val_start = i + nlen;
        size_t val_end = val_start;
        while (val_end < len && src[val_end] != ';' && src[val_end] != '}') {
            val_end++;
        }
        struct yl_grid_track tracks[YL_GRID_MAX_TRACKS];
        int ntracks =
            grid_parse_tracks(src + val_start, val_end - val_start, tracks, YL_GRID_MAX_TRACKS);
        /* `grid-template-columns: inherit` (subgrid idiom): register an
         * inherit entry — box-build copies the parent's tracks. */
        int inherit_template = 0;
        if (ntracks < 2) {
            size_t v = val_start;
            while (v < val_end && (src[v] == ' ' || src[v] == '\t')) {
                v++;
            }
            if (v + 7 <= val_end && strncasecmp(src + v, "inherit", 7) == 0) {
                inherit_template = 1;
                ntracks = 0;
            } else {
                continue; /* a single column is just a block — nothing to gain */
            }
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

        /* One entry per comma-alternative of the selector, keyed by the
         * last class of its final compound; the selector's other classes
         * become ancestor-context requirements (the gnews card template
         * lives on a compound: `.MQsxIb.q4atFc{…:1fr 16px auto}`). */
        size_t alt_start = sel_start;
        while (alt_start < sel_end) {
            size_t alt_end = alt_start;
            while (alt_end < sel_end && src[alt_end] != ',') {
                alt_end++;
            }
            char cls[64];
            char context[YL_GRID_SPAN_CONTEXT_MAX][64];
            int context_count = 0;
            if (!grid_selector_alternative_classes(src, alt_start, alt_end, cls, context,
                                                   &context_count)) {
                alt_start = alt_end + 1;
                continue;
            }
            int dup = 0;
            for (int e = 0; e < r->grid_class_count; e++) {
                const struct yl_grid_class *existing = &r->grid_classes[e];
                if (strcmp(existing->cls, cls) != 0 || existing->context_count != context_count) {
                    continue;
                }
                int same = 1;
                for (int k = 0; k < context_count; k++) {
                    if (strcmp(existing->context[k], context[k]) != 0) {
                        same = 0;
                        break;
                    }
                }
                if (same) {
                    dup = 1;
                    break;
                }
            }
            if (dup) {
                alt_start = alt_end + 1;
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
            entry->context_count = 0;
            for (int k = 0; k < context_count; k++) {
                entry->context[k] = strdup(context[k]);
                if (entry->context[k] == NULL) {
                    for (int f = 0; f < k; f++) {
                        free(entry->context[f]);
                    }
                    free(entry->cls);
                    return;
                }
                entry->context_count++;
            }
            memcpy(entry->tracks, tracks, sizeof(tracks));
            entry->ntracks = (uint8_t)ntracks;
            entry->inherit_template = (uint8_t)inherit_template;
            entry->col_gap = col_gap > 0.0f ? col_gap : 0.0f;
            entry->row_gap = row_gap > 0.0f ? row_gap : 0.0f;
            r->grid_class_count++;
            alt_start = alt_end + 1;
        }
    }
}

/* Parse a `grid-template-columns` declaration out of an inline `style`
 * attribute into `out` (up to maxn tracks); also reads the column/row gap
 * (`column-gap`/`row-gap`/`gap`). Returns the track count (0 if none). Lets
 * box-build treat `style="display:grid;grid-template-columns:200px 1fr"` as a
 * real grid — the class scanner only sees author-stylesheet rules, not inline
 * styles. */
int yetty_ylexbor_grid_parse_inline(const char *style, size_t len, struct yl_grid_track *out,
                                    int maxn, float *col_gap, float *row_gap)
{
    if (col_gap) {
        *col_gap = 0.0f;
    }
    if (row_gap) {
        *row_gap = 0.0f;
    }
    if (style == NULL || out == NULL || len < 12) {
        return 0;
    }
    static const char needle[] = "grid-template-columns:";
    const size_t nlen = sizeof(needle) - 1;
    for (size_t i = 0; i + nlen <= len; i++) {
        if (memcmp(style + i, needle, nlen) != 0) {
            continue;
        }
        size_t value_start = i + nlen;
        size_t value_end = value_start;
        while (value_end < len && style[value_end] != ';') {
            value_end++;
        }
        int ntracks = grid_parse_tracks(style + value_start, value_end - value_start, out, maxn);
        if (col_gap) {
            float g = grid_find_len(style, len, "column-gap", 0);
            if (g < 0.0f) {
                g = grid_find_len(style, len, "gap", 0);
            }
            if (g > 0.0f) {
                *col_gap = g;
            }
        }
        if (row_gap) {
            float g = grid_find_len(style, len, "row-gap", 0);
            if (g < 0.0f) {
                g = grid_find_len(style, len, "gap", 0);
            }
            if (g > 0.0f) {
                *row_gap = g;
            }
        }
        return ntracks;
    }
    return 0;
}

/* Like yetty_ylexbor_grid_parse_inline, but ALLOWS named-line templates
 * (`[name] 200px [c-start] 1fr [c-end]`). On success returns the track count
 * (named lines skipped) and, via *out_value / *out_value_len, the substring of
 * `style` holding the grid-template-columns value so the caller can stash it
 * (arena-dup) for later `grid-column:<name>` line resolution. Used only on the
 * explicit named-placement path. */
int yetty_ylexbor_grid_parse_inline_named(const char *style, size_t len, struct yl_grid_track *out,
                                          int maxn, float *col_gap, float *row_gap,
                                          const char **out_value, size_t *out_value_len)
{
    if (col_gap) {
        *col_gap = 0.0f;
    }
    if (row_gap) {
        *row_gap = 0.0f;
    }
    if (out_value) {
        *out_value = NULL;
    }
    if (out_value_len) {
        *out_value_len = 0;
    }
    if (style == NULL || out == NULL || len < 12) {
        return 0;
    }
    static const char needle[] = "grid-template-columns:";
    const size_t nlen = sizeof(needle) - 1;
    for (size_t i = 0; i + nlen <= len; i++) {
        if (memcmp(style + i, needle, nlen) != 0) {
            continue;
        }
        size_t value_start = i + nlen;
        size_t value_end = value_start;
        while (value_end < len && style[value_end] != ';') {
            value_end++;
        }
        int ntracks = grid_parse_tracks_ex(style + value_start, value_end - value_start, out, maxn,
                                           /*allow_named=*/1);
        if (out_value) {
            *out_value = style + value_start;
        }
        if (out_value_len) {
            *out_value_len = value_end - value_start;
        }
        if (col_gap) {
            float g = grid_find_len(style, len, "column-gap", 0);
            if (g < 0.0f) {
                g = grid_find_len(style, len, "gap", 0);
            }
            if (g > 0.0f) {
                *col_gap = g;
            }
        }
        if (row_gap) {
            float g = grid_find_len(style, len, "row-gap", 0);
            if (g < 0.0f) {
                g = grid_find_len(style, len, "gap", 0);
            }
            if (g > 0.0f) {
                *row_gap = g;
            }
        }
        return ntracks;
    }
    return 0;
}

/* Column gap (px) declared in an inline `style` attribute via `gap`,
 * `column-gap`, or `grid-column-gap`. Returns -1 when none is present. Used
 * for flex/grid containers whose gap is set inline (libcss in this tree only
 * models the legacy multicol column-gap, not the modern `gap` shorthand). */
float yetty_ylexbor_css_inline_gap(const char *style, size_t len)
{
    if (style == NULL || len == 0) {
        return -1.0f;
    }
    float g = grid_find_len(style, len, "column-gap", 0);
    if (g < 0.0f) {
        g = grid_find_len(style, len, "gap", 0);
    }
    if (g < 0.0f) {
        g = grid_find_len(style, len, "grid-column-gap", 0);
    }
    return g;
}

void yetty_ylexbor_grid_classes_free(struct yetty_ylexbor *r)
{
    if (r == NULL) {
        return;
    }
    for (int e = 0; e < r->grid_class_count; e++) {
        free(r->grid_classes[e].cls);
        for (int k = 0; k < r->grid_classes[e].context_count; k++) {
            free(r->grid_classes[e].context[k]);
        }
    }
    free(r->grid_classes);
    r->grid_classes = NULL;
    r->grid_class_count = 0;
    r->grid_class_cap = 0;
    for (int e = 0; e < r->grid_span_class_count; e++) {
        free(r->grid_span_classes[e].cls);
        for (int k = 0; k < r->grid_span_classes[e].context_count; k++) {
            free(r->grid_span_classes[e].context[k]);
        }
    }
    free(r->grid_span_classes);
    r->grid_span_classes = NULL;
    r->grid_span_class_count = 0;
    r->grid_span_class_cap = 0;
    for (int e = 0; e < r->flex_gap_class_count; e++) {
        free(r->flex_gap_classes[e].key);
    }
    free(r->flex_gap_classes);
    r->flex_gap_classes = NULL;
    r->flex_gap_class_count = 0;
    r->flex_gap_class_cap = 0;
}

static int class_attr_has_token(const char *attr, size_t attr_len, const char *cls);
static int element_or_ancestor_has_class(const lxb_dom_element_t *element, const char *cls);

const struct yl_grid_class *yetty_ylexbor_grid_class_lookup(struct yetty_ylexbor *r,
                                                            const lxb_dom_element_t *element)
{
    if (r == NULL || element == NULL || r->grid_class_count == 0) {
        return NULL;
    }
    size_t attr_len = 0;
    const lxb_char_t *attr = lxb_dom_element_get_attribute(
        (lxb_dom_element_t *)element, (const lxb_char_t *)"class", 5, &attr_len);
    if (attr == NULL || attr_len == 0) {
        return NULL;
    }
    const struct yl_grid_class *found = NULL;
    for (int e = 0; e < r->grid_class_count; e++) {
        const struct yl_grid_class *entry = &r->grid_classes[e];
        if (!class_attr_has_token((const char *)attr, attr_len, entry->cls)) {
            continue;
        }
        int context_ok = 1;
        for (int k = 0; k < entry->context_count; k++) {
            if (!element_or_ancestor_has_class(element, entry->context[k])) {
                context_ok = 0;
                break;
            }
        }
        if (context_ok) {
            /* Keep scanning: entries are in stylesheet order and the last
             * matching rule wins (cascade approximation). */
            found = entry;
        }
    }
    return found;
}

/* Extract the class structure of ONE comma-alternative of a selector
 * ([alt_start, alt_end) in `src`): `target` gets the last class of the
 * final compound; every other class in the alternative lands in
 * `context`. Pseudo-classes and attribute selectors are skipped. Returns
 * 0 when the alternative can't be keyed by a class (final compound is a
 * bare tag / `*`, malformed, or oversized). */
static int grid_selector_alternative_classes(const char *src, size_t alt_start, size_t alt_end,
                                             char target[64],
                                             char context[YL_GRID_SPAN_CONTEXT_MAX][64],
                                             int *out_context_count)
{
    enum { CLASS_TOKEN_MAX = 8 };
    struct {
        size_t start;
        size_t end;
        int compound;
    } tokens[CLASS_TOKEN_MAX];
    int token_count = 0;
    int compound_ordinal = -1; /* increments when a new compound opens */
    bool compound_open = false;

    size_t pos = alt_start;
    while (pos < alt_end) {
        unsigned char ch = (unsigned char)src[pos];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '>' || ch == '+' ||
            ch == '~') {
            compound_open = false;
            pos++;
            continue;
        }
        if (!compound_open) {
            compound_ordinal++;
            compound_open = true;
        }
        if (ch == '.') {
            pos++;
            size_t tok_start = pos;
            while (pos < alt_end) {
                unsigned char tc = (unsigned char)src[pos];
                if (tc == '\\' && pos + 1 < alt_end) {
                    pos += 2;
                } else if (isalnum(tc) || tc == '-' || tc == '_') {
                    pos++;
                } else {
                    break;
                }
            }
            if (pos == tok_start || token_count == CLASS_TOKEN_MAX) {
                return 0; /* malformed / oversized selector — skip it */
            }
            tokens[token_count].start = tok_start;
            tokens[token_count].end = pos;
            tokens[token_count].compound = compound_ordinal;
            token_count++;
            continue;
        }
        if (ch == '[') {
            while (pos < alt_end && src[pos] != ']') {
                pos++;
            }
            pos++;
            continue;
        }
        if (ch == ':') {
            pos++;
            while (pos < alt_end &&
                   (isalnum((unsigned char)src[pos]) || src[pos] == '-' || src[pos] == ':')) {
                pos++;
            }
            if (pos < alt_end && src[pos] == '(') {
                int depth = 0;
                while (pos < alt_end) {
                    if (src[pos] == '(') {
                        depth++;
                    } else if (src[pos] == ')' && --depth == 0) {
                        pos++;
                        break;
                    }
                    pos++;
                }
            }
            continue;
        }
        /* Tag name / universal / anything else — part of the compound but
         * not a class token. */
        pos++;
    }
    /* The final compound must contribute at least one class (reject
     * `.foo > div`); the last class token overall is the target,
     * everything else is context. */
    if (token_count == 0 || tokens[token_count - 1].compound != compound_ordinal) {
        return 0;
    }
    int target_token = token_count - 1;
    grid_cls_copy_unescape(src + tokens[target_token].start,
                           tokens[target_token].end - tokens[target_token].start, target, 64);
    int context_count = 0;
    for (int t = 0; t < token_count - 1 && context_count < YL_GRID_SPAN_CONTEXT_MAX; t++) {
        grid_cls_copy_unescape(src + tokens[t].start, tokens[t].end - tokens[t].start,
                               context[context_count], 64);
        context_count++;
    }
    *out_context_count = context_count;
    return 1;
}

/* Parse ONE comma-alternative of a `grid-column`/`grid-row` rule's
 * selector and append a placement entry keyed by the last class of the
 * final compound, with every other class stored as an ancestor-context
 * requirement. */
static void grid_span_scan_alternative(struct yetty_ylexbor *r, const char *src, size_t alt_start,
                                       size_t alt_end, int axis, int start, int span)
{
    char target[64];
    char context[YL_GRID_SPAN_CONTEXT_MAX][64];
    int context_count = 0;
    if (!grid_selector_alternative_classes(src, alt_start, alt_end, target, context,
                                           &context_count)) {
        return;
    }

    /* Dedupe identical entries (re-scans of the same sheet). */
    for (int e = 0; e < r->grid_span_class_count; e++) {
        const struct yl_grid_span_class *existing = &r->grid_span_classes[e];
        if (existing->axis != (uint8_t)axis || existing->start != (uint8_t)start ||
            existing->span != (uint8_t)span || strcmp(existing->cls, target) != 0 ||
            existing->context_count != context_count) {
            continue;
        }
        int same = 1;
        for (int k = 0; k < context_count; k++) {
            if (strcmp(existing->context[k], context[k]) != 0) {
                same = 0;
                break;
            }
        }
        if (same) {
            return;
        }
    }

    if (r->grid_span_class_count == r->grid_span_class_cap) {
        int cap = r->grid_span_class_cap ? r->grid_span_class_cap * 2 : 8;
        struct yl_grid_span_class *grown =
            realloc(r->grid_span_classes, (size_t)cap * sizeof(struct yl_grid_span_class));
        if (grown == NULL) {
            return;
        }
        r->grid_span_classes = grown;
        r->grid_span_class_cap = cap;
    }
    struct yl_grid_span_class *entry = &r->grid_span_classes[r->grid_span_class_count];
    entry->cls = strdup(target);
    if (entry->cls == NULL) {
        return;
    }
    entry->context_count = 0;
    for (int k = 0; k < context_count; k++) {
        entry->context[k] = strdup(context[k]);
        if (entry->context[k] == NULL) {
            for (int f = 0; f < k; f++) {
                free(entry->context[f]);
            }
            free(entry->cls);
            return;
        }
        entry->context_count++;
    }
    entry->axis = (uint8_t)axis;
    entry->start = (uint8_t)start;
    entry->span = (uint8_t)span;
    r->grid_span_class_count++;
}

int yetty_ylexbor_grid_parse_placement(const char *src, size_t val_start, size_t val_end,
                                       int *out_start, int *out_span)
{
    size_t p = val_start;
    while (p < val_end && (src[p] == ' ' || src[p] == '\t')) {
        p++;
    }
    int start = 0;
    int span = 0;
    if (p < val_end && src[p] >= '1' && src[p] <= '9') {
        start = atoi(src + p);
        while (p < val_end && src[p] >= '0' && src[p] <= '9') {
            p++;
        }
    } else if (p + 4 <= val_end && strncasecmp(src + p, "auto", 4) == 0) {
        p += 4;
    } else if (!(p + 4 <= val_end && strncasecmp(src + p, "span", 4) == 0)) {
        return 0; /* named line / negative index — not modelled */
    }
    while (p < val_end && (src[p] == ' ' || src[p] == '\t')) {
        p++;
    }
    if (p < val_end && src[p] == '/') {
        p++;
        while (p < val_end && (src[p] == ' ' || src[p] == '\t')) {
            p++;
        }
    }
    if (p + 4 <= val_end && strncasecmp(src + p, "span", 4) == 0) {
        p += 4;
        while (p < val_end && (src[p] == ' ' || src[p] == '\t')) {
            p++;
        }
        if (p < val_end && src[p] >= '0' && src[p] <= '9') {
            span = atoi(src + p);
        }
    } else if (p < val_end && src[p] >= '1' && src[p] <= '9') {
        int end_line = atoi(src + p);
        span = end_line > start ? end_line - start : 1;
    } else if (start > 0) {
        span = 1; /* bare `A` */
    }
    if (span < 1) {
        return 0;
    }
    *out_start = start;
    *out_span = span;
    return 1;
}

void yetty_ylexbor_css_scan_grid_spans(struct yetty_ylexbor *r, const char *src, size_t len)
{
    if (r == NULL || src == NULL || len < 12) {
        return;
    }
    static const char column_needle[] = "grid-column:";
    static const char row_needle[] = "grid-row:";
    const size_t column_nlen = sizeof(column_needle) - 1;
    const size_t row_nlen = sizeof(row_needle) - 1;
    for (size_t i = 0; i + row_nlen < len; i++) {
        int axis;
        size_t nlen;
        if (i + column_nlen < len && memcmp(src + i, column_needle, column_nlen) == 0) {
            axis = 0;
            nlen = column_nlen;
        } else if (memcmp(src + i, row_needle, row_nlen) == 0) {
            axis = 1;
            nlen = row_nlen;
        } else {
            continue;
        }
        if (!grid_media_active_at(src, len, i, (float)r->viewport_w)) {
            continue;
        }
        size_t val_start = i + nlen;
        size_t val_end = val_start;
        while (val_end < len && src[val_end] != ';' && src[val_end] != '}') {
            val_end++;
        }
        int start = 0;
        int span = 0;
        if (!yetty_ylexbor_grid_parse_placement(src, val_start, val_end, &start, &span)) {
            continue;
        }
        if (span > YL_GRID_MAX_TRACKS || start > 255) {
            continue;
        }
        /* Enclosing rule's selector list. Comma alternatives and
         * descendant/child chains are accepted: each alternative is keyed
         * by the last class of its final compound; every other class in
         * the alternative becomes an ancestor-context requirement. */
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
        size_t alt_start = sel_start;
        while (alt_start < sel_end) {
            size_t alt_end = alt_start;
            while (alt_end < sel_end && src[alt_end] != ',') {
                alt_end++;
            }
            grid_span_scan_alternative(r, src, alt_start, alt_end, axis, start, span);
            alt_start = alt_end + 1;
        }
    }
}

/* True when `cls` appears as a whole token in a space-separated class
 * attribute value. */
static int class_attr_has_token(const char *attr, size_t attr_len, const char *cls)
{
    size_t cls_len = strlen(cls);
    size_t i = 0;
    while (i < attr_len) {
        while (i < attr_len && (attr[i] == ' ' || attr[i] == '\t')) {
            i++;
        }
        size_t start = i;
        while (i < attr_len && attr[i] != ' ' && attr[i] != '\t') {
            i++;
        }
        if (i - start == cls_len && strncmp(attr + start, cls, cls_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* True when the element or one of its DOM ancestors carries class `cls`. */
static int element_or_ancestor_has_class(const lxb_dom_element_t *element, const char *cls)
{
    for (const lxb_dom_node_t *node = lxb_dom_interface_node(element); node != NULL;
         node = node->parent) {
        if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }
        size_t attr_len = 0;
        const lxb_char_t *attr =
            lxb_dom_element_get_attribute(lxb_dom_interface_element((lxb_dom_node_t *)node),
                                          (const lxb_char_t *)"class", 5, &attr_len);
        if (attr != NULL && class_attr_has_token((const char *)attr, attr_len, cls)) {
            return 1;
        }
    }
    return 0;
}

struct yl_grid_placement yetty_ylexbor_grid_span_class_lookup(struct yetty_ylexbor *r,
                                                              const lxb_dom_element_t *element)
{
    struct yl_grid_placement placement = {0};
    if (r == NULL || element == NULL || r->grid_span_class_count == 0) {
        return placement;
    }
    size_t attr_len = 0;
    const lxb_char_t *attr = lxb_dom_element_get_attribute(
        (lxb_dom_element_t *)element, (const lxb_char_t *)"class", 5, &attr_len);
    if (attr == NULL || attr_len == 0) {
        return placement;
    }
    for (int e = 0; e < r->grid_span_class_count; e++) {
        const struct yl_grid_span_class *entry = &r->grid_span_classes[e];
        if (!class_attr_has_token((const char *)attr, attr_len, entry->cls)) {
            continue;
        }
        int context_ok = 1;
        for (int k = 0; k < entry->context_count; k++) {
            if (!element_or_ancestor_has_class(element, entry->context[k])) {
                context_ok = 0;
                break;
            }
        }
        if (!context_ok) {
            continue;
        }
        /* Keep scanning: entries are in stylesheet order and the last
         * matching declaration per axis wins (cascade approximation). */
        if (entry->axis == 0) {
            placement.col_start = entry->start;
            placement.col_span = entry->span;
        } else {
            placement.row_start = entry->start;
            placement.row_span = entry->span;
        }
    }
    return placement;
}

void yetty_ylexbor_css_scan_flex_gaps(struct yetty_ylexbor *r, const char *src, size_t len)
{
    if (r == NULL || src == NULL || len < 16) {
        return;
    }
    static const char needle[] = "display";
    const size_t nlen = sizeof(needle) - 1;
    for (size_t i = 0; i + nlen < len; i++) {
        if (memcmp(src + i, needle, nlen) != 0) {
            continue;
        }
        /* `display : flex` (any spacing); also matches inline-flex via the
         * substring — both lay out as our flex row. */
        size_t p = i + nlen;
        while (p < len && (src[p] == ' ' || src[p] == '\t')) {
            p++;
        }
        if (p >= len || src[p] != ':') {
            continue;
        }
        p++;
        size_t val_end = p;
        while (val_end < len && src[val_end] != ';' && src[val_end] != '}') {
            val_end++;
        }
        int is_flex = 0;
        for (size_t k = p; k + 4 <= val_end; k++) {
            if (strncasecmp(src + k, "flex", 4) == 0) {
                is_flex = 1;
                break;
            }
        }
        if (!is_flex) {
            continue;
        }
        if (!grid_media_active_at(src, len, i, (float)r->viewport_w)) {
            continue;
        }
        /* Gap from the same block. */
        size_t brace = i;
        while (brace > 0 && src[brace] != '{') {
            brace--;
        }
        if (src[brace] != '{') {
            continue;
        }
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
        if (col_gap < 0.0f) {
            col_gap = grid_find_len(block, blen, "gap", 1);
        }
        if (col_gap < 0.0f) {
            col_gap = grid_find_len(block, blen, "gap", 0);
        }
        if (col_gap <= 0.0f) {
            continue;
        }
        /* Selector: take the LAST simple selector — a class chain keyed by
         * its last class, or a bare tag name (`header nav` → nav). */
        size_t sel_end = brace;
        size_t sel_start = brace;
        while (sel_start > 0 && src[sel_start - 1] != '}' && src[sel_start - 1] != '{' &&
               src[sel_start - 1] != ';') {
            sel_start--;
        }
        /* Trim trailing whitespace, then scan back to the last whitespace to
         * isolate the final simple selector. */
        while (sel_end > sel_start && (src[sel_end - 1] == ' ' || src[sel_end - 1] == '\n' ||
                                       src[sel_end - 1] == '\t' || src[sel_end - 1] == '\r')) {
            sel_end--;
        }
        size_t simple_start = sel_end;
        while (simple_start > sel_start) {
            char prev = src[simple_start - 1];
            if (prev == ' ' || prev == '\n' || prev == '\t' || prev == '\r' || prev == '>' ||
                prev == '+' || prev == '~' || prev == ',') {
                break;
            }
            simple_start--;
        }
        if (simple_start >= sel_end) {
            continue;
        }
        char key[64];
        uint8_t match_tag = 0;
        if (src[simple_start] == '.') {
            /* Class chain — last class wins. */
            size_t last_cls_start = 0, last_cls_end = 0;
            size_t pos = simple_start;
            while (pos < sel_end && src[pos] == '.') {
                size_t cls_start = pos + 1;
                size_t cls_end = cls_start;
                while (cls_end < sel_end) {
                    unsigned char ch = (unsigned char)src[cls_end];
                    if (ch == '\\' && cls_end + 1 < sel_end) {
                        cls_end += 2;
                    } else if (isalnum(ch) || ch == '-' || ch == '_') {
                        cls_end++;
                    } else {
                        break;
                    }
                }
                if (cls_end == cls_start) {
                    break;
                }
                last_cls_start = cls_start;
                last_cls_end = cls_end;
                pos = cls_end;
            }
            if (last_cls_end == 0 || pos != sel_end) {
                continue;
            }
            grid_cls_copy_unescape(src + last_cls_start, last_cls_end - last_cls_start, key,
                                   sizeof(key));
        } else {
            /* Bare tag name only (nav, header, ul, …) — no #id, :pseudo,
             * [attr] complexity. */
            size_t t = simple_start;
            while (t < sel_end && isalnum((unsigned char)src[t])) {
                t++;
            }
            if (t != sel_end || sel_end - simple_start >= sizeof(key)) {
                continue;
            }
            memcpy(key, src + simple_start, sel_end - simple_start);
            key[sel_end - simple_start] = '\0';
            match_tag = 1;
        }
        int dup = 0;
        for (int e = 0; e < r->flex_gap_class_count; e++) {
            if (r->flex_gap_classes[e].match_tag == match_tag &&
                strcmp(r->flex_gap_classes[e].key, key) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        if (r->flex_gap_class_count == r->flex_gap_class_cap) {
            int cap = r->flex_gap_class_cap ? r->flex_gap_class_cap * 2 : 8;
            struct yl_flex_gap_class *grown =
                realloc(r->flex_gap_classes, (size_t)cap * sizeof(struct yl_flex_gap_class));
            if (!grown) {
                return;
            }
            r->flex_gap_classes = grown;
            r->flex_gap_class_cap = cap;
        }
        struct yl_flex_gap_class *entry = &r->flex_gap_classes[r->flex_gap_class_count];
        entry->key = strdup(key);
        if (!entry->key) {
            return;
        }
        entry->match_tag = match_tag;
        entry->col_gap = col_gap;
        r->flex_gap_class_count++;
    }
}

float yetty_ylexbor_flex_gap_lookup(struct yetty_ylexbor *r, const char *class_attr,
                                    size_t class_len, const char *tag_name, size_t tag_len)
{
    if (r == NULL || r->flex_gap_class_count == 0) {
        return 0.0f;
    }
    if (class_attr != NULL) {
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
            for (int e = 0; e < r->flex_gap_class_count; e++) {
                const struct yl_flex_gap_class *entry = &r->flex_gap_classes[e];
                if (!entry->match_tag && strlen(entry->key) == tlen &&
                    strncmp(entry->key, class_attr + start, tlen) == 0) {
                    return entry->col_gap;
                }
            }
        }
    }
    if (tag_name != NULL && tag_len > 0) {
        for (int e = 0; e < r->flex_gap_class_count; e++) {
            const struct yl_flex_gap_class *entry = &r->flex_gap_classes[e];
            if (entry->match_tag && strlen(entry->key) == tag_len &&
                strncasecmp(entry->key, tag_name, tag_len) == 0) {
                return entry->col_gap;
            }
        }
    }
    return 0.0f;
}

/*===========================================================================
 * `flex` shorthand expansion — libcss parses the longhands only.
 *=========================================================================*/

struct flex_expand_out {
    char *data;
    size_t size, cap;
};

static int flex_expand_append(struct flex_expand_out *out, const char *bytes, size_t count)
{
    if (out->size + count + 1 > out->cap) {
        size_t new_cap = out->cap ? out->cap * 2 : 4096;
        while (new_cap < out->size + count + 1) {
            new_cap *= 2;
        }
        char *grown = realloc(out->data, new_cap);
        if (!grown) {
            return -1;
        }
        out->data = grown;
        out->cap = new_cap;
    }
    memcpy(out->data + out->size, bytes, count);
    out->size += count;
    return 0;
}

/* A bare CSS <number> — digits with an optional sign / decimal point and
 * no unit. Distinguishes grow/shrink factors from a flex-basis length. */
static int flex_token_is_number(const char *token, size_t token_len)
{
    size_t k = 0;
    int digits = 0;
    if (k < token_len && (token[k] == '+' || token[k] == '-')) {
        k++;
    }
    for (; k < token_len; k++) {
        if (token[k] >= '0' && token[k] <= '9') {
            digits = 1;
            continue;
        }
        if (token[k] == '.') {
            continue;
        }
        return 0;
    }
    return digits;
}

char *yetty_ylexbor_css_expand_flex(const char *src, size_t len, size_t *out_len)
{
    if (src == NULL || len < 6 || out_len == NULL) {
        return NULL;
    }
    struct flex_expand_out out = {0};
    size_t copied = 0;
    int changed = 0;
    for (size_t i = 0; i + 5 <= len; i++) {
        if (strncasecmp(src + i, "flex", 4) != 0) {
            continue;
        }
        /* Property position: the previous non-whitespace char must open a
         * declaration ('{' or ';'). Excludes `display:flex` (previous is
         * ':'), `.flex` selectors ('.'), and `-webkit-flex` ('-'). */
        int at_decl_start = 1;
        for (size_t back = i; back > 0;) {
            char prev = src[back - 1];
            if (prev == ' ' || prev == '\t' || prev == '\n' || prev == '\r') {
                back--;
                continue;
            }
            at_decl_start = (prev == '{' || prev == ';');
            break;
        }
        if (!at_decl_start) {
            continue;
        }
        /* Next non-whitespace after the name must be ':'. */
        size_t after_name = i + 4;
        while (after_name < len &&
               (src[after_name] == ' ' || src[after_name] == '\t' || src[after_name] == '\n')) {
            after_name++;
        }
        if (after_name >= len || src[after_name] != ':') {
            continue;
        }
        size_t val_start = after_name + 1;
        size_t val_end = val_start;
        while (val_end < len && src[val_end] != ';' && src[val_end] != '}') {
            val_end++;
        }
        /* Split off a trailing `!important`. */
        size_t effective_end = val_end;
        int important = 0;
        for (size_t k = val_start; k + 10 <= val_end; k++) {
            if (src[k] == '!' && strncasecmp(src + k + 1, "important", 9) == 0) {
                important = 1;
                effective_end = k;
                break;
            }
        }
        /* Tokenize (up to 3 value tokens). */
        char tokens[3][40];
        size_t token_lens[3] = {0};
        int token_count = 0;
        int parse_failed = 0;
        for (size_t k = val_start; k < effective_end;) {
            while (k < effective_end &&
                   (src[k] == ' ' || src[k] == '\t' || src[k] == '\n' || src[k] == '\r')) {
                k++;
            }
            size_t tok_start = k;
            while (k < effective_end && src[k] != ' ' && src[k] != '\t' && src[k] != '\n' &&
                   src[k] != '\r') {
                k++;
            }
            size_t tok_len = k - tok_start;
            if (tok_len == 0) {
                continue;
            }
            if (token_count == 3 || tok_len >= sizeof(tokens[0])) {
                parse_failed = 1;
                break;
            }
            memcpy(tokens[token_count], src + tok_start, tok_len);
            tokens[token_count][tok_len] = '\0';
            token_lens[token_count] = tok_len;
            token_count++;
        }
        if (parse_failed || token_count == 0) {
            continue;
        }
        /* Values we can't model — leave the declaration untouched. */
        int unsupported = 0;
        for (int t = 0; t < token_count; t++) {
            if (strcasecmp(tokens[t], "inherit") == 0 || strcasecmp(tokens[t], "unset") == 0 ||
                strcasecmp(tokens[t], "revert") == 0 || strncasecmp(tokens[t], "var(", 4) == 0 ||
                strcasecmp(tokens[t], "content") == 0) {
                unsupported = 1;
                break;
            }
        }
        if (unsupported) {
            continue;
        }
        const char *grow = NULL;
        const char *shrink = NULL;
        const char *basis = NULL;
        if (token_count == 1 && strcasecmp(tokens[0], "none") == 0) {
            grow = "0";
            shrink = "0";
            basis = "auto";
        } else if (token_count == 1 && strcasecmp(tokens[0], "auto") == 0) {
            grow = "1";
            shrink = "1";
            basis = "auto";
        } else if (token_count == 1 && strcasecmp(tokens[0], "initial") == 0) {
            grow = "0";
            shrink = "1";
            basis = "auto";
        } else {
            int bad = 0;
            for (int t = 0; t < token_count; t++) {
                if (flex_token_is_number(tokens[t], token_lens[t])) {
                    if (!grow) {
                        grow = tokens[t];
                    } else if (!shrink) {
                        shrink = tokens[t];
                    } else {
                        bad = 1;
                    }
                } else {
                    if (!basis) {
                        basis = tokens[t];
                    } else {
                        bad = 1;
                    }
                }
            }
            if (bad || (!grow && !basis)) {
                continue;
            }
            if (grow && !shrink) {
                shrink = "1";
            }
            if (grow && !basis) {
                basis = "0%";
            }
            if (!grow) { /* `flex: 300px` → 1 1 300px */
                grow = "1";
                shrink = "1";
            }
        }
        char replacement[192];
        const char *suffix = important ? " !important" : "";
        int written = snprintf(replacement, sizeof(replacement),
                               "flex-grow:%s%s;flex-shrink:%s%s;flex-basis:%s%s", grow, suffix,
                               shrink, suffix, basis, suffix);
        if (written < 0 || (size_t)written >= sizeof(replacement)) {
            continue;
        }
        if (flex_expand_append(&out, src + copied, i - copied) != 0 ||
            flex_expand_append(&out, replacement, (size_t)written) != 0) {
            free(out.data);
            return NULL;
        }
        copied = val_end; /* the source's ';' or '}' terminator is kept */
        changed = 1;
        i = val_end - 1;
    }
    if (!changed) {
        free(out.data);
        return NULL;
    }
    if (flex_expand_append(&out, src + copied, len - copied) != 0) {
        free(out.data);
        return NULL;
    }
    out.data[out.size] = '\0';
    *out_len = out.size;
    return out.data;
}
