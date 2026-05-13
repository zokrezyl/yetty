/*
 * ysvg-css.c — CSS support for SVG <style> elements.
 *
 * Scope: SVG Tiny 1.2 styling chapter (§ 8). We parse complex selector
 * chains with descendant / child / adjacent / general-sibling
 * combinators, attribute selectors, structural pseudo-classes, and
 * !important. Property → style application reuses the same applier as
 * the inline style attribute (ysvg-style.c), so every property the
 * cascade understands works via CSS too.
 *
 * Not supported (silently ignored, malformed rule skipped at next '}'):
 *   @media query bodies (rules inside are dropped)
 *   pseudo-elements (::before / ::after — no content to insert anyway)
 *   :nth-child / :nth-of-type expressions
 *   dynamic pseudo-classes (:hover / :focus / :active / :checked) — we
 *     recognise them so they don't crash matching but they always
 *     return false (no user interaction state).
 *
 * Cascade order at the call site (ysvg-style.c):
 *   inherited parent → presentation attrs → CSS (asc specificity)
 *     → inline style="…"
 * !important from CSS or inline overrides any non-important — handled
 * here by re-applying important decls last, after their non-important
 * pass.
 */

#include "ysvg-internal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Lexical helpers
 *===========================================================================*/

static int css_is_wsp(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int css_is_name_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || c >= 0x80;
}

static int css_is_name_char(int c)
{
    return css_is_name_start(c) || (c >= '0' && c <= '9');
}

/* Skip whitespace + /* … *⁠/ comments. */
static size_t css_skip_ws(const char *s, size_t len, size_t pos)
{
    for (;;) {
        while (pos < len && css_is_wsp((unsigned char)s[pos])) pos++;
        if (pos + 1 < len && s[pos] == '/' && s[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < len && !(s[pos] == '*' && s[pos + 1] == '/')) pos++;
            if (pos + 1 < len) pos += 2;
            continue;
        }
        break;
    }
    return pos;
}

static char *css_dup_slice(const char *s, size_t start, size_t end)
{
    size_t n = end - start;
    char *out = malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s + start, n);
    out[n] = '\0';
    return out;
}

static char *css_dup_lower(const char *s, size_t start, size_t end)
{
    char *out = css_dup_slice(s, start, end);
    if (!out) return NULL;
    for (char *p = out; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    }
    return out;
}

/* Read a CSS ident token at *pos. On success returns 1 and writes the
 * slice bounds; on no-ident returns 0 and leaves *pos unchanged. */
static int css_read_ident(const char *s, size_t len, size_t *pos, size_t *out_start,
                          size_t *out_end)
{
    size_t p = *pos;
    /* Optional leading '-' (e.g. -webkit-*) — only if followed by a name char. */
    size_t start = p;
    if (p < len && s[p] == '-') {
        if (p + 1 >= len || !css_is_name_start((unsigned char)s[p + 1])) return 0;
        p++;
    } else if (p >= len || !css_is_name_start((unsigned char)s[p])) {
        return 0;
    }
    while (p < len && css_is_name_char((unsigned char)s[p])) p++;
    *out_start = start;
    *out_end = p;
    *pos = p;
    return 1;
}

/* Read a CSS string literal: "..." or '...'. Returns 1 + slice bounds
 * EXCLUDING the surrounding quotes. Backslash escapes are passed
 * through verbatim (we don't currently decode \nnnn). */
static int css_read_string(const char *s, size_t len, size_t *pos, size_t *out_start,
                           size_t *out_end)
{
    if (*pos >= len) return 0;
    char q = s[*pos];
    if (q != '"' && q != '\'') return 0;
    size_t p = *pos + 1;
    *out_start = p;
    while (p < len && s[p] != q) {
        if (s[p] == '\\' && p + 1 < len) p++;
        p++;
    }
    *out_end = p;
    if (p < len) p++; /* consume closing quote */
    *pos = p;
    return 1;
}

/*=============================================================================
 * Selector parser
 *===========================================================================*/

static void css_free_simple(struct yetty_ysvg_css_simple *s);

static void css_free_compound(struct yetty_ysvg_css_compound *c)
{
    for (size_t i = 0; i < c->simple_count; i++) css_free_simple(&c->simples[i]);
    free(c->simples);
    c->simples = NULL;
    c->simple_count = 0;
}

static void css_free_complex(struct yetty_ysvg_css_complex *c)
{
    for (size_t i = 0; i < c->step_count; i++) css_free_compound(&c->steps[i].compound);
    free(c->steps);
    c->steps = NULL;
    c->step_count = 0;
}

static void css_free_simple(struct yetty_ysvg_css_simple *s)
{
    free(s->name);
    free(s->value);
    s->name = s->value = NULL;
    if (s->not_child) {
        css_free_simple(s->not_child);
        free(s->not_child);
        s->not_child = NULL;
    }
}

static int compound_push(struct yetty_ysvg_css_compound *c, struct yetty_ysvg_css_simple s)
{
    struct yetty_ysvg_css_simple *na = realloc(c->simples, (c->simple_count + 1) * sizeof(*na));
    if (!na) {
        css_free_simple(&s);
        return -1;
    }
    c->simples = na;
    c->simples[c->simple_count++] = s;
    return 0;
}

