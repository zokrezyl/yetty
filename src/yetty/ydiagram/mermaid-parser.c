/*
 * mermaid-parser.c — Mermaid flowchart syntax → graph IR.
 *
 * Hand-rolled recursive descent over line-oriented input. Mermaid is small
 * enough that no formal grammar is needed. Compared with the C++ POC the
 * shape detection order is preserved exactly so identical inputs produce
 * identical IRs.
 *
 * The parser does not allocate the graph — callers pass an already-init'd
 * `struct yetty_ydiagram_graph *` and we append to it. Malformed lines are
 * skipped rather than aborting the whole parse.
 */

#include <yetty/ydiagram/mermaid-parser.h>

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>

/*=============================================================================
 * String slice utilities
 *===========================================================================*/

struct slice {
    const char *p;
    size_t n;
};

static struct slice slice_make(const char *p, size_t n)
{
    return (struct slice){p, n};
}

static struct slice slice_trim(struct slice s)
{
    while (s.n && (s.p[0] == ' ' || s.p[0] == '\t' || s.p[0] == '\r' || s.p[0] == '\n')) {
        s.p++;
        s.n--;
    }
    while (s.n && (s.p[s.n - 1] == ' ' || s.p[s.n - 1] == '\t' || s.p[s.n - 1] == '\r' ||
                   s.p[s.n - 1] == '\n')) {
        s.n--;
    }
    return s;
}

static bool slice_starts_with(struct slice s, const char *prefix)
{
    size_t pn = strlen(prefix);
    return s.n >= pn && memcmp(s.p, prefix, pn) == 0;
}

static bool slice_equals_cstr(struct slice s, const char *cstr)
{
    size_t cn = strlen(cstr);
    return s.n == cn && memcmp(s.p, cstr, cn) == 0;
}

/* Find first occurrence of needle in haystack slice. Returns offset or
 * SIZE_MAX if not found. */
