/*
 * ylottie-json.c — a small, self-contained recursive-descent JSON parser.
 *
 * Lottie documents are JSON. Rather than pull in a dependency, ylottie parses
 * JSON itself (the same way ysvg rolls its own CSS/attribute parsers). The
 * parser is strict enough for real Bodymovin output and tolerant of trailing
 * whitespace; it rejects malformed input with a byte-offset diagnostic.
 *
 * Memory: every node and every unescaped string lives in a bump arena owned
 * by the struct yetty_ylottie_doc. Container children form a singly-linked
 * list. Destroying the doc frees the whole arena in O(chunks).
 */

#include "ylottie-internal.h"

#include <yetty/ycore/result.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YLOTTIE_JSON_CHUNK_BYTES (64u * 1024u)
#define YLOTTIE_JSON_MAX_DEPTH 256
#define YLOTTIE_JSON_NUM_BUF 64

/*=============================================================================
 * Arena
 *===========================================================================*/

struct yetty_ylottie_json_chunk {
    struct yetty_ylottie_json_chunk *next;
    size_t used;
    size_t cap;
    unsigned char *data;
};

static size_t round_up(size_t v, size_t align)
{
    size_t rem = v % align;
    return rem ? v + (align - rem) : v;
}

static void *arena_alloc(struct yetty_ylottie_doc *doc, size_t size, size_t align)
{
    struct yetty_ylottie_json_chunk *chunk = doc->chunks;
    if (chunk) {
        size_t aligned = round_up(chunk->used, align);
        if (aligned + size <= chunk->cap) {
            void *ptr = chunk->data + aligned;
            chunk->used = aligned + size;
            return ptr;
        }
    }
    size_t want = size + align;
    size_t cap = want > YLOTTIE_JSON_CHUNK_BYTES ? want : YLOTTIE_JSON_CHUNK_BYTES;
    struct yetty_ylottie_json_chunk *fresh = calloc(1, sizeof(*fresh));
    if (!fresh) {
        return NULL;
    }
    fresh->data = malloc(cap);
    if (!fresh->data) {
        free(fresh);
        return NULL;
    }
    fresh->cap = cap;
    size_t aligned = round_up(0, align);
    void *ptr = fresh->data + aligned;
    fresh->used = aligned + size;
    fresh->next = doc->chunks;
    doc->chunks = fresh;
    return ptr;
}

static struct yetty_ylottie_json *node_alloc(struct yetty_ylottie_doc *doc)
{
    struct yetty_ylottie_json *node =
        arena_alloc(doc, sizeof(struct yetty_ylottie_json), sizeof(void *));
    if (node) {
        memset(node, 0, sizeof(*node));
    }
    return node;
}

static void append_child(struct yetty_ylottie_json *parent, struct yetty_ylottie_json *child)
{
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
    parent->child_count++;
}

/*=============================================================================
 * Parser state
 *===========================================================================*/

struct ylottie_json_parser {
    const char *s;
    size_t len;
    size_t pos;
    struct yetty_ylottie_doc *doc;
    int oom;
    const char *err; /* static message, NULL on success */
    size_t err_pos;
};

static void parser_fail(struct ylottie_json_parser *p, const char *msg)
{
    if (!p->err) {
        p->err = msg;
        p->err_pos = p->pos;
    }
}