static enum yetty_ysvg_css_pseudo_kind css_pseudo_lookup(const char *name)
{
    if (!strcmp(name, "first-child")) return YETTY_YSVG_CSS_PSEUDO_FIRST_CHILD;
    if (!strcmp(name, "last-child")) return YETTY_YSVG_CSS_PSEUDO_LAST_CHILD;
    if (!strcmp(name, "only-child")) return YETTY_YSVG_CSS_PSEUDO_ONLY_CHILD;
    if (!strcmp(name, "root")) return YETTY_YSVG_CSS_PSEUDO_ROOT;
    if (!strcmp(name, "empty")) return YETTY_YSVG_CSS_PSEUDO_EMPTY;
    if (!strcmp(name, "not")) return YETTY_YSVG_CSS_PSEUDO_NOT;
    if (!strcmp(name, "hover")) return YETTY_YSVG_CSS_PSEUDO_HOVER;
    if (!strcmp(name, "focus")) return YETTY_YSVG_CSS_PSEUDO_FOCUS;
    if (!strcmp(name, "active")) return YETTY_YSVG_CSS_PSEUDO_ACTIVE;
    if (!strcmp(name, "link")) return YETTY_YSVG_CSS_PSEUDO_LINK;
    if (!strcmp(name, "visited")) return YETTY_YSVG_CSS_PSEUDO_VISITED;
    if (!strcmp(name, "enabled")) return YETTY_YSVG_CSS_PSEUDO_ENABLED;
    if (!strcmp(name, "disabled")) return YETTY_YSVG_CSS_PSEUDO_DISABLED;
    if (!strcmp(name, "checked")) return YETTY_YSVG_CSS_PSEUDO_CHECKED;
    if (!strcmp(name, "target")) return YETTY_YSVG_CSS_PSEUDO_TARGET;
    return YETTY_YSVG_CSS_PSEUDO_UNKNOWN;
}

/* Parse a single simple selector token. Returns 1 on success, 0 when no
 * simple selector is present at *pos (caller can decide to stop), -1 on
 * a malformed selector (skip rule). */
static int css_parse_simple(const char *s, size_t len, size_t *pos,
                            struct yetty_ysvg_css_simple *out)
{
    memset(out, 0, sizeof(*out));
    if (*pos >= len) return 0;
    char c = s[*pos];

    if (c == '*') {
        out->kind = YETTY_YSVG_CSS_SIMPLE_UNIVERSAL;
        (*pos)++;
        return 1;
    }
    if (c == '#') {
        (*pos)++;
        size_t a, b;
        if (!css_read_ident(s, len, pos, &a, &b)) return -1;
        out->kind = YETTY_YSVG_CSS_SIMPLE_ID;
        out->name = css_dup_slice(s, a, b);
        if (!out->name) return -1;
        return 1;
    }
    if (c == '.') {
        (*pos)++;
        size_t a, b;
        if (!css_read_ident(s, len, pos, &a, &b)) return -1;
        out->kind = YETTY_YSVG_CSS_SIMPLE_CLASS;
        out->name = css_dup_slice(s, a, b);
        if (!out->name) return -1;
        return 1;
    }
    if (c == '[') {
        (*pos)++;
        *pos = css_skip_ws(s, len, *pos);
        size_t a, b;
        if (!css_read_ident(s, len, pos, &a, &b)) return -1;
        out->kind = YETTY_YSVG_CSS_SIMPLE_ATTR;
        out->name = css_dup_lower(s, a, b);
        if (!out->name) return -1;
        out->attr_op = YETTY_YSVG_CSS_ATTR_EXISTS;
        *pos = css_skip_ws(s, len, *pos);
        if (*pos < len && s[*pos] != ']') {
            /* Optional operator: =, ~=, |=, ^=, $=, *= */
            if (*pos + 1 < len && s[*pos + 1] == '=') {
                switch (s[*pos]) {
                case '~': out->attr_op = YETTY_YSVG_CSS_ATTR_INCLUDES; break;
                case '|': out->attr_op = YETTY_YSVG_CSS_ATTR_DASHMATCH; break;
                case '^': out->attr_op = YETTY_YSVG_CSS_ATTR_PREFIX; break;
                case '$': out->attr_op = YETTY_YSVG_CSS_ATTR_SUFFIX; break;
                case '*': out->attr_op = YETTY_YSVG_CSS_ATTR_SUBSTR; break;
                default: return -1;
                }
                *pos += 2;
            } else if (s[*pos] == '=') {
                out->attr_op = YETTY_YSVG_CSS_ATTR_EQUAL;
                (*pos)++;
            } else {
                return -1;
            }
            *pos = css_skip_ws(s, len, *pos);
            size_t va, vb;
            if (css_read_string(s, len, pos, &va, &vb)) {
                out->value = css_dup_slice(s, va, vb);
            } else if (css_read_ident(s, len, pos, &va, &vb)) {
                out->value = css_dup_slice(s, va, vb);
            } else {
                return -1;
            }
            if (!out->value) return -1;
            *pos = css_skip_ws(s, len, *pos);
        }
        if (*pos >= len || s[*pos] != ']') return -1;
        (*pos)++;
        return 1;
    }
    if (c == ':') {
        (*pos)++;
        if (*pos < len && s[*pos] == ':') {
            /* ::pseudo-element — consume identifier and ignore by setting kind=UNKNOWN. */
            (*pos)++;
            size_t a, b;
            if (!css_read_ident(s, len, pos, &a, &b)) return -1;
            out->kind = YETTY_YSVG_CSS_SIMPLE_PSEUDO;
            out->pseudo = YETTY_YSVG_CSS_PSEUDO_UNKNOWN;
            out->name = css_dup_slice(s, a, b);
            return out->name ? 1 : -1;
        }
        size_t a, b;
        if (!css_read_ident(s, len, pos, &a, &b)) return -1;
        out->kind = YETTY_YSVG_CSS_SIMPLE_PSEUDO;
        out->name = css_dup_lower(s, a, b);
        if (!out->name) return -1;
        out->pseudo = css_pseudo_lookup(out->name);
        if (out->pseudo == YETTY_YSVG_CSS_PSEUDO_NOT && *pos < len && s[*pos] == '(') {
            (*pos)++;
            *pos = css_skip_ws(s, len, *pos);
            struct yetty_ysvg_css_simple inner;
            int r = css_parse_simple(s, len, pos, &inner);
            if (r < 0) return -1;
            if (r == 0) return -1; /* :not() with no argument */
            out->not_child = malloc(sizeof(*out->not_child));
            if (!out->not_child) {
                css_free_simple(&inner);
                return -1;
            }
            *out->not_child = inner;
            *pos = css_skip_ws(s, len, *pos);
            if (*pos >= len || s[*pos] != ')') return -1;
            (*pos)++;
        } else if (*pos < len && s[*pos] == '(') {
            /* Skip the argument list of any other functional pseudo — keeps
             * us going past :nth-child(...) without crashing; the pseudo
             * just won't match anything we render. */
            int depth = 1;
            (*pos)++;
            while (*pos < len && depth > 0) {
                if (s[*pos] == '(') depth++;
                else if (s[*pos] == ')') depth--;
                (*pos)++;
            }
        }
        return 1;
    }
    if (css_is_name_start((unsigned char)c)) {
        size_t a, b;
        if (!css_read_ident(s, len, pos, &a, &b)) return -1;
        out->kind = YETTY_YSVG_CSS_SIMPLE_TYPE;
        out->name = css_dup_lower(s, a, b);
        return out->name ? 1 : -1;
    }
    return 0;
}