static size_t slice_find(struct slice hay, const char *needle, size_t start)
{
    size_t nl = strlen(needle);
    if (nl == 0 || nl > hay.n || start > hay.n - nl) {
        return (size_t)-1;
    }
    for (size_t i = start; i + nl <= hay.n; i++) {
        if (memcmp(hay.p + i, needle, nl) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

static char *slice_to_cstr(struct slice s)
{
    char *out = calloc(s.n + 1, 1);
    if (!out) {
        return NULL;
    }
    if (s.n) {
        memcpy(out, s.p, s.n);
    }
    return out;
}

/* Find last occurrence of needle in haystack slice. Returns offset or
 * SIZE_MAX if not found. */
static size_t slice_find_last(struct slice hay, const char *needle)
{
    size_t best = (size_t)-1;
    size_t from = 0;
    for (;;) {
        size_t pos = slice_find(hay, needle, from);
        if (pos == (size_t)-1) {
            return best;
        }
        best = pos;
        from = pos + 1;
    }
}

static bool slice_char_is_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

/* First / last whitespace offset within the slice, or SIZE_MAX if none. */
static size_t slice_find_whitespace(struct slice s)
{
    for (size_t i = 0; i < s.n; i++) {
        if (slice_char_is_space(s.p[i])) {
            return i;
        }
    }
    return (size_t)-1;
}

static size_t slice_find_last_whitespace(struct slice s)
{
    for (size_t i = s.n; i-- > 0;) {
        if (slice_char_is_space(s.p[i])) {
            return i;
        }
    }
    return (size_t)-1;
}

/* [A-Za-z0-9_-]+ — the shape of a Mermaid class name. */
static bool slice_is_identifier(struct slice s)
{
    if (s.n == 0) {
        return false;
    }
    for (size_t i = 0; i < s.n; i++) {
        char ch = s.p[i];
        if (!isalnum((unsigned char)ch) && ch != '_' && ch != '-') {
            return false;
        }
    }
    return true;
}

/*=============================================================================
 * Node styling — classDef / class / style directives and the ::: shorthand
 *
 * Mermaid allows `classDef` to appear after the nodes (and `class` lines)
 * that reference it, so directives are collected during the line walk and
 * resolved against the graph once the whole input has been parsed.
 *===========================================================================*/

/* Subset of a node style a directive may override; only the fields named in
 * the directive's key:value list are applied. */
struct style_patch {
    bool has_fill_color;
    uint32_t fill_color;
    bool has_stroke_color;
    uint32_t stroke_color;
    bool has_text_color;
    uint32_t text_color;
    bool has_stroke_width;
    float stroke_width;
};

struct class_def_entry {
    char *name; /* heap */
    struct style_patch patch;
};

struct style_assign_entry {
    char *node_id;    /* heap */
    char *class_name; /* heap; NULL → apply `patch` directly (a `style` line) */
    struct style_patch patch;
};

struct pending_styles {
    struct class_def_entry *class_defs;
    size_t class_def_count;
    size_t class_def_capacity;
    struct style_assign_entry *assigns;
    size_t assign_count;
    size_t assign_capacity;
};

static void pending_styles_destroy(struct pending_styles *pending)
{
    for (size_t i = 0; i < pending->class_def_count; i++) {
        free(pending->class_defs[i].name);
    }
    free(pending->class_defs);
    for (size_t i = 0; i < pending->assign_count; i++) {
        free(pending->assigns[i].node_id);
        free(pending->assigns[i].class_name);
    }
    free(pending->assigns);
    memset(pending, 0, sizeof(*pending));
}

/* Colors pack as ydraw RGBA — R in the low byte, A in the high byte. */
static uint32_t pack_rgba(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha)
{
    return (alpha << 24) | (blue << 16) | (green << 8) | red;
}

static int hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

/* #rgb / #rrggbb / #rrggbbaa hex forms plus the handful of CSS color names
 * that show up in real-world Mermaid. Returns false for anything else; the
 * caller then skips that key. */
static bool parse_color_value(struct slice value, uint32_t *color_out)
{
    value = slice_trim(value);
    if (value.n == 0) {
        return false;
    }
    if (value.p[0] == '#') {
        struct slice hex = slice_make(value.p + 1, value.n - 1);
        if (hex.n != 3 && hex.n != 6 && hex.n != 8) {
            return false;
        }
        int nibbles[8];
        for (size_t i = 0; i < hex.n; i++) {
            nibbles[i] = hex_nibble(hex.p[i]);
            if (nibbles[i] < 0) {
                return false;
            }
        }
        if (hex.n == 3) {
            *color_out = pack_rgba((uint32_t)nibbles[0] * 17u, (uint32_t)nibbles[1] * 17u,
                                   (uint32_t)nibbles[2] * 17u, 0xFFu);
            return true;
        }
        uint32_t red = (uint32_t)(nibbles[0] * 16 + nibbles[1]);
        uint32_t green = (uint32_t)(nibbles[2] * 16 + nibbles[3]);
        uint32_t blue = (uint32_t)(nibbles[4] * 16 + nibbles[5]);
        uint32_t alpha = (hex.n == 8) ? (uint32_t)(nibbles[6] * 16 + nibbles[7]) : 0xFFu;
        *color_out = pack_rgba(red, green, blue, alpha);
        return true;
    }

    char name[24];
    if (value.n >= sizeof(name)) {
        return false;
    }
    for (size_t i = 0; i < value.n; i++) {
        name[i] = (char)tolower((unsigned char)value.p[i]);
    }
    name[value.n] = 0;

    static const struct {
        const char *name;
        uint8_t red, green, blue, alpha;
    } k_named_colors[] = {
        {"none", 0x00, 0x00, 0x00, 0x00},      {"transparent", 0x00, 0x00, 0x00, 0x00},
        {"white", 0xFF, 0xFF, 0xFF, 0xFF},     {"black", 0x00, 0x00, 0x00, 0xFF},
        {"red", 0xFF, 0x00, 0x00, 0xFF},       {"green", 0x00, 0x80, 0x00, 0xFF},
        {"lime", 0x00, 0xFF, 0x00, 0xFF},      {"blue", 0x00, 0x00, 0xFF, 0xFF},
        {"yellow", 0xFF, 0xFF, 0x00, 0xFF},    {"orange", 0xFF, 0xA5, 0x00, 0xFF},
        {"purple", 0x80, 0x00, 0x80, 0xFF},    {"pink", 0xFF, 0xC0, 0xCB, 0xFF},
        {"gray", 0x80, 0x80, 0x80, 0xFF},      {"grey", 0x80, 0x80, 0x80, 0xFF},
        {"lightgray", 0xD3, 0xD3, 0xD3, 0xFF}, {"lightgrey", 0xD3, 0xD3, 0xD3, 0xFF},
        {"darkgray", 0xA9, 0xA9, 0xA9, 0xFF},  {"darkgrey", 0xA9, 0xA9, 0xA9, 0xFF},
        {"lightblue", 0xAD, 0xD8, 0xE6, 0xFF}, {"lightgreen", 0x90, 0xEE, 0x90, 0xFF},
        {"cyan", 0x00, 0xFF, 0xFF, 0xFF},      {"magenta", 0xFF, 0x00, 0xFF, 0xFF},
        {"brown", 0xA5, 0x2A, 0x2A, 0xFF},
    };
    for (size_t i = 0; i < sizeof(k_named_colors) / sizeof(k_named_colors[0]); i++) {
        if (strcmp(name, k_named_colors[i].name) == 0) {
            *color_out = pack_rgba(k_named_colors[i].red, k_named_colors[i].green,
                                   k_named_colors[i].blue, k_named_colors[i].alpha);
            return true;
        }
    }
    return false;
}

/* `fill:#20242b,stroke:#6ee7b7,color:#f8fafc,stroke-width:2px` → patch.
 * Keys that don't map onto the node style (stroke-dasharray, …) are
 * skipped. */
static void parse_style_list(struct slice list, struct style_patch *patch)
{
    memset(patch, 0, sizeof(*patch));
    while (list.n > 0) {
        size_t comma = slice_find(list, ",", 0);
        struct slice item = (comma == (size_t)-1) ? list : slice_make(list.p, comma);
        list = (comma == (size_t)-1) ? slice_make(NULL, 0)
                                     : slice_make(list.p + comma + 1, list.n - comma - 1);
        item = slice_trim(item);
        if (item.n == 0) {
            continue;
        }
        size_t colon = slice_find(item, ":", 0);
        if (colon == (size_t)-1) {
            continue;
        }
        struct slice key = slice_trim(slice_make(item.p, colon));
        struct slice value = slice_trim(slice_make(item.p + colon + 1, item.n - colon - 1));
        if (slice_equals_cstr(key, "fill")) {
            patch->has_fill_color = parse_color_value(value, &patch->fill_color);
        } else if (slice_equals_cstr(key, "stroke")) {
            patch->has_stroke_color = parse_color_value(value, &patch->stroke_color);
        } else if (slice_equals_cstr(key, "color")) {
            patch->has_text_color = parse_color_value(value, &patch->text_color);
        } else if (slice_equals_cstr(key, "stroke-width")) {
            char width_text[16];
            if (value.n < sizeof(width_text)) {
                memcpy(width_text, value.p, value.n);
                width_text[value.n] = 0;
                char *end = NULL;
                float width = strtof(width_text, &end); /* trailing "px" is ignored */
                if (end != width_text && width >= 0.0f) {
                    patch->stroke_width = width;
                    patch->has_stroke_width = true;
                }
            }
        }
    }
}

static struct yetty_ycore_void_result pending_styles_add_class_def(struct pending_styles *pending,
                                                                   struct slice name,
                                                                   const struct style_patch *patch)
{
    if (pending->class_def_count == pending->class_def_capacity) {
        size_t new_capacity = pending->class_def_capacity ? pending->class_def_capacity * 2 : 4;
        struct class_def_entry *grown =
            realloc(pending->class_defs, new_capacity * sizeof(struct class_def_entry));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "pending_styles_add_class_def: realloc");
        }
        pending->class_defs = grown;
        pending->class_def_capacity = new_capacity;
    }
    char *name_z = slice_to_cstr(name);
    if (!name_z) {
        return YETTY_ERR(yetty_ycore_void, "pending_styles_add_class_def: slice_to_cstr");
    }
    pending->class_defs[pending->class_def_count] =
        (struct class_def_entry){.name = name_z, .patch = *patch};
    pending->class_def_count++;
    return YETTY_OK_VOID();
}

/* Records `node_id:::class_name` when class_name is non-empty, or a direct
 * patch (a `style` line) when it is empty. */
static struct yetty_ycore_void_result pending_styles_add_assign(struct pending_styles *pending,
                                                                struct slice node_id,
                                                                struct slice class_name,
                                                                const struct style_patch *patch)
{
    if (pending->assign_count == pending->assign_capacity) {
        size_t new_capacity = pending->assign_capacity ? pending->assign_capacity * 2 : 4;
        struct style_assign_entry *grown =
            realloc(pending->assigns, new_capacity * sizeof(struct style_assign_entry));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "pending_styles_add_assign: realloc");
        }
        pending->assigns = grown;
        pending->assign_capacity = new_capacity;
    }
    char *node_id_z = slice_to_cstr(node_id);
    if (!node_id_z) {
        return YETTY_ERR(yetty_ycore_void, "pending_styles_add_assign: slice_to_cstr node id");
    }
    char *class_name_z = NULL;
    if (class_name.n > 0) {
        class_name_z = slice_to_cstr(class_name);
        if (!class_name_z) {
            free(node_id_z);
            return YETTY_ERR(yetty_ycore_void, "pending_styles_add_assign: slice_to_cstr class");
        }
    }
    struct style_assign_entry *entry = &pending->assigns[pending->assign_count];
    entry->node_id = node_id_z;
    entry->class_name = class_name_z;
    if (patch) {
        entry->patch = *patch;
    } else {
        memset(&entry->patch, 0, sizeof(entry->patch));
    }
    pending->assign_count++;
    return YETTY_OK_VOID();
}

