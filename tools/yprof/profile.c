/*
 * profile.c — folded-stack aggregation and perf-script collapsing for yprof.
 *
 * The symbol table is built by walking the folded text: the leaf frame of each
 * stack gets its count added to `self`, and every distinct frame in the stack
 * gets it added to `total` (deduplicated within the stack so recursion counts
 * once). The perf-script collapser turns `perf script` output into the same
 * folded text so both paths converge on one representation.
 */
#define _DEFAULT_SOURCE 1

#include "profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Symbol hash table (name -> index into profile->symbols)             */
/* ------------------------------------------------------------------ */

static uint64_t fnv1a(const char *bytes, size_t len)
{
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int symtab_grow(struct yprof_profile *profile)
{
    size_t new_cap = profile->bucket_cap ? profile->bucket_cap * 2 : 256;
    int32_t *buckets = malloc(new_cap * sizeof(*buckets));
    if (!buckets) {
        return 0;
    }
    for (size_t i = 0; i < new_cap; i++) {
        buckets[i] = -1;
    }
    size_t mask = new_cap - 1;
    for (size_t i = 0; i < profile->n_symbols; i++) {
        const char *name = profile->symbols[i].name;
        size_t slot = (size_t)fnv1a(name, strlen(name)) & mask;
        while (buckets[slot] >= 0) {
            slot = (slot + 1) & mask;
        }
        buckets[slot] = (int32_t)i;
    }
    free(profile->buckets);
    profile->buckets = buckets;
    profile->bucket_cap = new_cap;
    return 1;
}

/* Intern a symbol name; returns its index, or -1 on allocation failure. */
static int64_t symtab_intern(struct yprof_profile *profile, const char *name, size_t len)
{
    if (profile->bucket_cap == 0 || (profile->n_symbols + 1) * 10 >= profile->bucket_cap * 7) {
        if (!symtab_grow(profile)) {
            return -1;
        }
    }
    size_t mask = profile->bucket_cap - 1;
    size_t slot = (size_t)fnv1a(name, len) & mask;
    while (profile->buckets[slot] >= 0) {
        struct yprof_symbol *sym = &profile->symbols[profile->buckets[slot]];
        if (strlen(sym->name) == len && memcmp(sym->name, name, len) == 0) {
            return profile->buckets[slot];
        }
        slot = (slot + 1) & mask;
    }
    if (profile->n_symbols == profile->cap_symbols) {
        size_t new_cap = profile->cap_symbols ? profile->cap_symbols * 2 : 128;
        struct yprof_symbol *grown = realloc(profile->symbols, new_cap * sizeof(*grown));
        if (!grown) {
            return -1;
        }
        profile->symbols = grown;
        profile->cap_symbols = new_cap;
    }
    char *copy = malloc(len + 1);
    if (!copy) {
        return -1;
    }
    memcpy(copy, name, len);
    copy[len] = '\0';
    int64_t index = (int64_t)profile->n_symbols;
    profile->symbols[index].name = copy;
    profile->symbols[index].self = 0;
    profile->symbols[index].total = 0;
    profile->symbols[index].gen = 0;
    profile->n_symbols++;
    profile->buckets[slot] = (int32_t)index;
    return index;
}

/* ------------------------------------------------------------------ */
/* Folded-stack parsing                                                */
/* ------------------------------------------------------------------ */

static struct yetty_ycore_void_result aggregate_folded(struct yprof_profile *profile)
{
    const char *text = profile->folded;
    size_t len = profile->folded_len;
    size_t i = 0;
    while (i < len) {
        size_t line_start = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        size_t line_end = i;
        if (i < len) {
            i++; /* step over '\n' */
        }
        if (line_end > line_start && text[line_end - 1] == '\r') {
            line_end--;
        }
        if (line_end == line_start) {
            continue;
        }

        /* Count is the token after the last space; frame names may contain
         * spaces, so split at the final space only. */
        size_t space = line_end;
        while (space > line_start && text[space - 1] != ' ') {
            space--;
        }
        if (space <= line_start) {
            continue; /* no count field */
        }
        char count_buf[32];
        size_t count_len = line_end - space;
        if (count_len == 0 || count_len >= sizeof(count_buf)) {
            continue;
        }
        memcpy(count_buf, text + space, count_len);
        count_buf[count_len] = '\0';
        char *count_end = NULL;
        unsigned long long count = strtoull(count_buf, &count_end, 10);
        if (count_end == count_buf || *count_end != '\0' || count == 0) {
            continue;
        }

        /* The frames region is [line_start, frames_end); frames_end is the index
         * of the single space separating the stack from its count. */
        size_t frames_end = space - 1;
        profile->stack_count++;
        profile->total_samples += count;
        uint64_t gen = profile->stack_count;

        int64_t leaf = -1;
        uint32_t depth = 0;
        size_t frame_start = line_start;
        for (size_t pos = line_start; pos <= frames_end; pos++) {
            if (pos != frames_end && text[pos] != ';') {
                continue;
            }
            size_t frame_len = pos - frame_start;
            if (frame_len > 0) {
                int64_t index = symtab_intern(profile, text + frame_start, frame_len);
                if (index < 0) {
                    return YETTY_ERR(yetty_ycore_void, "out of memory building symbols");
                }
                if (profile->symbols[index].gen != gen) {
                    profile->symbols[index].total += count;
                    profile->symbols[index].gen = gen;
                }
                leaf = index;
                depth++;
            }
            frame_start = pos + 1;
        }
        if (leaf >= 0) {
            profile->symbols[leaf].self += count;
        }
        if (depth > profile->max_depth) {
            profile->max_depth = depth;
        }
    }
    return YETTY_OK_VOID();
}

/* ------------------------------------------------------------------ */
/* Profile lifecycle                                                   */
/* ------------------------------------------------------------------ */

static struct yprof_profile *profile_new(const char *folded, size_t len)
{
    struct yprof_profile *profile = calloc(1, sizeof(*profile));
    if (!profile) {
        return NULL;
    }
    profile->folded = malloc(len + 1);
    if (!profile->folded) {
        free(profile);
        return NULL;
    }
    memcpy(profile->folded, folded, len);
    profile->folded[len] = '\0';
    profile->folded_len = len;
    return profile;
}

void yprof_profile_destroy(struct yprof_profile *profile)
{
    if (!profile) {
        return;
    }
    for (size_t i = 0; i < profile->n_symbols; i++) {
        free(profile->symbols[i].name);
    }
    free(profile->symbols);
    free(profile->buckets);
    free(profile->folded);
    free(profile->timeline);
    free(profile);
}

struct yetty_ycore_void_result yprof_profile_from_folded(const char *folded, size_t len,
                                                         struct yprof_profile **out)
{
    if (!folded || !out) {
        return YETTY_ERR(yetty_ycore_void, "yprof_profile_from_folded: null argument");
    }
    struct yprof_profile *profile = profile_new(folded, len);
    if (!profile) {
        return YETTY_ERR(yetty_ycore_void, "out of memory");
    }
    struct yetty_ycore_void_result agg = aggregate_folded(profile);
    if (YETTY_IS_ERR(agg)) {
        yprof_profile_destroy(profile);
        return agg;
    }
    /* The name index is only needed while aggregating. */
    free(profile->buckets);
    profile->buckets = NULL;
    profile->bucket_cap = 0;
    *out = profile;
    return YETTY_OK_VOID();
}

/* Does the frames region [start,end) of a folded line contain `symbol` as a
 * whole `;`-delimited frame? */
static int folded_line_has_symbol(const char *frames, size_t frames_len, const char *symbol,
                                  size_t symbol_len)
{
    size_t frame_start = 0;
    for (size_t pos = 0; pos <= frames_len; pos++) {
        if (pos == frames_len || frames[pos] == ';') {
            size_t frame_len = pos - frame_start;
            if (frame_len == symbol_len && memcmp(frames + frame_start, symbol, symbol_len) == 0) {
                return 1;
            }
            frame_start = pos + 1;
        }
    }
    return 0;
}

struct yetty_ycore_void_result yprof_folded_filter(const char *folded, size_t len,
                                                   const char *symbol, char **out, size_t *out_len)
{
    if (!folded || !out || !out_len) {
        return YETTY_ERR(yetty_ycore_void, "yprof_folded_filter: null argument");
    }
    size_t symbol_len = symbol ? strlen(symbol) : 0;
    size_t cap = len + 1, pos = 0;
    char *buf = malloc(cap);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "out of memory filtering folded");
    }
    size_t i = 0;
    while (i < len) {
        size_t line_start = i;
        while (i < len && folded[i] != '\n') {
            i++;
        }
        size_t line_end = i;
        if (i < len) {
            i++;
        }
        size_t bare_end = line_end;
        if (bare_end > line_start && folded[bare_end - 1] == '\r') {
            bare_end--;
        }
        if (bare_end == line_start) {
            continue;
        }
        int keep = (symbol_len == 0);
        if (!keep) {
            size_t space = bare_end;
            while (space > line_start && folded[space - 1] != ' ') {
                space--;
            }
            if (space > line_start) {
                keep = folded_line_has_symbol(folded + line_start, space - 1 - line_start, symbol,
                                              symbol_len);
            }
        }
        if (keep) {
            size_t bare_len = bare_end - line_start;
            memcpy(buf + pos, folded + line_start, bare_len);
            pos += bare_len;
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    *out = buf;
    *out_len = pos;
    return YETTY_OK_VOID();
}

/* Find a `NNN.NNN:` timestamp token on a perf-script header line; return its
 * seconds value, or -1.0 if the line carries no recognizable timestamp. */
static double perf_header_timestamp(const char *line, size_t len)
{
    size_t i = 0;
    while (i < len) {
        while (i < len && line[i] == ' ') {
            i++;
        }
        size_t token_start = i;
        while (i < len && line[i] != ' ') {
            i++;
        }
        size_t token_end = i;
        /* Accept a token shaped like <digits>.<digits>: — perf prints the
         * sample time this way (e.g. "1234.567890:"). */
        if (token_end > token_start && line[token_end - 1] == ':') {
            size_t s = token_start, e = token_end - 1;
            int dot = 0, digits = 0, ok = 1;
            for (size_t k = s; k < e; k++) {
                if (line[k] == '.') {
                    dot++;
                } else if (line[k] >= '0' && line[k] <= '9') {
                    digits++;
                } else {
                    ok = 0;
                    break;
                }
            }
            if (ok && dot == 1 && digits > 0) {
                char buf[64];
                size_t n = e - s;
                if (n >= sizeof(buf)) {
                    n = sizeof(buf) - 1;
                }
                memcpy(buf, line + s, n);
                buf[n] = '\0';
                return strtod(buf, NULL);
            }
        }
    }
    return -1.0;
}

/* Scan raw perf-script text for per-sample timestamps and bucket them into a
 * fixed-width sample-rate timeline on the profile (best-effort; absent for
 * input without timestamps). */
static void extract_timeline(struct yprof_profile *profile, const char *text, size_t len)
{
    enum { YPROF_TIMELINE_BUCKETS = 120 };
    double *stamps = NULL;
    size_t n = 0, cap = 0;
    size_t i = 0;
    while (i < len) {
        size_t line_start = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        size_t line_end = i;
        if (i < len) {
            i++;
        }
        size_t line_len = line_end - line_start;
        if (line_len == 0) {
            continue;
        }
        /* Only header lines (non-indented) carry the sample timestamp. */
        if (text[line_start] == ' ' || text[line_start] == '\t') {
            continue;
        }
        double ts = perf_header_timestamp(text + line_start, line_len);
        if (ts < 0.0) {
            continue;
        }
        if (n == cap) {
            size_t new_cap = cap ? cap * 2 : 1024;
            double *grown = realloc(stamps, new_cap * sizeof(*stamps));
            if (!grown) {
                free(stamps);
                return;
            }
            stamps = grown;
            cap = new_cap;
        }
        stamps[n++] = ts;
    }
    if (n < 2) {
        free(stamps);
        return;
    }
    double lo = stamps[0], hi = stamps[0];
    for (size_t k = 1; k < n; k++) {
        if (stamps[k] < lo) {
            lo = stamps[k];
        }
        if (stamps[k] > hi) {
            hi = stamps[k];
        }
    }
    if (hi <= lo) {
        free(stamps);
        return;
    }
    uint32_t *buckets = calloc(YPROF_TIMELINE_BUCKETS, sizeof(*buckets));
    if (!buckets) {
        free(stamps);
        return;
    }
    double span = hi - lo;
    uint32_t peak = 0;
    for (size_t k = 0; k < n; k++) {
        size_t b = (size_t)((stamps[k] - lo) / span * (double)(YPROF_TIMELINE_BUCKETS - 1));
        if (b >= YPROF_TIMELINE_BUCKETS) {
            b = YPROF_TIMELINE_BUCKETS - 1;
        }
        buckets[b]++;
        if (buckets[b] > peak) {
            peak = buckets[b];
        }
    }
    free(stamps);
    profile->timeline = buckets;
    profile->timeline_n = YPROF_TIMELINE_BUCKETS;
    profile->timeline_peak = peak;
    profile->time_start = lo;
    profile->time_end = hi;
}

struct yetty_ycore_void_result yprof_profile_from_perf_script(const char *text, size_t len,
                                                              struct yprof_profile **out)
{
    char *folded = NULL;
    size_t folded_len = 0;
    struct yetty_ycore_void_result collapse =
        yprof_collapse_perf_script(text, len, &folded, &folded_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, collapse, "perf-script collapse failed");
    struct yetty_ycore_void_result built = yprof_profile_from_folded(folded, folded_len, out);
    free(folded);
    if (YETTY_IS_OK(built) && out && *out) {
        extract_timeline(*out, text, len);
    }
    return built;
}

/* ------------------------------------------------------------------ */
/* Sorting                                                             */
/* ------------------------------------------------------------------ */

static int cmp_self(const void *a, const void *b)
{
    const struct yprof_symbol *x = a, *y = b;
    if (x->self != y->self) {
        return y->self > x->self ? 1 : -1;
    }
    return strcmp(x->name, y->name);
}

static int cmp_total(const void *a, const void *b)
{
    const struct yprof_symbol *x = a, *y = b;
    if (x->total != y->total) {
        return y->total > x->total ? 1 : -1;
    }
    return strcmp(x->name, y->name);
}

static int cmp_name(const void *a, const void *b)
{
    const struct yprof_symbol *x = a, *y = b;
    return strcmp(x->name, y->name);
}

void yprof_profile_sort(struct yprof_profile *profile, enum yprof_sort_mode mode)
{
    if (!profile || profile->n_symbols < 2) {
        return;
    }
    int (*cmp)(const void *, const void *) = cmp_self;
    switch (mode) {
    case YPROF_SORT_SELF:
        cmp = cmp_self;
        break;
    case YPROF_SORT_TOTAL:
        cmp = cmp_total;
        break;
    case YPROF_SORT_NAME:
        cmp = cmp_name;
        break;
    case YPROF_SORT_MODE_COUNT:
        cmp = cmp_self;
        break;
    }
    qsort(profile->symbols, profile->n_symbols, sizeof(profile->symbols[0]), cmp);
}

const char *yprof_sort_mode_name(enum yprof_sort_mode mode)
{
    switch (mode) {
    case YPROF_SORT_SELF:
        return "self";
    case YPROF_SORT_TOTAL:
        return "total";
    case YPROF_SORT_NAME:
        return "name";
    case YPROF_SORT_MODE_COUNT:
        break;
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Input helpers                                                       */
/* ------------------------------------------------------------------ */

static struct yetty_ycore_void_result read_stream(FILE *stream, char **out, size_t *out_len)
{
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "out of memory reading input");
    }
    for (;;) {
        if (len == cap) {
            size_t new_cap = cap * 2;
            char *grown = realloc(buf, new_cap);
            if (!grown) {
                free(buf);
                return YETTY_ERR(yetty_ycore_void, "out of memory reading input");
            }
            buf = grown;
            cap = new_cap;
        }
        size_t got = fread(buf + len, 1, cap - len, stream);
        len += got;
        if (got == 0) {
            break;
        }
    }
    buf[len < cap ? len : cap - 1] = '\0';
    *out = buf;
    *out_len = len;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yprof_read_all(const char *path, char **out, size_t *out_len)
{
    if (!out || !out_len) {
        return YETTY_ERR(yetty_ycore_void, "yprof_read_all: null argument");
    }
    if (!path) {
        return read_stream(stdin, out, out_len);
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "cannot open input file");
    }
    struct yetty_ycore_void_result result = read_stream(file, out, out_len);
    fclose(file);
    return result;
}

/* ------------------------------------------------------------------ */
/* perf-script collapsing                                              */
/* ------------------------------------------------------------------ */

/* count map: collapsed-stack string -> sample count */
struct collapse_map {
    char **keys;
    uint64_t *counts;
    size_t n;
    size_t cap;
    int32_t *buckets;
    size_t bucket_cap;
};

static int collapse_map_grow(struct collapse_map *map)
{
    size_t new_cap = map->bucket_cap ? map->bucket_cap * 2 : 512;
    int32_t *buckets = malloc(new_cap * sizeof(*buckets));
    if (!buckets) {
        return 0;
    }
    for (size_t i = 0; i < new_cap; i++) {
        buckets[i] = -1;
    }
    size_t mask = new_cap - 1;
    for (size_t i = 0; i < map->n; i++) {
        size_t slot = (size_t)fnv1a(map->keys[i], strlen(map->keys[i])) & mask;
        while (buckets[slot] >= 0) {
            slot = (slot + 1) & mask;
        }
        buckets[slot] = (int32_t)i;
    }
    free(map->buckets);
    map->buckets = buckets;
    map->bucket_cap = new_cap;
    return 1;
}

static int collapse_map_add(struct collapse_map *map, const char *key, uint64_t count)
{
    if (map->bucket_cap == 0 || (map->n + 1) * 10 >= map->bucket_cap * 7) {
        if (!collapse_map_grow(map)) {
            return 0;
        }
    }
    size_t len = strlen(key);
    size_t mask = map->bucket_cap - 1;
    size_t slot = (size_t)fnv1a(key, len) & mask;
    while (map->buckets[slot] >= 0) {
        int32_t index = map->buckets[slot];
        if (strcmp(map->keys[index], key) == 0) {
            map->counts[index] += count;
            return 1;
        }
        slot = (slot + 1) & mask;
    }
    if (map->n == map->cap) {
        size_t new_cap = map->cap ? map->cap * 2 : 256;
        char **keys = realloc(map->keys, new_cap * sizeof(*keys));
        if (!keys) {
            return 0;
        }
        map->keys = keys;
        uint64_t *counts = realloc(map->counts, new_cap * sizeof(*counts));
        if (!counts) {
            return 0;
        }
        map->counts = counts;
        map->cap = new_cap;
    }
    char *copy = malloc(len + 1);
    if (!copy) {
        return 0;
    }
    memcpy(copy, key, len + 1);
    map->keys[map->n] = copy;
    map->counts[map->n] = count;
    map->buckets[slot] = (int32_t)map->n;
    map->n++;
    return 1;
}

static void collapse_map_free(struct collapse_map *map)
{
    for (size_t i = 0; i < map->n; i++) {
        free(map->keys[i]);
    }
    free(map->keys);
    free(map->counts);
    free(map->buckets);
}

/* Extract a symbol name from a perf-script frame line, stripping the leading
 * instruction address, any "+0x…" offset, and a trailing "(module)". */
static void extract_symbol(const char *line, size_t len, char *out, size_t out_size)
{
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    size_t first_start = i;
    while (i < len && line[i] != ' ' && line[i] != '\t') {
        i++;
    }
    size_t first_end = i;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    size_t sym_start, sym_end;
    if (i >= len) {
        /* single token: it is the symbol (no address column) */
        sym_start = first_start;
        sym_end = first_end;
    } else {
        sym_start = i;
        sym_end = len;
    }
    /* cut at " (" (module) */
    for (size_t k = sym_start; k + 1 < sym_end; k++) {
        if (line[k] == ' ' && line[k + 1] == '(') {
            sym_end = k;
            break;
        }
    }
    /* cut at "+0x" (offset) */
    for (size_t k = sym_start; k + 2 < sym_end; k++) {
        if (line[k] == '+' && line[k + 1] == '0' && line[k + 2] == 'x') {
            sym_end = k;
            break;
        }
    }
    while (sym_end > sym_start && (line[sym_end - 1] == ' ' || line[sym_end - 1] == '\t')) {
        sym_end--;
    }
    if (sym_end <= sym_start) {
        snprintf(out, out_size, "[unknown]");
        return;
    }
    size_t n = sym_end - sym_start;
    if (n > out_size - 1) {
        n = out_size - 1;
    }
    memcpy(out, line + sym_start, n);
    out[n] = '\0';
}

/* Comm = the text before the first all-digit token (the pid), spaces mapped to
 * underscores so it reads as one folded frame. */
static void extract_comm(const char *line, size_t len, char *out, size_t out_size)
{
    size_t comm_end = 0;
    size_t i = 0;
    int found = 0;
    while (i < len) {
        while (i < len && line[i] == ' ') {
            i++;
        }
        size_t token_start = i;
        while (i < len && line[i] != ' ') {
            i++;
        }
        if (i > token_start) {
            int all_digit = 1;
            for (size_t k = token_start; k < i; k++) {
                if (line[k] < '0' || line[k] > '9') {
                    all_digit = 0;
                    break;
                }
            }
            if (all_digit) {
                comm_end = token_start;
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        /* fall back to the first token */
        i = 0;
        while (i < len && line[i] == ' ') {
            i++;
        }
        comm_end = i;
        while (comm_end < len && line[comm_end] != ' ') {
            comm_end++;
        }
    }
    while (comm_end > 0 && line[comm_end - 1] == ' ') {
        comm_end--;
    }
    size_t n = 0;
    for (size_t k = 0; k < comm_end && n < out_size - 1; k++) {
        out[n++] = (line[k] == ' ') ? '_' : line[k];
    }
    out[n] = '\0';
    if (n == 0) {
        snprintf(out, out_size, "perf");
    }
}

static int flush_stack(struct collapse_map *map, const char *comm, char **frames, size_t n_frames)
{
    if (n_frames == 0) {
        return 1;
    }
    size_t needed = strlen(comm) + 1;
    for (size_t i = 0; i < n_frames; i++) {
        needed += strlen(frames[i]) + 1;
    }
    char *line = malloc(needed + 1);
    if (!line) {
        return 0;
    }
    size_t pos = 0;
    int wrote_comm = 0;
    if (comm[0]) {
        pos += (size_t)snprintf(line + pos, needed + 1 - pos, "%s", comm);
        wrote_comm = 1;
    }
    /* frames were collected leaf-first; emit root-first */
    for (size_t i = n_frames; i-- > 0;) {
        if (wrote_comm || pos > 0) {
            line[pos++] = ';';
        }
        pos += (size_t)snprintf(line + pos, needed + 1 - pos, "%s", frames[i]);
        wrote_comm = 1;
    }
    line[pos] = '\0';
    int ok = collapse_map_add(map, line, 1);
    free(line);
    return ok;
}

static void free_frames(char **frames, size_t *n_frames)
{
    for (size_t i = 0; i < *n_frames; i++) {
        free(frames[i]);
    }
    *n_frames = 0;
}

struct yetty_ycore_void_result yprof_collapse_perf_script(const char *text, size_t len, char **out,
                                                          size_t *out_len)
{
    if (!text || !out || !out_len) {
        return YETTY_ERR(yetty_ycore_void, "yprof_collapse_perf_script: null argument");
    }
    struct collapse_map map = {0};
    char comm[256] = {0};
    char **frames = NULL;
    size_t n_frames = 0, cap_frames = 0;
    struct yetty_ycore_void_result status = YETTY_OK_VOID();

    size_t i = 0;
    while (i <= len) {
        size_t line_start = i;
        while (i < len && text[i] != '\n') {
            i++;
        }
        size_t line_end = i;
        int is_last = (i >= len);
        if (line_end > line_start && text[line_end - 1] == '\r') {
            line_end--;
        }
        size_t line_len = line_end - line_start;
        int blank = (line_len == 0);
        int indented = (!blank && (text[line_start] == ' ' || text[line_start] == '\t'));

        if (blank) {
            if (!flush_stack(&map, comm, frames, n_frames)) {
                status = YETTY_ERR(yetty_ycore_void, "out of memory collapsing perf script");
                goto done;
            }
            free_frames(frames, &n_frames);
            comm[0] = '\0';
        } else if (indented) {
            char sym[256];
            extract_symbol(text + line_start, line_len, sym, sizeof(sym));
            if (n_frames == cap_frames) {
                size_t new_cap = cap_frames ? cap_frames * 2 : 64;
                char **grown = realloc(frames, new_cap * sizeof(*grown));
                if (!grown) {
                    status = YETTY_ERR(yetty_ycore_void, "out of memory collapsing");
                    goto done;
                }
                frames = grown;
                cap_frames = new_cap;
            }
            frames[n_frames] = malloc(strlen(sym) + 1);
            if (!frames[n_frames]) {
                status = YETTY_ERR(yetty_ycore_void, "out of memory collapsing");
                goto done;
            }
            memcpy(frames[n_frames], sym, strlen(sym) + 1);
            n_frames++;
        } else {
            /* header line: flush any pending stack, start a new one */
            if (!flush_stack(&map, comm, frames, n_frames)) {
                status = YETTY_ERR(yetty_ycore_void, "out of memory collapsing perf script");
                goto done;
            }
            free_frames(frames, &n_frames);
            extract_comm(text + line_start, line_len, comm, sizeof(comm));
        }

        if (is_last) {
            break;
        }
        i++; /* advance past '\n' (i currently points at it) */
    }
    if (!flush_stack(&map, comm, frames, n_frames)) {
        status = YETTY_ERR(yetty_ycore_void, "out of memory collapsing perf script");
        goto done;
    }

    /* Emit folded text from the collapsed map. */
    {
        size_t cap = 4096, pos = 0;
        char *buf = malloc(cap);
        if (!buf) {
            status = YETTY_ERR(yetty_ycore_void, "out of memory emitting folded");
            goto done;
        }
        for (size_t k = 0; k < map.n; k++) {
            char count_buf[24];
            int count_len = snprintf(count_buf, sizeof(count_buf), " %llu\n",
                                     (unsigned long long)map.counts[k]);
            size_t key_len = strlen(map.keys[k]);
            size_t need = pos + key_len + (size_t)count_len + 1;
            if (need > cap) {
                while (cap < need) {
                    cap *= 2;
                }
                char *grown = realloc(buf, cap);
                if (!grown) {
                    free(buf);
                    status = YETTY_ERR(yetty_ycore_void, "out of memory emitting folded");
                    goto done;
                }
                buf = grown;
            }
            memcpy(buf + pos, map.keys[k], key_len);
            pos += key_len;
            memcpy(buf + pos, count_buf, (size_t)count_len);
            pos += (size_t)count_len;
        }
        buf[pos] = '\0';
        *out = buf;
        *out_len = pos;
    }

done:
    free_frames(frames, &n_frames);
    free(frames);
    collapse_map_free(&map);
    return status;
}

/* ------------------------------------------------------------------ */
/* Formatting                                                          */
/* ------------------------------------------------------------------ */

void yprof_fmt_count(uint64_t value, char *out, size_t out_size)
{
    if (value >= 10000000ULL) {
        snprintf(out, out_size, "%.1fM", (double)value / 1e6);
    } else if (value >= 100000ULL) {
        snprintf(out, out_size, "%.1fk", (double)value / 1e3);
    } else {
        snprintf(out, out_size, "%llu", (unsigned long long)value);
    }
}