/* Parse a compound (sequence of simple selectors with no combinator
 * between them). Returns 1 on at-least-one, 0 on none, -1 on error. */
static int css_parse_compound(const char *s, size_t len, size_t *pos,
                              struct yetty_ysvg_css_compound *out)
{
    memset(out, 0, sizeof(*out));
    for (;;) {
        struct yetty_ysvg_css_simple sim;
        int r = css_parse_simple(s, len, pos, &sim);
        if (r < 0) {
            css_free_compound(out);
            return -1;
        }
        if (r == 0) break;
        if (compound_push(out, sim) < 0) {
            css_free_compound(out);
            return -1;
        }
    }
    return out->simple_count > 0 ? 1 : 0;
}

/* Parse a complex selector. Returns 1 on success, 0 on empty, -1 on error. */
static int css_parse_complex(const char *s, size_t len, size_t *pos,
                             struct yetty_ysvg_css_complex *out)
{
    memset(out, 0, sizeof(*out));
    *pos = css_skip_ws(s, len, *pos);
    struct yetty_ysvg_css_compound first;
    int r = css_parse_compound(s, len, pos, &first);
    if (r < 0) return -1;
    if (r == 0) return 0;

    struct yetty_ysvg_css_complex_step *steps = malloc(sizeof(*steps));
    if (!steps) {
        css_free_compound(&first);
        return -1;
    }
    steps[0].combinator = YETTY_YSVG_CSS_DESCENDANT;
    steps[0].compound = first;
    out->steps = steps;
    out->step_count = 1;

    for (;;) {
        /* Detect combinator. Whitespace counts as descendant only if
         * the NEXT non-ws char isn't a combinator/end. */
        size_t save = *pos;
        int had_ws = 0;
        while (*pos < len && css_is_wsp((unsigned char)s[*pos])) {
            had_ws = 1;
            (*pos)++;
        }
        if (*pos >= len) break;
        char c = s[*pos];
        enum yetty_ysvg_css_combinator comb = YETTY_YSVG_CSS_DESCENDANT;
        int explicit = 0;
        if (c == '>') {
            comb = YETTY_YSVG_CSS_CHILD;
            (*pos)++;
            explicit = 1;
        } else if (c == '+') {
            comb = YETTY_YSVG_CSS_ADJACENT;
            (*pos)++;
            explicit = 1;
        } else if (c == '~') {
            comb = YETTY_YSVG_CSS_SIBLING;
            (*pos)++;
            explicit = 1;
        } else if (c == ',' || c == '{') {
            *pos = save;
            break;
        } else if (!had_ws) {
            *pos = save;
            break;
        }
        *pos = css_skip_ws(s, len, *pos);
        struct yetty_ysvg_css_compound next;
        int rr = css_parse_compound(s, len, pos, &next);
        if (rr <= 0) {
            if (explicit) {
                css_free_complex(out);
                return -1;
            }
            /* Pure trailing whitespace — no new compound. */
            *pos = save;
            break;
        }
        struct yetty_ysvg_css_complex_step *ns =
            realloc(out->steps, (out->step_count + 1) * sizeof(*ns));
        if (!ns) {
            css_free_compound(&next);
            css_free_complex(out);
            return -1;
        }
        out->steps = ns;
        out->steps[out->step_count].combinator = comb;
        out->steps[out->step_count].compound = next;
        out->step_count++;
    }