/* `classDef name1[,name2…] key:value[,key:value…]` */
static struct yetty_ycore_void_result parse_class_def_directive(struct slice rest,
                                                                struct pending_styles *pending)
{
    rest = slice_trim(rest);
    size_t split = slice_find_whitespace(rest);
    if (split == (size_t)-1) {
        return YETTY_OK_VOID(); /* malformed — skip, like any bad line */
    }
    struct slice names = slice_make(rest.p, split);
    struct slice styles = slice_trim(slice_make(rest.p + split + 1, rest.n - split - 1));
    struct style_patch patch;
    parse_style_list(styles, &patch);
    while (names.n > 0) {
        size_t comma = slice_find(names, ",", 0);
        struct slice name = (comma == (size_t)-1) ? names : slice_make(names.p, comma);
        names = (comma == (size_t)-1) ? slice_make(NULL, 0)
                                      : slice_make(names.p + comma + 1, names.n - comma - 1);
        name = slice_trim(name);
        if (name.n == 0) {
            continue;
        }
        struct yetty_ycore_void_result add_res =
            pending_styles_add_class_def(pending, name, &patch);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "parse_class_def_directive: add");
    }
    return YETTY_OK_VOID();
}

/* `class node1[,node2…] className` */
static struct yetty_ycore_void_result parse_class_assign_directive(struct slice rest,
                                                                   struct pending_styles *pending)
{
    rest = slice_trim(rest);
    size_t split = slice_find_last_whitespace(rest);
    if (split == (size_t)-1) {
        return YETTY_OK_VOID();
    }
    struct slice node_list = slice_trim(slice_make(rest.p, split));
    struct slice class_name = slice_trim(slice_make(rest.p + split + 1, rest.n - split - 1));
    if (class_name.n == 0) {
        return YETTY_OK_VOID();
    }
    while (node_list.n > 0) {
        size_t comma = slice_find(node_list, ",", 0);
        struct slice node_id = (comma == (size_t)-1) ? node_list : slice_make(node_list.p, comma);
        node_list = (comma == (size_t)-1)
                        ? slice_make(NULL, 0)
                        : slice_make(node_list.p + comma + 1, node_list.n - comma - 1);
        node_id = slice_trim(node_id);
        if (node_id.n == 0) {
            continue;
        }
        struct yetty_ycore_void_result add_res =
            pending_styles_add_assign(pending, node_id, class_name, NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "parse_class_assign_directive: add");
    }
    return YETTY_OK_VOID();
}

