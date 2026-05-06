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

#include "ylexbor-internal.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* strncasecmp */


/* ===========================================================================
 * Storage — linear scan; tens to low hundreds of entries on real sites.
 * ===========================================================================*/

static int customs_set(struct yetty_ylexbor_customs *t,
		       const char *name, size_t nlen,
		       const char *value, size_t vlen)
{
	for (int i = 0; i < t->size; i++) {
		if (strlen(t->data[i].name) == nlen &&
		    memcmp(t->data[i].name, name, nlen) == 0) {
			char *nv = malloc(vlen + 1);
			if (!nv) return -1;
			memcpy(nv, value, vlen); nv[vlen] = '\0';
			free(t->data[i].value);
			t->data[i].value = nv;
			return 0;
		}
	}
	if (t->size == t->cap) {
		int nc = t->cap ? t->cap * 2 : 32;
		struct yetty_ylexbor_custom_prop *p =
			realloc(t->data, nc * sizeof(*p));
		if (!p) return -1;
		t->data = p; t->cap = nc;
	}
	char *nm = malloc(nlen + 1);
	char *vl = malloc(vlen + 1);
	if (!nm || !vl) { free(nm); free(vl); return -1; }
	memcpy(nm, name, nlen); nm[nlen] = '\0';
	memcpy(vl, value, vlen); vl[vlen] = '\0';
	t->data[t->size].name  = nm;
	t->data[t->size].value = vl;
	t->size++;
	return 0;
}