    /* Compute specificity over all simples in every compound. */
    for (size_t i = 0; i < out->step_count; i++) {
        const struct yetty_ysvg_css_compound *cp = &out->steps[i].compound;
        for (size_t j = 0; j < cp->simple_count; j++) {
            switch (cp->simples[j].kind) {
            case YETTY_YSVG_CSS_SIMPLE_ID:
                out->spec_a++;
                break;
            case YETTY_YSVG_CSS_SIMPLE_CLASS:
            case YETTY_YSVG_CSS_SIMPLE_ATTR:
                out->spec_b++;
                break;
            case YETTY_YSVG_CSS_SIMPLE_PSEUDO:
                /* Pseudo-elements would count as types (c), but we route
                 * those into UNKNOWN above — treat as c+ to keep their
                 * implicit type-like weight; not visible in matching
                 * anyway. */
                out->spec_b++;
                break;
            case YETTY_YSVG_CSS_SIMPLE_TYPE:
                out->spec_c++;
                break;
            case YETTY_YSVG_CSS_SIMPLE_UNIVERSAL:
                break;
            }
        }
    }
    return 1;
}

/*=============================================================================
 * Declaration parser
 *===========================================================================*/

static int css_parse_decl(const char *s, size_t len, size_t *pos, struct yetty_ysvg_css_decl *out)
{
    memset(out, 0, sizeof(*out));
    *pos = css_skip_ws(s, len, *pos);
    size_t pa, pb;
    if (!css_read_ident(s, len, pos, &pa, &pb)) return 0;
    out->property = css_dup_lower(s, pa, pb);
    if (!out->property) return -1;

    *pos = css_skip_ws(s, len, *pos);
    if (*pos >= len || s[*pos] != ':') {
        free(out->property);
        out->property = NULL;
        return 0;
    }
    (*pos)++;
    *pos = css_skip_ws(s, len, *pos);

    /* Value: take everything up to ';' or '}', minding strings + nested
     * parens (e.g. url("http://;...") shouldn't end the value at ';'). */
    size_t vstart = *pos;
    while (*pos < len && s[*pos] != ';' && s[*pos] != '}') {
        if (s[*pos] == '"' || s[*pos] == '\'') {
            char q = s[*pos];
            (*pos)++;
            while (*pos < len && s[*pos] != q) {
                if (s[*pos] == '\\' && *pos + 1 < len) (*pos)++;
                (*pos)++;
            }
            if (*pos < len) (*pos)++;
        } else if (s[*pos] == '(') {
            int depth = 1;
            (*pos)++;
            while (*pos < len && depth > 0) {
                if (s[*pos] == '(') depth++;
                else if (s[*pos] == ')') depth--;
                (*pos)++;
            }
        } else {
            (*pos)++;
        }
    }
    size_t vend = *pos;
    /* Trim trailing whitespace. */
    while (vend > vstart && css_is_wsp((unsigned char)s[vend - 1])) vend--;
    /* Detect trailing "!important". */
    if (vend - vstart >= 10) {
        size_t scan = vend;
        while (scan > vstart && (css_is_name_char((unsigned char)s[scan - 1]) ||
                                  s[scan - 1] == '!'))
            scan--;
        if (vend - scan >= 10 && memcmp(s + scan, "!important", 10) == 0) {
            int valid = 1;
            for (size_t i = scan + 10; i < vend; i++) {
                if (!css_is_wsp((unsigned char)s[i])) {
                    valid = 0;
                    break;
                }
            }
            if (valid) {
                /* trim "!important" + preceding whitespace */
                size_t cut = scan;
                while (cut > vstart && css_is_wsp((unsigned char)s[cut - 1])) cut--;
                vend = cut;
                out->important = 1;
            }
        }
    }
    out->value = css_dup_slice(s, vstart, vend);
    if (!out->value) {
        free(out->property);
        out->property = NULL;
        return -1;
    }
    return 1;
}

static void css_free_decl(struct yetty_ysvg_css_decl *d)
{
    free(d->property);
    free(d->value);
    d->property = d->value = NULL;
}

/*=============================================================================
 * Rule + sheet
 *===========================================================================*/