/* `style nodeId key:value[,key:value…]` */
static struct yetty_ycore_void_result parse_style_directive(struct slice rest,
                                                            struct pending_styles *pending)
{
    rest = slice_trim(rest);
    size_t split = slice_find_whitespace(rest);
    if (split == (size_t)-1) {
        return YETTY_OK_VOID();
    }
    struct slice node_id = slice_make(rest.p, split);
    struct slice styles = slice_trim(slice_make(rest.p + split + 1, rest.n - split - 1));
    struct style_patch patch;
    parse_style_list(styles, &patch);
    struct yetty_ycore_void_result add_res =
        pending_styles_add_assign(pending, node_id, slice_make(NULL, 0), &patch);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "parse_style_directive: add");
    return YETTY_OK_VOID();
}

static void apply_style_patch(struct yetty_ydiagram_node *node, const struct style_patch *patch)
{
    if (patch->has_fill_color) {
        node->style.fill_color = patch->fill_color;
    }
    if (patch->has_stroke_color) {
        node->style.stroke_color = patch->stroke_color;
    }
    if (patch->has_text_color) {
        node->style.text_color = patch->text_color;
    }
    if (patch->has_stroke_width) {
        node->style.stroke_width = patch->stroke_width;
    }
}

/* Latest definition of a class name wins, matching Mermaid. */
static const struct style_patch *find_class_patch(const struct pending_styles *pending,
                                                  const char *name)
{
    for (size_t i = pending->class_def_count; i-- > 0;) {
        if (strcmp(pending->class_defs[i].name, name) == 0) {
            return &pending->class_defs[i].patch;
        }
    }
    return NULL;
}

static void apply_pending_styles(struct yetty_ydiagram_graph *g,
                                 const struct pending_styles *pending)
{
    /* Mermaid's `classDef default …` restyles every node; the explicit
     * assignments below then override per node. */
    const struct style_patch *default_patch = find_class_patch(pending, "default");
    if (default_patch) {
        for (size_t i = 0; i < g->node_count; i++) {
            apply_style_patch(&g->nodes[i], default_patch);
        }
    }
    for (size_t i = 0; i < pending->assign_count; i++) {
        const struct style_assign_entry *assign = &pending->assigns[i];
        struct yetty_ydiagram_node *node = yetty_ydiagram_graph_find_node(g, assign->node_id);
        if (!node) {
            continue; /* unknown target — skip, like any malformed line */
        }
        const struct style_patch *patch = &assign->patch;
        if (assign->class_name) {
            patch = find_class_patch(pending, assign->class_name);
            if (!patch) {
                continue;
            }
        }
        apply_style_patch(node, patch);
    }
}

/*=============================================================================
 * Shape detection — A[text], A((text)), A{text}, ...
 *
 * Returns true if a recognised shape suffix was found. `id` and `label`
 * are filled with slices into the input (no allocation). On `true` callers
 * must duplicate `id`/`label` before storing.
 *===========================================================================*/

struct node_def {
    struct slice id;
    struct slice label;
    enum yetty_ydiagram_node_shape shape;
    struct slice class_name; /* `:::className` shorthand; empty if none */
};

static bool try_open_close(struct slice tok, const char *open, const char *close,
                           enum yetty_ydiagram_node_shape shape, struct node_def *out)
{
    size_t op = slice_find(tok, open, 0);
    if (op == (size_t)-1) {
        return false;
    }
    size_t ol = strlen(open);
    size_t cp = slice_find(tok, close, op + ol);
    if (cp == (size_t)-1) {
        return false;
    }
    size_t cl = strlen(close);
    out->id = slice_make(tok.p, op);
    out->label = slice_make(tok.p + op + ol, cp - op - ol);
    out->shape = shape;
    return out->id.n > 0;
}