static void skip_ws(struct ylottie_json_parser *p)
{
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

/* Append the UTF-8 encoding of `cp` to the stack buffer at *out (advancing
 * it). `cap` is the bytes remaining; returns 0 on success, -1 if it would
 * overflow. */
static int utf8_encode(uint32_t cp, char **out, size_t *cap)
{
    char *o = *out;
    if (cp < 0x80u) {
        if (*cap < 1) {
            return -1;
        }
        *o++ = (char)cp;
        *cap -= 1;
    } else if (cp < 0x800u) {
        if (*cap < 2) {
            return -1;
        }
        *o++ = (char)(0xC0u | (cp >> 6));
        *o++ = (char)(0x80u | (cp & 0x3Fu));
        *cap -= 2;
    } else if (cp < 0x10000u) {
        if (*cap < 3) {
            return -1;
        }
        *o++ = (char)(0xE0u | (cp >> 12));
        *o++ = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        *o++ = (char)(0x80u | (cp & 0x3Fu));
        *cap -= 3;
    } else {
        if (*cap < 4) {
            return -1;
        }
        *o++ = (char)(0xF0u | (cp >> 18));
        *o++ = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        *o++ = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        *o++ = (char)(0x80u | (cp & 0x3Fu));
        *cap -= 4;
    }
    *out = o;
    return 0;
}

static int read_hex4(struct ylottie_json_parser *p, uint32_t *out)
{
    if (p->pos + 4 > p->len) {
        return -1;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p->s[p->pos + (size_t)i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (uint32_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (uint32_t)(c - 'A' + 10);
        } else {
            return -1;
        }
    }
    p->pos += 4;
    *out = v;
    return 0;
}

/* Parse a JSON string starting at the opening quote. Writes an arena-owned
 * NUL-terminated copy to *out_str and its length to *out_len. We decode in
 * two passes is overkill; instead we copy into a growable temp on the heap.
 * Strings in Lottie are short, so a 256-byte stack buffer with a heap spill
 * keeps the common path allocation-free. */
static int parse_string_raw(struct ylottie_json_parser *p, const char **out_str, size_t *out_len)
{
    if (p->pos >= p->len || p->s[p->pos] != '"') {
        parser_fail(p, "expected string");
        return -1;
    }
    p->pos++; /* opening quote */

    char stackbuf[256];
    char *buf = stackbuf;
    size_t cap = sizeof(stackbuf);
    size_t n = 0;
    int heap = 0;

#define ENSURE(extra)                                                                              \
    do {                                                                                           \
        if (n + (extra) > cap) {                                                                   \
            size_t ncap = cap * 2;                                                                 \
            while (n + (extra) > ncap) {                                                           \
                ncap *= 2;                                                                         \
            }                                                                                      \
            char *nb = heap ? realloc(buf, ncap) : malloc(ncap);                                   \
            if (!nb) {                                                                             \
                if (heap) {                                                                        \
                    free(buf);                                                                     \
                }                                                                                  \
                p->oom = 1;                                                                        \
                return -1;                                                                         \
            }                                                                                      \
            if (!heap) {                                                                           \
                memcpy(nb, stackbuf, n);                                                           \
            }                                                                                      \
            buf = nb;                                                                              \
            cap = ncap;                                                                            \
            heap = 1;                                                                              \
        }                                                                                          \
    } while (0)

    while (p->pos < p->len) {
        char c = p->s[p->pos++];
        if (c == '"') {
            /* done */
            char *dst = arena_alloc(p->doc, n + 1, 1);
            if (!dst) {
                if (heap) {
                    free(buf);
                }
                p->oom = 1;
                return -1;
            }
            memcpy(dst, buf, n);
            dst[n] = '\0';
            if (heap) {
                free(buf);
            }
            *out_str = dst;
            *out_len = n;
            return 0;
        }
        if (c == '\\') {
            if (p->pos >= p->len) {
                break;
            }
            char esc = p->s[p->pos++];
            char decoded = 0;
            switch (esc) {
            case '"':
                decoded = '"';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '/':
                decoded = '/';
                break;
            case 'b':
                decoded = '\b';
                break;
            case 'f':
                decoded = '\f';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            case 'u': {
                uint32_t cp;
                if (read_hex4(p, &cp) != 0) {
                    parser_fail(p, "bad \\u escape");
                    if (heap) {
                        free(buf);
                    }
                    return -1;
                }
                if (cp >= 0xD800u && cp <= 0xDBFFu) {
                    /* high surrogate — expect a low surrogate. */
                    if (p->pos + 1 < p->len && p->s[p->pos] == '\\' && p->s[p->pos + 1] == 'u') {
                        p->pos += 2;
                        uint32_t lo;
                        if (read_hex4(p, &lo) == 0 && lo >= 0xDC00u && lo <= 0xDFFFu) {
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        } else {
                            cp = 0xFFFDu; /* replacement */
                        }
                    } else {
                        cp = 0xFFFDu;
                    }
                } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                    cp = 0xFFFDu; /* lone low surrogate */
                }
                char enc[4];
                char *ep = enc;
                size_t ecap = sizeof(enc);
                if (utf8_encode(cp, &ep, &ecap) != 0) {
                    cp = 0xFFFDu; /* unreachable for valid cp */
                }
                size_t elen = (size_t)(ep - enc);
                ENSURE(elen);
                memcpy(buf + n, enc, elen);
                n += elen;
                continue;
            }
            default:
                parser_fail(p, "bad escape");
                if (heap) {
                    free(buf);
                }
                return -1;
            }
            ENSURE(1);
            buf[n++] = decoded;
            continue;
        }
        ENSURE(1);
        buf[n++] = c;
    }

#undef ENSURE
    if (heap) {
        free(buf);
    }
    parser_fail(p, "unterminated string");
    return -1;
}

static int parse_number(struct ylottie_json_parser *p, struct yetty_ylottie_json *node)
{
    size_t start = p->pos;
    if (p->pos < p->len && (p->s[p->pos] == '-' || p->s[p->pos] == '+')) {
        p->pos++;
    }
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            p->pos++;
        } else {
            break;
        }
    }
    size_t span = p->pos - start;
    if (span == 0) {
        parser_fail(p, "expected number");
        return -1;
    }
    char buf[YLOTTIE_JSON_NUM_BUF];
    if (span >= sizeof(buf)) {
        span = sizeof(buf) - 1; /* pathologically long literal — clamp */
    }
    memcpy(buf, p->s + start, span);
    buf[span] = '\0';
    char *end = NULL;
    double v = strtod(buf, &end);
    if (end == buf) {
        parser_fail(p, "malformed number");
        return -1;
    }
    node->type = YETTY_YLOTTIE_JSON_NUMBER;
    node->number = v;
    return 0;
}