static void css_free_rule(struct yetty_ysvg_css_rule *r)
{
    for (size_t i = 0; i < r->selector_count; i++) css_free_complex(&r->selectors[i]);
    free(r->selectors);
    for (size_t i = 0; i < r->decl_count; i++) css_free_decl(&r->decls[i]);
    free(r->decls);
    r->selectors = NULL;
    r->decls = NULL;
    r->selector_count = r->decl_count = 0;
}

void yetty_ysvg_css_free_doc_rules(struct yetty_ysvg_doc *doc)
{
    if (!doc) return;
    for (size_t i = 0; i < doc->rule_count; i++) css_free_rule(&doc->rules[i]);
    free(doc->rules);
    doc->rules = NULL;
    doc->rule_count = doc->rule_cap = 0;
}

static int doc_push_rule(struct yetty_ysvg_doc *doc, struct yetty_ysvg_css_rule rule)
{
    if (doc->rule_count == doc->rule_cap) {
        size_t nc = doc->rule_cap ? doc->rule_cap * 2 : 8;
        struct yetty_ysvg_css_rule *nr = realloc(doc->rules, nc * sizeof(*nr));
        if (!nr) return -1;
        doc->rules = nr;
        doc->rule_cap = nc;
    }
    doc->rules[doc->rule_count++] = rule;
    return 0;
}

/* Skip to balancing '}' or end. Used after a malformed selector. */
static void css_skip_block(const char *s, size_t len, size_t *pos)
{
    int depth = 0;
    while (*pos < len) {
        char c = s[*pos];
        if (c == '"' || c == '\'') {
            char q = c;
            (*pos)++;
            while (*pos < len && s[*pos] != q) {
                if (s[*pos] == '\\' && *pos + 1 < len) (*pos)++;
                (*pos)++;
            }
            if (*pos < len) (*pos)++;
            continue;
        }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth <= 0) {
                (*pos)++;
                return;
            }
        }
        (*pos)++;
    }
}

/* Skip an @-rule from '@' through end. We never honour @media bodies. */
static void css_skip_at_rule(const char *s, size_t len, size_t *pos)
{
    /* skip past '@' and the keyword */
    (*pos)++;
    while (*pos < len && css_is_name_char((unsigned char)s[*pos])) (*pos)++;
    /* skip until ';' (terminates @import/@charset) or '{' (block) */
    while (*pos < len && s[*pos] != ';' && s[*pos] != '{') {
        if (s[*pos] == '"' || s[*pos] == '\'') {
            char q = s[*pos];
            (*pos)++;
            while (*pos < len && s[*pos] != q) {
                if (s[*pos] == '\\' && *pos + 1 < len) (*pos)++;
                (*pos)++;
            }
            if (*pos < len) (*pos)++;
            continue;
        }
        (*pos)++;
    }
    if (*pos < len) {
        if (s[*pos] == ';') {
            (*pos)++;
            return;
        }
        if (s[*pos] == '{') {
            (*pos)++;
            css_skip_block(s, len, pos);
            return;
        }
    }
}

int yetty_ysvg_css_parse(struct yetty_ysvg_doc *doc, const char *css, size_t len)
{
    if (!doc || !css || len == 0) return 0;
    size_t pos = 0;
    while (pos < len) {
        pos = css_skip_ws(css, len, pos);
        if (pos >= len) break;
        if (css[pos] == '@') {
            css_skip_at_rule(css, len, &pos);
            continue;
        }
        if (css[pos] == '}') {
            /* stray close brace */
            pos++;
            continue;
        }

        /* Selector list */
        struct yetty_ysvg_css_complex *sels = NULL;
        size_t sel_count = 0;
        for (;;) {
            struct yetty_ysvg_css_complex cx;
            int r = css_parse_complex(css, len, &pos, &cx);
            if (r < 0) {
                for (size_t i = 0; i < sel_count; i++) css_free_complex(&sels[i]);
                free(sels);
                /* Resync at next '}' so caller continues. */
                css_skip_block(css, len, &pos);
                goto rule_done;
            }
            if (r == 0) break;
            struct yetty_ysvg_css_complex *ns =
                realloc(sels, (sel_count + 1) * sizeof(*ns));
            if (!ns) {
                css_free_complex(&cx);
                for (size_t i = 0; i < sel_count; i++) css_free_complex(&sels[i]);
                free(sels);
                return -1;
            }
            sels = ns;
            sels[sel_count++] = cx;
            pos = css_skip_ws(css, len, pos);
            if (pos < len && css[pos] == ',') {
                pos++;
                continue;
            }
            break;
        }
        pos = css_skip_ws(css, len, pos);
        if (pos >= len || css[pos] != '{') {
            for (size_t i = 0; i < sel_count; i++) css_free_complex(&sels[i]);
            free(sels);
            continue;
        }
        pos++; /* { */

        /* Declarations */
        struct yetty_ysvg_css_decl *decls = NULL;
        size_t dcount = 0;
        for (;;) {
            pos = css_skip_ws(css, len, pos);
            if (pos >= len || css[pos] == '}') break;
            struct yetty_ysvg_css_decl d;
            int r = css_parse_decl(css, len, &pos, &d);
            if (r < 0) {
                for (size_t i = 0; i < dcount; i++) css_free_decl(&decls[i]);
                free(decls);
                for (size_t i = 0; i < sel_count; i++) css_free_complex(&sels[i]);
                free(sels);
                return -1;
            }
            if (r == 0) {
                /* Skip to ';' or '}' to resume. */
                while (pos < len && css[pos] != ';' && css[pos] != '}') pos++;
            } else {
                struct yetty_ysvg_css_decl *nd =
                    realloc(decls, (dcount + 1) * sizeof(*nd));
                if (!nd) {
                    css_free_decl(&d);
                    for (size_t i = 0; i < dcount; i++) css_free_decl(&decls[i]);
                    free(decls);
                    for (size_t i = 0; i < sel_count; i++) css_free_complex(&sels[i]);
                    free(sels);
                    return -1;
                }
                decls = nd;
                decls[dcount++] = d;
            }
            pos = css_skip_ws(css, len, pos);
            if (pos < len && css[pos] == ';') pos++;
        }
        if (pos < len) pos++; /* '}' */

        struct yetty_ysvg_css_rule rule = {
            .selectors = sels,
            .selector_count = sel_count,
            .decls = decls,
            .decl_count = dcount,
            .order = (uint32_t)doc->rule_count,
        };
        if (doc_push_rule(doc, rule) < 0) {
            css_free_rule(&rule);
            return -1;
        }
    rule_done:;
    }
    return 0;
}