static bool parse_node_def(struct slice token, struct node_def *out)
{
    out->id = slice_make(NULL, 0);
    out->label = slice_make(NULL, 0);
    out->shape = YETTY_YDIAGRAM_SHAPE_RECTANGLE;
    out->class_name = slice_make(NULL, 0);
    if (token.n == 0) {
        return false;
    }

    /* `A[label]:::className` / `A:::className` — Mermaid's class shorthand.
     * Only strip when the suffix is a plain identifier so a `:::` inside a
     * label is left alone. */
    size_t class_marker = slice_find_last(token, ":::");
    if (class_marker != (size_t)-1) {
        struct slice suffix =
            slice_trim(slice_make(token.p + class_marker + 3, token.n - class_marker - 3));
        if (slice_is_identifier(suffix)) {
            out->class_name = suffix;
            token = slice_trim(slice_make(token.p, class_marker));
            if (token.n == 0) {
                return false;
            }
        }
    }

    /* Order matters — longer / more specific delimiters first. */
    if (try_open_close(token, "((", "))", YETTY_YDIAGRAM_SHAPE_CIRCLE, out)) {
        return true;
    }
    if (try_open_close(token, "{{", "}}", YETTY_YDIAGRAM_SHAPE_HEXAGON, out)) {
        return true;
    }
    if (try_open_close(token, "[(", ")]", YETTY_YDIAGRAM_SHAPE_CYLINDER, out)) {
        return true;
    }
    if (try_open_close(token, "([", "])", YETTY_YDIAGRAM_SHAPE_STADIUM, out)) {
        return true;
    }
    if (try_open_close(token, "[/", "/]", YETTY_YDIAGRAM_SHAPE_PARALLELOGRAM, out)) {
        return true;
    }
    if (try_open_close(token, "[\\", "\\]", YETTY_YDIAGRAM_SHAPE_PARALLELOGRAM, out)) {
        return true;
    }
    if (try_open_close(token, "[/", "\\]", YETTY_YDIAGRAM_SHAPE_TRAPEZOID, out)) {
        return true;
    }
    if (try_open_close(token, "{", "}", YETTY_YDIAGRAM_SHAPE_DIAMOND, out)) {
        return true;
    }
    if (try_open_close(token, "(", ")", YETTY_YDIAGRAM_SHAPE_ROUNDED_RECT, out)) {
        return true;
    }
    if (try_open_close(token, "[", "]", YETTY_YDIAGRAM_SHAPE_RECTANGLE, out)) {
        return true;
    }

    /* Bare id — no delimiter. */
    out->id = token;
    out->label = token;
    out->shape = YETTY_YDIAGRAM_SHAPE_RECTANGLE;
    return true;
}

/*=============================================================================
 * Arrow detection
 *===========================================================================*/

/* Sorted by length (longest first) so longer matches win. The original C++
 * iterated unsorted but picked the leftmost; that doesn't disambiguate
 * between "-->" and "-->|" at the same offset. We pick longest-at-leftmost,
 * which matches Mermaid's reference behaviour. */
static const char *const k_arrows[] = {
    "-.->", "==>>", "-->>", "-->|", "===|", "-.-|", "---|", "==>", "-->", "-.-", "===", "---",
};

static int arrow_find(struct slice line, size_t *out_pos, const char **out_arrow)
{
    size_t best_pos = (size_t)-1;
    const char *best_arrow = NULL;
    for (size_t i = 0; i < sizeof(k_arrows) / sizeof(k_arrows[0]); i++) {
        size_t pos = slice_find(line, k_arrows[i], 0);
        if (pos == (size_t)-1) {
            continue;
        }
        /* Prefer the leftmost; on tie prefer the longest (k_arrows is
         * already sorted longest-first). */
        if (best_pos == (size_t)-1 || pos < best_pos ||
            (pos == best_pos && strlen(k_arrows[i]) > strlen(best_arrow))) {
            best_pos = pos;
            best_arrow = k_arrows[i];
        }
    }
    if (best_pos == (size_t)-1) {
        return -1;
    }
    *out_pos = best_pos;
    *out_arrow = best_arrow;
    return 0;
}

static void apply_arrow_style(const char *arrow, struct yetty_ydiagram_edge_style *style)
{
    size_t len = strlen(arrow);
    const char *body = arrow;
    /* Drop a trailing '|' (label opener) for style decisions. */
    if (len > 0 && body[len - 1] == '|') {
        len--;
    }

    style->source_arrow = YETTY_YDIAGRAM_ARROW_NONE;
    style->target_arrow = YETTY_YDIAGRAM_ARROW_NONE;
    style->line_style = YETTY_YDIAGRAM_LINE_SOLID;

    bool has_dot = false;
    bool has_thick = false;
    for (size_t i = 0; i + 1 < len; i++) {
        if (body[i] == '-' && body[i + 1] == '.') {
            has_dot = true;
        }
        if (body[i] == '.' && body[i + 1] == '-') {
            has_dot = true;
        }
        if (body[i] == '=' && body[i + 1] == '=') {
            has_thick = true;
        }
    }
    if (has_dot) {
        style->line_style = YETTY_YDIAGRAM_LINE_DASHED;
    } else if (has_thick) {
        style->line_style = YETTY_YDIAGRAM_LINE_THICK;
        style->stroke_width = 3.0f;
    }

    for (size_t i = 0; i < len; i++) {
        if (body[i] == '>') {
            style->target_arrow = YETTY_YDIAGRAM_ARROW_NORMAL;
            break;
        }
    }
    if (len > 0 && body[0] == '<') {
        style->source_arrow = YETTY_YDIAGRAM_ARROW_NORMAL;
    }
}