static int match_literal(struct ylottie_json_parser *p, const char *word)
{
    size_t wl = strlen(word);
    if (p->pos + wl > p->len || memcmp(p->s + p->pos, word, wl) != 0) {
        return -1;
    }
    p->pos += wl;
    return 0;
}

static int parse_value(struct ylottie_json_parser *p, struct yetty_ylottie_json *node, int depth);

static int parse_array(struct ylottie_json_parser *p, struct yetty_ylottie_json *node, int depth)
{
    node->type = YETTY_YLOTTIE_JSON_ARRAY;
    p->pos++; /* '[' */
    skip_ws(p);
    if (p->pos < p->len && p->s[p->pos] == ']') {
        p->pos++;
        return 0;
    }
    for (;;) {
        struct yetty_ylottie_json *child = node_alloc(p->doc);
        if (!child) {
            p->oom = 1;
            return -1;
        }
        if (parse_value(p, child, depth + 1) != 0) {
            return -1;
        }
        append_child(node, child);
        skip_ws(p);
        if (p->pos >= p->len) {
            parser_fail(p, "unterminated array");
            return -1;
        }
        char c = p->s[p->pos++];
        if (c == ',') {
            skip_ws(p);
            continue;
        }
        if (c == ']') {
            return 0;
        }
        parser_fail(p, "expected ',' or ']'");
        return -1;
    }
}

static int parse_object(struct ylottie_json_parser *p, struct yetty_ylottie_json *node, int depth)
{
    node->type = YETTY_YLOTTIE_JSON_OBJECT;
    p->pos++; /* '{' */
    skip_ws(p);
    if (p->pos < p->len && p->s[p->pos] == '}') {
        p->pos++;
        return 0;
    }
    for (;;) {
        skip_ws(p);
        const char *key = NULL;
        size_t key_len = 0;
        if (parse_string_raw(p, &key, &key_len) != 0) {
            return -1;
        }
        skip_ws(p);
        if (p->pos >= p->len || p->s[p->pos] != ':') {
            parser_fail(p, "expected ':'");
            return -1;
        }
        p->pos++; /* ':' */
        struct yetty_ylottie_json *child = node_alloc(p->doc);
        if (!child) {
            p->oom = 1;
            return -1;
        }
        if (parse_value(p, child, depth + 1) != 0) {
            return -1;
        }
        child->key = key;
        child->key_len = key_len;
        append_child(node, child);
        skip_ws(p);
        if (p->pos >= p->len) {
            parser_fail(p, "unterminated object");
            return -1;
        }
        char c = p->s[p->pos++];
        if (c == ',') {
            continue;
        }
        if (c == '}') {
            return 0;
        }
        parser_fail(p, "expected ',' or '}'");
        return -1;
    }
}

static int parse_value(struct ylottie_json_parser *p, struct yetty_ylottie_json *node, int depth)
{
    if (depth > YLOTTIE_JSON_MAX_DEPTH) {
        parser_fail(p, "nesting too deep");
        return -1;
    }
    skip_ws(p);
    if (p->pos >= p->len) {
        parser_fail(p, "unexpected end of input");
        return -1;
    }
    char c = p->s[p->pos];
    switch (c) {
    case '{':
        return parse_object(p, node, depth);
    case '[':
        return parse_array(p, node, depth);
    case '"': {
        const char *str = NULL;
        size_t slen = 0;
        if (parse_string_raw(p, &str, &slen) != 0) {
            return -1;
        }
        node->type = YETTY_YLOTTIE_JSON_STRING;
        node->string = str;
        node->string_len = slen;
        return 0;
    }
    case 't':
        if (match_literal(p, "true") == 0) {
            node->type = YETTY_YLOTTIE_JSON_BOOL;
            node->boolean = true;
            return 0;
        }
        parser_fail(p, "invalid literal");
        return -1;
    case 'f':
        if (match_literal(p, "false") == 0) {
            node->type = YETTY_YLOTTIE_JSON_BOOL;
            node->boolean = false;
            return 0;
        }
        parser_fail(p, "invalid literal");
        return -1;
    case 'n':
        if (match_literal(p, "null") == 0) {
            node->type = YETTY_YLOTTIE_JSON_NULL;
            return 0;
        }
        parser_fail(p, "invalid literal");
        return -1;
    default:
        if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
            return parse_number(p, node);
        }
        parser_fail(p, "unexpected character");
        return -1;
    }
}