/*=============================================================================
 * Matching
 *===========================================================================*/

static int node_tag_eq(const struct yetty_ysvg_node *n, const char *tag_lc)
{
    if (!n->tag_name) return 0;
    const char *local = n->tag_name;
    const char *colon = strchr(local, ':');
    if (colon) local = colon + 1;
    /* Compare case-insensitively — SVG tag names are lowercased in
     * authoring practice but technically case-sensitive for XML;
     * lowering both sides is safest. */
    for (size_t i = 0;; i++) {
        char a = local[i];
        char b = tag_lc[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (a != b) return 0;
        if (!a) return 1;
    }
}

static int node_id_eq(const struct yetty_ysvg_node *n, const char *id)
{
    const struct yetty_ysvg_attr *a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_ID);
    if (!a || !a->value) return 0;
    size_t il = strlen(id);
    return a->value_len == il && memcmp(a->value, id, il) == 0;
}

static int node_has_class(const struct yetty_ysvg_node *n, const char *cls)
{
    const struct yetty_ysvg_attr *a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_CLASS);
    if (!a || !a->value) return 0;
    size_t cl = strlen(cls);
    const char *p = a->value;
    size_t plen = a->value_len;
    size_t i = 0;
    while (i < plen) {
        while (i < plen && css_is_wsp((unsigned char)p[i])) i++;
        size_t s = i;
        while (i < plen && !css_is_wsp((unsigned char)p[i])) i++;
        if (i - s == cl && memcmp(p + s, cls, cl) == 0) return 1;
    }
    return 0;
}

static const struct yetty_ysvg_attr *node_attr(const struct yetty_ysvg_node *n,
                                               const char *name_lc)
{
    /* The attr_find_named call wants the literal name; SVG attribute names
     * are usually camelCase or hyphen-case. We compare lowercase against
     * lowercase. */
    if (!n) return NULL;
    for (size_t i = 0; i < n->attr_count; i++) {
        const struct yetty_ysvg_attr *a = &n->attrs[i];
        if (!a->name) continue;
        if (a->name_len != strlen(name_lc)) continue;
        int eq = 1;
        for (size_t j = 0; j < a->name_len; j++) {
            char ca = a->name[j];
            if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
            if (ca != name_lc[j]) {
                eq = 0;
                break;
            }
        }
        if (eq) return a;
    }
    return NULL;
}

static int attr_matches_op(const char *av, size_t avlen, const char *val,
                           enum yetty_ysvg_css_attr_op op)
{
    if (!val) return op == YETTY_YSVG_CSS_ATTR_EXISTS;
    size_t vl = strlen(val);
    switch (op) {
    case YETTY_YSVG_CSS_ATTR_EXISTS:
        return 1;
    case YETTY_YSVG_CSS_ATTR_EQUAL:
        return avlen == vl && memcmp(av, val, vl) == 0;
    case YETTY_YSVG_CSS_ATTR_INCLUDES: {
        size_t i = 0;
        while (i < avlen) {
            while (i < avlen && css_is_wsp((unsigned char)av[i])) i++;
            size_t s = i;
            while (i < avlen && !css_is_wsp((unsigned char)av[i])) i++;
            if (i - s == vl && memcmp(av + s, val, vl) == 0) return 1;
        }
        return 0;
    }
    case YETTY_YSVG_CSS_ATTR_DASHMATCH:
        if (avlen == vl && memcmp(av, val, vl) == 0) return 1;
        if (avlen > vl && av[vl] == '-' && memcmp(av, val, vl) == 0) return 1;
        return 0;
    case YETTY_YSVG_CSS_ATTR_PREFIX:
        return avlen >= vl && memcmp(av, val, vl) == 0;
    case YETTY_YSVG_CSS_ATTR_SUFFIX:
        return avlen >= vl && memcmp(av + avlen - vl, val, vl) == 0;
    case YETTY_YSVG_CSS_ATTR_SUBSTR: {
        if (vl == 0) return 1;
        for (size_t i = 0; i + vl <= avlen; i++) {
            if (memcmp(av + i, val, vl) == 0) return 1;
        }
        return 0;
    }
    }
    return 0;
}