/*=============================================================================
 * Top-level can_parse / parse
 *===========================================================================*/

bool yetty_ydiagram_mermaid_can_parse(const char *input, size_t len)
{
    if (!input) {
        return false;
    }
    /* Walk past blank / comment lines (`%% ...`) before sniffing the
     * first real line. Mermaid `.mmd` files in the wild commonly start
     * with `%% title` or similar metadata. */
    struct slice s = slice_make(input, len);
    while (s.n > 0) {
        size_t nl = slice_find(s, "\n", 0);
        struct slice line = (nl == (size_t)-1) ? s : slice_make(s.p, nl);
        line = slice_trim(line);
        if (line.n > 0 && line.p[0] != '%') {
            return slice_starts_with(line, "graph ") || slice_starts_with(line, "graph\t") ||
                   slice_equals_cstr(line, "graph") || slice_starts_with(line, "flowchart ") ||
                   slice_starts_with(line, "flowchart\t") || slice_equals_cstr(line, "flowchart");
        }
        if (nl == (size_t)-1) {
            break;
        }
        s = slice_make(s.p + nl + 1, s.n - nl - 1);
    }
    return false;
}

/* Ensure a node with `id` exists; creates it with `label`/`shape` if it
 * doesn't. Returns the node's *index* in g->nodes (stable across later
 * appends), or -1 on alloc failure. The pointer obtained via
 * `&g->nodes[idx]` is only valid until the next add_node/add_edge call. */
static struct yetty_ycore_int_result ensure_node_idx(struct yetty_ydiagram_graph *g,
                                                     struct slice id, struct slice label,
                                                     enum yetty_ydiagram_node_shape shape,
                                                     const char *current_subgraph)
{
    char *id_z = slice_to_cstr(id);
    if (!id_z) {
        return YETTY_ERR(yetty_ycore_int, "ensure_node_idx: slice_to_cstr id");
    }
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].id && strcmp(g->nodes[i].id, id_z) == 0) {
            free(id_z);
            return YETTY_OK(yetty_ycore_int, (int)i);
        }
    }
    char *lbl_z = slice_to_cstr(label);
    if (!lbl_z) {
        free(id_z);
        return YETTY_ERR(yetty_ycore_int, "ensure_node_idx: slice_to_cstr label");
    }
    struct yetty_ycore_int_result ar = yetty_ydiagram_graph_add_node(g, id_z, lbl_z, shape);
    free(lbl_z);
    if (YETTY_IS_ERR(ar)) {
        free(id_z);
        return YETTY_ERR(yetty_ycore_int, "ensure_node_idx: add_node", ar);
    }
    if (current_subgraph) {
        g->nodes[ar.value].cluster_id = strdup(current_subgraph);
        struct yetty_ydiagram_cluster *c = yetty_ydiagram_graph_find_cluster(g, current_subgraph);
        if (c) {
            struct yetty_ycore_void_result cr = yetty_ydiagram_cluster_add_node(c, id_z);
            if (YETTY_IS_ERR(cr)) {
                free(id_z);
                return YETTY_ERR(yetty_ycore_int, "ensure_node_idx: cluster_add_node", cr);
            }
        }
    }
    free(id_z);
    return YETTY_OK(yetty_ycore_int, ar.value);
}

