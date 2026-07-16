/*
 * profile.h — yperf profile model.
 *
 * yperf reuses the `yflame` class for the flame-graph render, but yflame parses
 * folded text and does not expose its call tree. So the profile model here owns
 * two things: the folded-stack text (handed verbatim to yflame) and an
 * aggregated per-symbol table (self / total sample counts) computed by parsing
 * the same folded text. It also holds a best-effort `perf script` collapser so
 * a raw perf capture can be turned into folded text.
 *
 * The model has no yetty dependency beyond the Result type, so `yperf --print`
 * exercises it headless.
 */
#ifndef YPERF_PROFILE_H
#define YPERF_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

struct yperf_symbol {
    char *name;
    uint64_t self;  /* samples where this symbol is the leaf (on-CPU top) */
    uint64_t total; /* samples where this symbol appears anywhere in the stack */
    uint64_t gen;   /* per-stack dedup generation, used while aggregating total */
};

struct yperf_profile {
    char *folded; /* folded-stack text (owned), fed verbatim to yflame */
    size_t folded_len;

    struct yperf_symbol *symbols;
    size_t n_symbols;
    size_t cap_symbols;
    int32_t *buckets; /* name -> symbol index hash, freed once aggregation is done */
    size_t bucket_cap;

    uint64_t total_samples; /* sum of every stack's count */
    uint64_t stack_count;   /* number of folded lines accepted */
    uint32_t max_depth;     /* deepest stack, used to size the flame rows */

    /* Optional sample timeline, present only for timestamped input (`perf
     * script`). timeline[i] is the sample count in the i-th equal-width time
     * bucket between time_start and time_end (seconds). NULL for folded input. */
    uint32_t *timeline;
    size_t timeline_n;
    uint32_t timeline_peak; /* max bucket count, for bar scaling */
    double time_start;
    double time_end;
};

enum yperf_sort_mode {
    YPERF_SORT_SELF = 0, /* self samples, largest first (default) */
    YPERF_SORT_TOTAL,    /* total samples, largest first */
    YPERF_SORT_NAME,     /* symbol name, A->Z */
    YPERF_SORT_MODE_COUNT
};

/* Build a profile from Brendan-Gregg folded text (`frame;frame;... count`).
 * Takes its own copy of the text. */
struct yetty_ycore_void_result yperf_profile_from_folded(const char *folded, size_t len,
                                                         struct yperf_profile **out);

/* Build a profile from raw `perf script` text by collapsing it to folded first. */
struct yetty_ycore_void_result yperf_profile_from_perf_script(const char *text, size_t len,
                                                              struct yperf_profile **out);

void yperf_profile_destroy(struct yperf_profile *profile);
void yperf_profile_sort(struct yperf_profile *profile, enum yperf_sort_mode mode);
const char *yperf_sort_mode_name(enum yperf_sort_mode mode);

/* Read a whole file (path) or stdin (path == NULL) into a heap buffer. */
struct yetty_ycore_void_result yperf_read_all(const char *path, char **out, size_t *out_len);

/* Collapse raw `perf script` text into folded text (heap-owned in *out). */
struct yetty_ycore_void_result yperf_collapse_perf_script(const char *text, size_t len, char **out,
                                                          size_t *out_len);

/* Keep only folded lines whose stack contains `symbol` as a whole frame; the
 * result is fresh folded text (heap-owned in *out). Used to drill the profile
 * down to callers/callees of one symbol. A NULL/empty symbol copies verbatim. */
struct yetty_ycore_void_result yperf_folded_filter(const char *folded, size_t len,
                                                   const char *symbol, char **out, size_t *out_len);

/* Compact sample-count formatting (e.g. "2175", "12.3k", "4.1M"). */
void yperf_fmt_count(uint64_t value, char *out, size_t out_size);

#endif /* YPERF_PROFILE_H */