static int node_position(const struct yetty_ysvg_node *n, int *is_first, int *is_last,
                         int *is_only)
{
    /* All sibling tests are over ELEMENT children — and SVG nodes are
     * already element-only (no text-only siblings tracked separately in
     * our DOM). */
    if (!n) return 0;
    struct yetty_ysvg_node *parent = n->parent;
    if (!parent) {
        *is_first = *is_last = *is_only = 1;
        return 1;
    }
    int first = (parent->first_child == n);
    int last = (parent->last_child == n);
    int only = first && last;
    *is_first = first;
    *is_last = last;
    *is_only = only;
    return 1;
}

static int simple_matches(const struct yetty_ysvg_css_simple *sel,
                          const struct yetty_ysvg_node *n)
{
    if (!n) return 0;
    switch (sel->kind) {
    case YETTY_YSVG_CSS_SIMPLE_UNIVERSAL:
        return 1;
    case YETTY_YSVG_CSS_SIMPLE_TYPE:
        return node_tag_eq(n, sel->name);
    case YETTY_YSVG_CSS_SIMPLE_ID:
        return node_id_eq(n, sel->name);
    case YETTY_YSVG_CSS_SIMPLE_CLASS:
        return node_has_class(n, sel->name);
    case YETTY_YSVG_CSS_SIMPLE_ATTR: {
        const struct yetty_ysvg_attr *a = node_attr(n, sel->name);
        if (!a) return 0;
        return attr_matches_op(a->value, a->value_len, sel->value, sel->attr_op);
    }
    case YETTY_YSVG_CSS_SIMPLE_PSEUDO:
        switch (sel->pseudo) {
        case YETTY_YSVG_CSS_PSEUDO_ROOT:
            return n->parent == NULL;
        case YETTY_YSVG_CSS_PSEUDO_EMPTY:
            return n->first_child == NULL && (!n->text || n->text_len == 0);
        case YETTY_YSVG_CSS_PSEUDO_FIRST_CHILD: {
            int f, l, o;
            return node_position(n, &f, &l, &o) ? f : 0;
        }
        case YETTY_YSVG_CSS_PSEUDO_LAST_CHILD: {
            int f, l, o;
            return node_position(n, &f, &l, &o) ? l : 0;
        }
        case YETTY_YSVG_CSS_PSEUDO_ONLY_CHILD: {
            int f, l, o;
            return node_position(n, &f, &l, &o) ? o : 0;
        }
        case YETTY_YSVG_CSS_PSEUDO_NOT:
            return sel->not_child ? !simple_matches(sel->not_child, n) : 0;
        /* Dynamic / state pseudo-classes — we have no UI state. */
        case YETTY_YSVG_CSS_PSEUDO_HOVER:
        case YETTY_YSVG_CSS_PSEUDO_FOCUS:
        case YETTY_YSVG_CSS_PSEUDO_ACTIVE:
        case YETTY_YSVG_CSS_PSEUDO_LINK:
        case YETTY_YSVG_CSS_PSEUDO_VISITED:
        case YETTY_YSVG_CSS_PSEUDO_ENABLED:
        case YETTY_YSVG_CSS_PSEUDO_DISABLED:
        case YETTY_YSVG_CSS_PSEUDO_CHECKED:
        case YETTY_YSVG_CSS_PSEUDO_TARGET:
        case YETTY_YSVG_CSS_PSEUDO_UNKNOWN:
            return 0;
        }
        return 0;
    }
    return 0;
}

static int compound_matches(const struct yetty_ysvg_css_compound *c,
                            const struct yetty_ysvg_node *n)
{
    for (size_t i = 0; i < c->simple_count; i++) {
        if (!simple_matches(&c->simples[i], n)) return 0;
    }
    return 1;
}

/* Walk a complex selector right-to-left. Returns 1 if the rightmost
 * compound matches `n` and every left-of-it compound matches via the
 * indicated combinator climbing the ancestors / preceding siblings. */