static struct yetty_ycore_void_result parse_line(struct slice line, struct yetty_ydiagram_graph *g,
                                                 struct pending_styles *pending,
                                                 const char *current_subgraph)
{
    size_t arrow_pos = (size_t)-1;
    const char *arrow_str = NULL;

    if (arrow_find(line, &arrow_pos, &arrow_str) != 0) {
        /* Standalone node definition. */
        struct node_def nd;
        struct slice trimmed = slice_trim(line);
        if (parse_node_def(trimmed, &nd)) {
            struct yetty_ycore_int_result node_res =
                ensure_node_idx(g, nd.id, nd.label, nd.shape, current_subgraph);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, node_res, "parse_line: standalone node");
            if (nd.class_name.n > 0) {
                struct yetty_ycore_void_result assign_res =
                    pending_styles_add_assign(pending, nd.id, nd.class_name, NULL);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, assign_res, "parse_line: class shorthand");
            }
        }
        return YETTY_OK_VOID();
    }

    size_t arrow_len = strlen(arrow_str);

    struct slice source_part = slice_trim(slice_make(line.p, arrow_pos));
    struct slice after_arrow =
        slice_make(line.p + arrow_pos + arrow_len, line.n - arrow_pos - arrow_len);

    /* Edge label syntax: -->|label| or  -->| label | */
    struct slice edge_label = slice_make(NULL, 0);
    if (arrow_len > 0 && arrow_str[arrow_len - 1] == '|') {
        size_t end = slice_find(after_arrow, "|", 0);
        if (end != (size_t)-1) {
            edge_label = slice_make(after_arrow.p, end);
            after_arrow = slice_make(after_arrow.p + end + 1, after_arrow.n - end - 1);
        }
    } else if (after_arrow.n > 0 && after_arrow.p[0] == '|') {
        size_t end = slice_find(after_arrow, "|", 1);
        if (end != (size_t)-1) {
            edge_label = slice_make(after_arrow.p + 1, end - 1);
            after_arrow = slice_make(after_arrow.p + end + 1, after_arrow.n - end - 1);
        }
    }
    edge_label = slice_trim(edge_label);

    struct slice target_part = slice_trim(after_arrow);

    /* Source */
    struct node_def src_nd;
    if (!parse_node_def(source_part, &src_nd)) {
        src_nd.id = source_part;
        src_nd.label = source_part;
        src_nd.shape = YETTY_YDIAGRAM_SHAPE_RECTANGLE;
        src_nd.class_name = slice_make(NULL, 0);
    }
    struct yetty_ycore_int_result src_res =
        ensure_node_idx(g, src_nd.id, src_nd.label, src_nd.shape, current_subgraph);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, src_res, "parse_line: source node");
    int src_idx = src_res.value;
    if (src_nd.class_name.n > 0) {
        struct yetty_ycore_void_result assign_res =
            pending_styles_add_assign(pending, src_nd.id, src_nd.class_name, NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, assign_res, "parse_line: source class shorthand");
    }

    /* Detect a chained arrow in target_part: A --> B --> C. We pull out
     * the first target then recurse on `<first_target> <rest>`. */
    size_t chain_pos = (size_t)-1;
    const char *chain_arrow = NULL;
    (void)arrow_find(target_part, &chain_pos, &chain_arrow);

    struct slice first_target = target_part;
    if (chain_pos != (size_t)-1) {
        first_target = slice_trim(slice_make(target_part.p, chain_pos));
    }

    struct node_def tgt_nd;
    if (!parse_node_def(first_target, &tgt_nd)) {
        tgt_nd.id = first_target;
        tgt_nd.label = first_target;
        tgt_nd.shape = YETTY_YDIAGRAM_SHAPE_RECTANGLE;
        tgt_nd.class_name = slice_make(NULL, 0);
    }
    struct yetty_ycore_int_result tgt_res =
        ensure_node_idx(g, tgt_nd.id, tgt_nd.label, tgt_nd.shape, current_subgraph);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tgt_res, "parse_line: target node");
    int tgt_idx = tgt_res.value;
    if (tgt_nd.class_name.n > 0) {
        struct yetty_ycore_void_result assign_res =
            pending_styles_add_assign(pending, tgt_nd.id, tgt_nd.class_name, NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, assign_res, "parse_line: target class shorthand");
    }

    /* Strdup the ids NOW — they're stable in g->nodes[idx].id but the
     * pointers into g->nodes themselves are not stable across add_edge
     * calls (which can realloc g->edges and indirectly trigger pressure
     * on the heap). Belt-and-braces: take ownership and never deref the
     * graph for these ids again in this iteration. */
    char *src_id = strdup(g->nodes[src_idx].id);
    char *tgt_id = strdup(g->nodes[tgt_idx].id);
    char *lbl = slice_to_cstr(edge_label);
    struct yetty_ycore_void_result line_res = YETTY_OK_VOID();
    if (src_id && tgt_id && lbl) {
        struct yetty_ydiagram_edge_style style = g->default_edge_style;
        apply_arrow_style(arrow_str, &style);
        struct yetty_ycore_int_result edge_res =
            yetty_ydiagram_graph_add_edge(g, src_id, tgt_id, lbl, &style);
        if (YETTY_IS_ERR(edge_res)) {
            line_res = YETTY_ERR(yetty_ycore_void, "parse_line: add_edge", edge_res);
        }
    }

    /* Recurse on the rest of the chain: <first_target_id> <chain_arrow>... */
    if (YETTY_IS_OK(line_res) && chain_pos != (size_t)-1 && tgt_id) {
        size_t rest_off = chain_pos;
        const char *rest_p = target_part.p + rest_off;
        size_t rest_n = target_part.n - rest_off;
        size_t total = strlen(tgt_id) + 1 + rest_n;
        char *buf = malloc(total + 1);
        if (buf) {
            size_t off = 0;
            memcpy(buf + off, tgt_id, strlen(tgt_id));
            off += strlen(tgt_id);
            buf[off++] = ' ';
            memcpy(buf + off, rest_p, rest_n);
            off += rest_n;
            buf[off] = 0;
            struct yetty_ycore_void_result rec =
                parse_line(slice_make(buf, off), g, pending, current_subgraph);
            free(buf);
            if (YETTY_IS_ERR(rec)) {
                line_res = YETTY_ERR(yetty_ycore_void, "parse_line: chain recurse", rec);
            }
        }
    }

    free(src_id);
    free(tgt_id);
    free(lbl);
    return line_res;
}

struct yetty_ycore_void_result yetty_ydiagram_mermaid_parse(const char *input, size_t len,
                                                            struct yetty_ydiagram_graph *g)
{
    if (!input || !g) {
        return YETTY_ERR(yetty_ycore_void, "mermaid_parse: NULL input or graph");
    }

    struct slice cursor = slice_make(input, len);
    bool in_graph = false;
    char *current_subgraph = NULL;
    struct pending_styles pending = {0};