/*=============================================================================
 * Public entry
 *===========================================================================*/

void yetty_ylottie_doc_destroy(struct yetty_ylottie_doc *doc)
{
    if (!doc) {
        return;
    }
    struct yetty_ylottie_json_chunk *chunk = doc->chunks;
    while (chunk) {
        struct yetty_ylottie_json_chunk *next = chunk->next;
        free(chunk->data);
        free(chunk);
        chunk = next;
    }
    free(doc);
}

struct yetty_ylottie_doc_ptr_result yetty_ylottie_json_parse(const char *src, size_t len)
{
    if (!src && len > 0) {
        return YETTY_ERR(yetty_ylottie_doc_ptr, "ylottie-json: NULL src with len > 0");
    }
    struct yetty_ylottie_doc *doc = calloc(1, sizeof(*doc));
    if (!doc) {
        return YETTY_ERR(yetty_ylottie_doc_ptr, "ylottie-json: out of memory");
    }

    struct ylottie_json_parser p = {.s = src, .len = len, .pos = 0, .doc = doc};
    struct yetty_ylottie_json *root = node_alloc(doc);
    if (!root) {
        yetty_ylottie_doc_destroy(doc);
        return YETTY_ERR(yetty_ylottie_doc_ptr, "ylottie-json: out of memory");
    }
    if (parse_value(&p, root, 0) != 0) {
        yetty_ylottie_doc_destroy(doc);
        if (p.oom) {
            return YETTY_ERR(yetty_ylottie_doc_ptr, "ylottie-json: out of memory");
        }
        return YETTY_ERR(yetty_ylottie_doc_ptr, p.err ? p.err : "ylottie-json: parse failed");
    }
    skip_ws(&p);
    if (p.pos != len) {
        /* Trailing non-whitespace after the top-level value. */
        yetty_ylottie_doc_destroy(doc);
        return YETTY_ERR(yetty_ylottie_doc_ptr, "ylottie-json: trailing garbage after document");
    }
    doc->root = root;
    return YETTY_OK(yetty_ylottie_doc_ptr, doc);
}

/*=============================================================================
 * Accessors
 *===========================================================================*/

const struct yetty_ylottie_json *yetty_ylottie_json_get(const struct yetty_ylottie_json *obj,
                                                        const char *key)
{
    if (!obj || obj->type != YETTY_YLOTTIE_JSON_OBJECT || !key) {
        return NULL;
    }
    size_t klen = strlen(key);
    for (const struct yetty_ylottie_json *m = obj->first_child; m; m = m->next_sibling) {
        if (m->key && m->key_len == klen && memcmp(m->key, key, klen) == 0) {
            return m;
        }
    }
    return NULL;
}

const struct yetty_ylottie_json *yetty_ylottie_json_at(const struct yetty_ylottie_json *arr,
                                                       size_t index)
{
    if (!arr || arr->type != YETTY_YLOTTIE_JSON_ARRAY) {
        return NULL;
    }
    size_t i = 0;
    for (const struct yetty_ylottie_json *e = arr->first_child; e; e = e->next_sibling, i++) {
        if (i == index) {
            return e;
        }
    }
    return NULL;
}

double yetty_ylottie_json_num(const struct yetty_ylottie_json *node, double fallback)
{
    if (!node || node->type != YETTY_YLOTTIE_JSON_NUMBER) {
        return fallback;
    }
    return node->number;
}

double yetty_ylottie_json_num_at(const struct yetty_ylottie_json *node, size_t index,
                                 double fallback)
{
    return yetty_ylottie_json_num(yetty_ylottie_json_at(node, index), fallback);
}

double yetty_ylottie_json_num_key(const struct yetty_ylottie_json *obj, const char *key,
                                  double fallback)
{
    return yetty_ylottie_json_num(yetty_ylottie_json_get(obj, key), fallback);
}