static int complex_matches(const struct yetty_ysvg_css_complex *cx,
                           const struct yetty_ysvg_node *n)
{
    if (cx->step_count == 0) return 0;
    /* Rightmost compound must match the candidate node. */
    if (!compound_matches(&cx->steps[cx->step_count - 1].compound, n)) return 0;
    /* Walk leftward through steps. The combinator on steps[i] applies
     * BETWEEN steps[i-1] and steps[i]. */
    const struct yetty_ysvg_node *cursor = n;
    for (ssize_t i = (ssize_t)cx->step_count - 1; i > 0; i--) {
        enum yetty_ysvg_css_combinator comb = cx->steps[i].combinator;
        const struct yetty_ysvg_css_compound *prev = &cx->steps[i - 1].compound;
        switch (comb) {
        case YETTY_YSVG_CSS_DESCENDANT: {
            /* Some ancestor must match `prev`. */
            int found = 0;
            const struct yetty_ysvg_node *p = cursor->parent;
            while (p) {
                if (compound_matches(prev, p)) {
                    cursor = p;
                    found = 1;
                    break;
                }
                p = p->parent;
            }
            if (!found) return 0;
            break;
        }
        case YETTY_YSVG_CSS_CHILD: {
            const struct yetty_ysvg_node *p = cursor->parent;
            if (!p || !compound_matches(prev, p)) return 0;
            cursor = p;
            break;
        }
        case YETTY_YSVG_CSS_ADJACENT: {
            /* Immediately preceding sibling must match. */
            const struct yetty_ysvg_node *parent = cursor->parent;
            if (!parent) return 0;
            const struct yetty_ysvg_node *prev_sibling = NULL;
            for (struct yetty_ysvg_node *c = parent->first_child; c && c != cursor;
                 c = c->next_sibling) {
                prev_sibling = c;
            }
            if (!prev_sibling || !compound_matches(prev, prev_sibling)) return 0;
            cursor = prev_sibling;
            break;
        }
        case YETTY_YSVG_CSS_SIBLING: {
            const struct yetty_ysvg_node *parent = cursor->parent;
            if (!parent) return 0;
            int found = 0;
            for (struct yetty_ysvg_node *c = parent->first_child; c && c != cursor;
                 c = c->next_sibling) {
                if (compound_matches(prev, c)) {
                    cursor = c;
                    found = 1;
                    /* Stop at first match for left-side anchor — we don't
                     * need to backtrack since downstream combinators
                     * don't depend on which one we picked. */
                    break;
                }
            }
            if (!found) return 0;
            break;
        }
        }
    }
    return 1;
}

/*=============================================================================
 * Apply
 *===========================================================================*/

struct ysvg_css_match {
    uint32_t key;             /* (spec_a<<24)|(spec_b<<16)|spec_c|... packed */
    uint16_t spec_a, spec_b, spec_c;
    uint32_t order;
    const struct yetty_ysvg_css_rule *rule;
};

static int match_cmp_norm(const void *a_, const void *b_)
{
    const struct ysvg_css_match *a = a_;
    const struct ysvg_css_match *b = b_;
    if (a->spec_a != b->spec_a) return (a->spec_a < b->spec_a) ? -1 : 1;
    if (a->spec_b != b->spec_b) return (a->spec_b < b->spec_b) ? -1 : 1;
    if (a->spec_c != b->spec_c) return (a->spec_c < b->spec_c) ? -1 : 1;
    if (a->order != b->order) return (a->order < b->order) ? -1 : 1;
    return 0;
}

static void apply_one_rule(struct yetty_ysvg_style *out, const struct yetty_ysvg_css_rule *r,
                           int only_important)
{
    for (size_t i = 0; i < r->decl_count; i++) {
        const struct yetty_ysvg_css_decl *d = &r->decls[i];
        if (only_important && !d->important) continue;
        if (!only_important && d->important) continue;
        yetty_ysvg_style_apply_property(out, d->property, strlen(d->property), d->value,
                                         strlen(d->value));
    }
}

void yetty_ysvg_css_apply(struct yetty_ysvg_style *out, const struct yetty_ysvg_doc *doc,
                          const struct yetty_ysvg_node *node)
{
    if (!doc || doc->rule_count == 0) return;
    struct ysvg_css_match *matches = malloc(doc->rule_count * sizeof(*matches));
    if (!matches) return;
    size_t mcount = 0;
    for (size_t i = 0; i < doc->rule_count; i++) {
        const struct yetty_ysvg_css_rule *r = &doc->rules[i];
        uint16_t bA = 0, bB = 0, bC = 0;
        int hit = 0;
        for (size_t j = 0; j < r->selector_count; j++) {
            const struct yetty_ysvg_css_complex *cx = &r->selectors[j];
            if (complex_matches(cx, node)) {
                hit = 1;
                /* Pick the highest-specificity matching selector. */
                if (cx->spec_a > bA ||
                    (cx->spec_a == bA && cx->spec_b > bB) ||
                    (cx->spec_a == bA && cx->spec_b == bB && cx->spec_c > bC)) {
                    bA = cx->spec_a;
                    bB = cx->spec_b;
                    bC = cx->spec_c;
                }
            }
        }
        if (hit) {
            matches[mcount].spec_a = bA;
            matches[mcount].spec_b = bB;
            matches[mcount].spec_c = bC;
            matches[mcount].order = r->order;
            matches[mcount].rule = r;
            mcount++;
        }
    }
    qsort(matches, mcount, sizeof(*matches), match_cmp_norm);
    /* First pass: non-!important in ascending specificity. */
    for (size_t i = 0; i < mcount; i++) {
        apply_one_rule(out, matches[i].rule, 0);
    }
    /* Second pass: !important in ascending specificity — these win over
     * any non-important value already applied. (CSS importance order
     * actually has the !important block sit ABOVE inline non-important,
     * but the call site applies inline style after CSS, then we re-apply
     * !important last; see ysvg-style.c.) */
    for (size_t i = 0; i < mcount; i++) {
        apply_one_rule(out, matches[i].rule, 1);
    }
    free(matches);
}