    while (cursor.n > 0) {
        /* Pull one line. */
        size_t nl = slice_find(cursor, "\n", 0);
        struct slice line = (nl == (size_t)-1) ? cursor : slice_make(cursor.p, nl);
        struct slice next = (nl == (size_t)-1) ? slice_make(NULL, 0)
                                               : slice_make(cursor.p + nl + 1, cursor.n - nl - 1);
        line = slice_trim(line);
        cursor = next;

        if (line.n == 0 || line.p[0] == '%') {
            continue;
        }

        if (slice_starts_with(line, "graph ") || slice_starts_with(line, "flowchart ") ||
            slice_equals_cstr(line, "graph") || slice_equals_cstr(line, "flowchart")) {
            in_graph = true;
            size_t sp = slice_find(line, " ", 0);
            if (sp != (size_t)-1) {
                struct slice dir = slice_trim(slice_make(line.p + sp + 1, line.n - sp - 1));
                if (slice_equals_cstr(dir, "TD") || slice_equals_cstr(dir, "TB")) {
                    g->direction = YETTY_YDIAGRAM_DIR_TB;
                } else if (slice_equals_cstr(dir, "BT")) {
                    g->direction = YETTY_YDIAGRAM_DIR_BT;
                } else if (slice_equals_cstr(dir, "LR")) {
                    g->direction = YETTY_YDIAGRAM_DIR_LR;
                } else if (slice_equals_cstr(dir, "RL")) {
                    g->direction = YETTY_YDIAGRAM_DIR_RL;
                }
            }
            continue;
        }

        if (slice_starts_with(line, "subgraph ")) {
            struct slice rest = slice_trim(slice_make(line.p + 9, line.n - 9));
            size_t br = slice_find(rest, "[", 0);
            struct slice sub_id, sub_label;
            if (br != (size_t)-1) {
                sub_id = slice_trim(slice_make(rest.p, br));
                size_t end_b = slice_find(rest, "]", br);
                sub_label = (end_b != (size_t)-1) ? slice_make(rest.p + br + 1, end_b - br - 1)
                                                  : slice_make(NULL, 0);
            } else {
                sub_id = rest;
                sub_label = rest;
            }
            char *id_z = slice_to_cstr(sub_id);
            char *lbl_z = slice_to_cstr(sub_label);
            if (id_z && lbl_z) {
                struct yetty_ycore_int_result cluster_res =
                    yetty_ydiagram_graph_add_cluster(g, id_z, lbl_z);
                if (YETTY_IS_ERR(cluster_res)) {
                    free(id_z);
                    free(lbl_z);
                    free(current_subgraph);
                    pending_styles_destroy(&pending);
                    return YETTY_ERR(yetty_ycore_void, "mermaid_parse: add_cluster", cluster_res);
                }
                free(current_subgraph);
                current_subgraph = strdup(id_z);
            }
            free(id_z);
            free(lbl_z);
            continue;
        }

        if (slice_equals_cstr(line, "end")) {
            free(current_subgraph);
            current_subgraph = NULL;
            continue;
        }

        /* Styling directives — collected now, resolved after the walk (a
         * `classDef` may appear after the nodes that reference it). */
        if (slice_starts_with(line, "classDef ")) {
            struct yetty_ycore_void_result directive_res =
                parse_class_def_directive(slice_make(line.p + 9, line.n - 9), &pending);
            if (YETTY_IS_ERR(directive_res)) {
                free(current_subgraph);
                pending_styles_destroy(&pending);
                return YETTY_ERR(yetty_ycore_void, "mermaid_parse: classDef", directive_res);
            }
            continue;
        }
        if (slice_starts_with(line, "class ")) {
            struct yetty_ycore_void_result directive_res =
                parse_class_assign_directive(slice_make(line.p + 6, line.n - 6), &pending);
            if (YETTY_IS_ERR(directive_res)) {
                free(current_subgraph);
                pending_styles_destroy(&pending);
                return YETTY_ERR(yetty_ycore_void, "mermaid_parse: class", directive_res);
            }
            continue;
        }
        if (slice_starts_with(line, "style ")) {
            struct yetty_ycore_void_result directive_res =
                parse_style_directive(slice_make(line.p + 6, line.n - 6), &pending);
            if (YETTY_IS_ERR(directive_res)) {
                free(current_subgraph);
                pending_styles_destroy(&pending);
                return YETTY_ERR(yetty_ycore_void, "mermaid_parse: style", directive_res);
            }
            continue;
        }
        if (slice_starts_with(line, "linkStyle ")) {
            /* Edge styling — not mapped onto the IR yet; consume the line so
             * it doesn't turn into a literal node. */
            continue;
        }

        if (in_graph) {
            struct yetty_ycore_void_result line_res =
                parse_line(line, g, &pending, current_subgraph);
            if (YETTY_IS_ERR(line_res)) {
                free(current_subgraph);
                pending_styles_destroy(&pending);
                return YETTY_ERR(yetty_ycore_void, "mermaid_parse: parse_line", line_res);
            }
        }
    }

    apply_pending_styles(g, &pending);
    pending_styles_destroy(&pending);
    free(current_subgraph);
    return YETTY_OK_VOID();
}
