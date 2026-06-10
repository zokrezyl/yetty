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