static const char *customs_get(const struct yetty_ylexbor_customs *t,
			       const char *name, size_t nlen)
{
	for (int i = 0; i < t->size; i++) {
		if (strlen(t->data[i].name) == nlen &&
		    memcmp(t->data[i].name, name, nlen) == 0) {
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

/* ===========================================================================
 * Scanner — accept declarations from rules whose selector list contains
 * any of `:root`, `html`, `body`, `*`. We skip @media / @supports
 * blocks for now (they'd need media-query evaluation which we don't do).
 * ===========================================================================*/

static int sel_is_global_target_simple(const char *sel, size_t len)
{
	while (len > 0 && (*sel == ' ' || *sel == '\t' || *sel == '\n')) {
		sel++; len--;
	}
	while (len > 0 && (sel[len - 1] == ' ' || sel[len - 1] == '\t' ||
			   sel[len - 1] == '\n')) len--;
	if (len == 0) return 0;
	if (len == 5 && strncasecmp(sel, ":root", 5) == 0) return 1;
	if (len == 4 && strncasecmp(sel, "html", 4) == 0) return 1;
	if (len == 4 && strncasecmp(sel, "body", 4) == 0) return 1;
	if (len == 1 && sel[0] == '*') return 1;
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
	(void)sel; (void)len;
	(void)sel_is_global_target_simple;
	return 1;
}

/* Walk `src` looking for top-level rules. For each rule whose selector
 * looks like :root / html / body / asterisk-wildcard, scan its
 * declaration block for `--…` declarations and stash. */
void yetty_ylexbor_css_vars_scan(struct yetty_ylexbor *r,
				 const char *src, size_t len)
{
	if (!src || len == 0) return;
	const char *p = src;
	const char *end = src + len;

	while (p < end) {
		/* Skip whitespace + comments. */
		while (p < end && (*p == ' ' || *p == '\t' ||
				   *p == '\n' || *p == '\r')) p++;
		if (p + 1 < end && p[0] == '/' && p[1] == '*') {
			p += 2;
			while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
			if (p + 1 < end) p += 2; else p = end;
			continue;
		}
		if (p >= end) break;

		/* Skip @-rule blocks (and their nested rules). We don't
		 * try to evaluate @media; just descend into the body
		 * recursively so customs in :root inside @media do
		 * still get picked up — best-effort. */
		if (*p == '@') {
			/* Find { or ; */
			while (p < end && *p != '{' && *p != ';') p++;
			if (p < end && *p == ';') { p++; continue; }
			if (p < end && *p == '{') {
				p++;
				/* Find matching close — balance braces. */
				int depth = 1;
				const char *body_start = p;
				while (p < end && depth > 0) {
					if (*p == '{') depth++;
					else if (*p == '}') depth--;
					if (depth > 0) p++;
				}
				/* Recurse into the @-rule body. */
				yetty_ylexbor_css_vars_scan(r,
					body_start, (size_t)(p - body_start));
				if (p < end) p++;  /* skip closing } */
			}
			continue;
		}

		/* Selector list runs up to '{'. */
		const char *sel = p;
		while (p < end && *p != '{') p++;
		if (p >= end) break;
		size_t sel_len = (size_t)(p - sel);
		p++;  /* past { */
		const char *block = p;
		int depth = 1;
		while (p < end && depth > 0) {
			if (*p == '{') depth++;
			else if (*p == '}') depth--;
			if (depth > 0) p++;
		}
		size_t block_len = (size_t)(p - block);
		if (p < end) p++;

		if (!sel_is_global_target(sel, sel_len)) continue;

		/* Walk declarations: NAME : VALUE ; — but watch for
		 * nested () (var(...) values), [], "...", '...'. */
		const char *q = block;
		const char *bend = block + block_len;
		while (q < bend) {
			while (q < bend && (*q == ' ' || *q == '\t' ||
					    *q == '\n' || *q == '\r' ||
					    *q == ';')) q++;
			if (q + 1 < bend && q[0] == '/' && q[1] == '*') {
				q += 2;
				while (q + 1 < bend &&
				       !(q[0] == '*' && q[1] == '/')) q++;
				if (q + 1 < bend) q += 2; else q = bend;
				continue;
			}
			if (q >= bend) break;

			/* Property name. */
			const char *nm = q;
			while (q < bend && *q != ':' && *q != ';') q++;
			if (q >= bend || *q == ';') continue;
			size_t nlen = (size_t)(q - nm);
			while (nlen > 0 && (nm[nlen - 1] == ' ' ||
					    nm[nlen - 1] == '\t')) nlen--;
			q++;  /* past : */
			while (q < bend && (*q == ' ' || *q == '\t')) q++;
			const char *vstart = q;
			/* Value runs to ; (top level only). */
			int paren = 0, bracket = 0;
			char quote = 0;
			while (q < bend) {
				char c = *q;
				if (quote) {
					if (c == quote) quote = 0;
				} else if (c == '"' || c == '\'') {
					quote = c;
				} else if (c == '(') paren++;
				else if (c == ')') paren--;
				else if (c == '[') bracket++;
				else if (c == ']') bracket--;
				else if (c == ';' && paren == 0 && bracket == 0)
					break;
				q++;
			}
			size_t vlen = (size_t)(q - vstart);
			while (vlen > 0 && (vstart[vlen - 1] == ' ' ||
					    vstart[vlen - 1] == '\t' ||
					    vstart[vlen - 1] == '\n' ||
					    vstart[vlen - 1] == '\r')) vlen--;
			if (q < bend) q++;  /* skip ; */

			if (nlen >= 2 && nm[0] == '-' && nm[1] == '-' &&
			    vlen > 0) {
				(void)customs_set(&r->customs,
					nm, nlen, vstart, vlen);
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

static int append_str(char **buf, size_t *len, size_t *cap,
		      const char *src, size_t n)
{
	if (*len + n + 1 > *cap) {
		size_t nc = *cap ? *cap * 2 : 256;
		while (nc < *len + n + 1) nc *= 2;
		char *p = realloc(*buf, nc);
		if (!p) return -1;
		*buf = p; *cap = nc;
	}
	memcpy(*buf + *len, src, n);
	*len += n;
	(*buf)[*len] = '\0';
	return 0;
}

static int resolve_into(struct yetty_ylexbor *r,
			const char *src, size_t n,
			char **out, size_t *olen, size_t *ocap,
			int depth);

static int resolve_one_var(struct yetty_ylexbor *r,
			   const char *body, size_t blen,
			   char **out, size_t *olen, size_t *ocap,
			   int depth)
{
	/* body = "--name" or "--name , fallback expr". */
	size_t i = 0;
	while (i < blen && (body[i] == ' ' || body[i] == '\t')) i++;
	const char *name = body + i;
	size_t name_len = 0;
	while (i < blen && body[i] != ',' && body[i] != ' ' &&
	       body[i] != '\t') {
		i++; name_len++;
	}
	while (i < blen && (body[i] == ' ' || body[i] == '\t')) i++;
	const char *fallback = NULL;
	size_t fallback_len = 0;
	if (i < blen && body[i] == ',') {
		i++;
		while (i < blen && (body[i] == ' ' || body[i] == '\t')) i++;
		fallback = body + i;
		fallback_len = blen - i;
		while (fallback_len > 0 &&
		       (fallback[fallback_len - 1] == ' ' ||
			fallback[fallback_len - 1] == '\t')) fallback_len--;
	}

	const char *val = customs_get(&r->customs, name, name_len);
	if (val && val[0]) {
		return resolve_into(r, val, strlen(val),
				    out, olen, ocap, depth + 1);
	}
	if (fallback && fallback_len > 0) {
		return resolve_into(r, fallback, fallback_len,
				    out, olen, ocap, depth + 1);
	}
	return 0;
}

static int resolve_into(struct yetty_ylexbor *r,
			const char *src, size_t n,
			char **out, size_t *olen, size_t *ocap,
			int depth)
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
				if (src[i] == '(') paren++;
				else if (src[i] == ')') paren--;
				if (paren > 0) i++;
			}
			size_t blen = i - start;
			(void)resolve_one_var(r, src + start, blen,
					      out, olen, ocap, depth);
			if (i < n) i++;  /* skip ) */
			continue;
		}
		if (append_str(out, olen, ocap, src + i, 1) != 0) return -1;
		i++;
	}
	return 0;
}

char *yetty_ylexbor_css_vars_resolve(struct yetty_ylexbor *r,
				     const char *value, size_t len)
{
	char *out = NULL;
	size_t olen = 0, ocap = 0;
	if (resolve_into(r, value, len, &out, &olen, &ocap, 0) != 0) {
		free(out);
		out = malloc(len + 1);
		if (!out) return NULL;
		memcpy(out, value, len); out[len] = '\0';
	}
	if (!out) {
		out = malloc(1);
		if (out) out[0] = '\0';
	}
	return out;
}
