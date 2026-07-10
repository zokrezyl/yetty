/*
 * yperf — per-app wasm-function profile recorder and reader for yos.
 *
 * Usage (modelled on linux perf):
 *
 *   yperf record [-o file] <prog> [args...]    # capture a profile
 *   yperf report [-i file] [-n N]              # print a flat profile
 *
 * Default file is `./yperf.data` in the current directory, same
 * convention as `perf.data`. -o / -i override.
 *
 * RECORD: fork+exec the target with YPERF_FILE / YPERF=yes set in
 * the env; after waitpid() returns, signal the host yperf recorder
 * to flush + reset via setenv("YPERF","stop"). The result lives in
 * the binary YPERFv01 file (see src/yos/yperf/yperf.c for layout).
 *
 * REPORT: parse the binary file, compute self/cumulative time per
 * function using the standard "stack-of-pending-records" walk —
 * each new record absorbs every stack-top whose enter_ts is >= its
 * own (those calls were nested inside it). Prints a perf-style
 * flat table sorted by cumulative time.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define YPERF_DEFAULT_FILE  "yperf.data"

/* ── helpers ──────────────────────────────────────────────────────── */

static void usage(void)
{
    fprintf(stderr,
        "yperf - per-app wasm-function profiler for yos.\n"
        "usage:\n"
        "  yperf record [-o file] [-s SIZE] <prog> [args...]\n"
        "    -o file   output file (default ./" YPERF_DEFAULT_FILE ")\n"
        "    -s SIZE   per-thread ring capacity in RECORDS (default 1M)\n"
        "              each record is 24 B — set to 2-8M for full nvim\n"
        "              startup, more for editing sessions / test suites\n"
        "  yperf report [-i file] [-n N]\n"
        "    -n N      show top N functions (default 20)\n"
        "  yperf -h | --help\n");
}

/* ── record ──────────────────────────────────────────────────────── */

static int cmd_record(int argc, char **argv)
{
    const char *out_file = NULL;
    const char *ring_str = NULL;
    int i = 0;
    while (i < argc) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[i + 1]; i += 2;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            ring_str = argv[i + 1]; i += 2;
        } else if (argv[i][0] == '-' && argv[i][1] != 0) {
            fprintf(stderr, "yperf record: unknown flag %s\n", argv[i]);
            return 2;
        } else {
            break;
        }
    }
    if (i >= argc) {
        fprintf(stderr, "yperf record: missing program\n");
        return 2;
    }

    /* YPERF_FILE is a relative path by default → ends up in the cwd
     * of whatever process opens the file, which (post-fork) is the
     * host yos itself. yos inherits the guest's cwd via ctx->cwd
     * but host fopen uses the host cwd, which yos.sh sets to the
     * user's invocation directory. Net effect: yperf.data lands
     * next to where the user ran yperf, like perf.data. */
    if (!out_file) out_file = YPERF_DEFAULT_FILE;
    setenv("YPERF_FILE", out_file, 1);
    /* Ring size must be set BEFORE the child's first wasm-fn call so
     * its per-thread state allocates the right size. The setenv goes
     * through yos's env hook which routes YPERF_RING_SIZE → host
     * setenv → yperf_init reads it on first use. */
    if (ring_str) setenv("YPERF_RING_SIZE", ring_str, 1);
    setenv("YPERF", "yes", 1);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "yperf: fork: %s\n", strerror(errno));
        setenv("YPERF", "stop", 1);
        return 1;
    }
    if (pid == 0) {
        execvp(argv[i], argv + i);
        fprintf(stderr, "yperf: exec %s: %s\n", argv[i], strerror(errno));
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        fprintf(stderr, "yperf: waitpid: %s\n", strerror(errno));
        break;
    }

    /* Trigger host-side dump + reset. After this returns the file is
     * on disk and a subsequent capture would start a fresh ring. */
    setenv("YPERF", "stop", 1);

    fprintf(stderr, "[ yperf record: profile written to %s ]\n", out_file);

    if (WIFEXITED(status))    return WEXITSTATUS(status);
    if (WIFSIGNALED(status))  return 128 + WTERMSIG(status);
    return 0;
}

/* ── report ──────────────────────────────────────────────────────── */

struct yperf_file_header {
    char     magic[8];
    uint64_t header_size;
    uint64_t num_records;
    uint64_t num_symbols;
    uint64_t records_offset;
    uint64_t symbols_offset;
    uint64_t overflow_count;
};

/* On-disk record layout. fn_handle is explicit uint64_t — the file
 * is written by the host (8-byte pointers) so the reader must use
 * uint64_t, not uintptr_t (which would be 4 bytes here in wasm32). */
struct yperf_record {
    uint64_t enter_ts_ns;
    uint64_t exit_ts_ns;
    uint64_t fn_handle;
};

struct sym {
    uint64_t  fn_handle;
    char     *name;
};

struct fn_stat {
    uint64_t  fn_handle;
    uint64_t  calls;
    uint64_t  cum_ns;
    uint64_t  self_ns;
};

/* Lookup by linear scan — symbol count is small (hundreds to
 * thousands). For the record loop we use a per-loop pointer cache
 * (last_handle / last_stat) to avoid re-scanning when the same fn
 * appears many times in a row, which is the common case. */
static struct fn_stat *find_stat(struct fn_stat *stats, size_t n,
                                  uint64_t h)
{
    for (size_t i = 0; i < n; i++)
        if (stats[i].fn_handle == h) return &stats[i];
    return NULL;
}

static const char *sym_name(struct sym *syms, size_t n, uint64_t h)
{
    for (size_t i = 0; i < n; i++)
        if (syms[i].fn_handle == h) return syms[i].name;
    return "<unknown>";
}

static int cmd_report(int argc, char **argv)
{
    const char *in_file = NULL;
    int top = 20;
    int i = 0;
    while (i < argc) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            in_file = argv[i + 1]; i += 2;
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            top = atoi(argv[i + 1]); i += 2;
            if (top <= 0) top = 20;
        } else {
            fprintf(stderr, "yperf report: unknown flag %s\n", argv[i]);
            return 2;
        }
    }
    if (!in_file) in_file = YPERF_DEFAULT_FILE;

    FILE *f = fopen(in_file, "rb");
    if (!f) {
        fprintf(stderr, "yperf report: open %s: %s\n", in_file, strerror(errno));
        return 1;
    }

    struct yperf_file_header h;
    if (fread(&h, sizeof h, 1, f) != 1) {
        fprintf(stderr, "yperf report: short header in %s\n", in_file);
        fclose(f); return 1;
    }
    if (memcmp(h.magic, "YPERFv01", 8) != 0) {
        fprintf(stderr, "yperf report: bad magic in %s\n", in_file);
        fclose(f); return 1;
    }

    /* Load all records. */
    struct yperf_record *recs = calloc(h.num_records, sizeof *recs);
    if (!recs) { fprintf(stderr, "yperf report: oom\n"); fclose(f); return 1; }
    if (fseek(f, (long)h.records_offset, SEEK_SET) != 0 ||
        fread(recs, sizeof *recs, h.num_records, f) != h.num_records) {
        fprintf(stderr, "yperf report: short records\n");
        free(recs); fclose(f); return 1;
    }

    /* Load all symbols. */
    struct sym *syms = calloc(h.num_symbols, sizeof *syms);
    if (!syms) { fprintf(stderr, "yperf report: oom\n"); free(recs); fclose(f); return 1; }
    if (fseek(f, (long)h.symbols_offset, SEEK_SET) != 0) {
        fprintf(stderr, "yperf report: seek symbols\n");
        free(recs); free(syms); fclose(f); return 1;
    }
    for (uint64_t k = 0; k < h.num_symbols; k++) {
        uint32_t nlen;
        uint64_t handle;
        if (fread(&nlen,   sizeof nlen,   1, f) != 1 ||
            fread(&handle, sizeof handle, 1, f) != 1) {
            fprintf(stderr, "yperf report: short symbol[%" PRIu64 "]\n", k);
            free(recs); free(syms); fclose(f); return 1;
        }
        char *name = malloc(nlen + 1);
        if (!name || fread(name, 1, nlen, f) != nlen) {
            fprintf(stderr, "yperf report: short symbol name\n");
            free(name); free(recs); free(syms); fclose(f); return 1;
        }
        name[nlen] = 0;
        syms[k].fn_handle = handle;
        syms[k].name      = name;
    }
    fclose(f);

    /* Self/cum computation via the stack-walk described in the file
     * banner: records are written in exit-time order per thread, so
     * children of a record appear before it. We push every record
     * onto a temporary stack; when a record arrives that contains
     * the stack-top (enter <= top.enter and exit >= top.exit), we
     * absorb the top's cumulative time as a child of the new record.
     * Records from different threads can interleave, but a child
     * always satisfies `enter > parent.enter`, so the test is correct
     * across thread boundaries too.
     *
     * Aggregation: stats[] is keyed by fn_handle. Linear search
     * with a single-element last-hit cache; the call patterns we
     * see are bursty enough that this catches most lookups in O(1). */
    typedef struct { uint64_t enter, exit, child_sum, fn; } sframe;
    struct fn_stat *stats = calloc(h.num_records, sizeof *stats);
    sframe         *stack = calloc(h.num_records + 1, sizeof *stack);
    size_t nstats = 0;
    if (!stats || !stack) {
        fprintf(stderr, "yperf report: oom\n");
        free(stats); free(stack);
        for (uint64_t k = 0; k < h.num_symbols; k++) free(syms[k].name);
        free(syms); free(recs);
        return 1;
    }
    size_t sp = 0;

    uintptr_t last_h = 0;
    struct fn_stat *last_stat = NULL;

    uint64_t min_ts = UINT64_MAX, max_ts = 0;

    for (uint64_t k = 0; k < h.num_records; k++) {
        struct yperf_record *r = &recs[k];
        if (r->enter_ts_ns < min_ts) min_ts = r->enter_ts_ns;
        if (r->exit_ts_ns  > max_ts) max_ts = r->exit_ts_ns;

        /* Pop every stack frame whose [enter, exit] is strictly
         * inside this record's interval — those were nested calls. */
        uint64_t children_cum = 0;
        while (sp > 0
               && stack[sp - 1].enter >= r->enter_ts_ns
               && stack[sp - 1].exit  <= r->exit_ts_ns) {
            children_cum += (stack[sp - 1].exit - stack[sp - 1].enter);
            sp--;
        }

        uint64_t cum  = r->exit_ts_ns - r->enter_ts_ns;
        uint64_t self = cum > children_cum ? cum - children_cum : 0;

        /* Aggregate into per-function stats. */
        struct fn_stat *s = (last_h == r->fn_handle) ? last_stat : NULL;
        if (!s) s = find_stat(stats, nstats, r->fn_handle);
        if (!s) {
            s = &stats[nstats++];
            s->fn_handle = r->fn_handle;
        }
        s->calls   += 1;
        s->cum_ns  += cum;
        s->self_ns += self;
        last_h = r->fn_handle; last_stat = s;

        stack[sp].enter = r->enter_ts_ns;
        stack[sp].exit  = r->exit_ts_ns;
        stack[sp].fn    = r->fn_handle;
        stack[sp].child_sum = 0;
        sp++;
    }

    /* Sort stats by cum_ns descending — simple insertion sort,
     * nstats is bounded by distinct function count (hundreds). */
    for (size_t i = 1; i < nstats; i++) {
        struct fn_stat tmp = stats[i];
        size_t j = i;
        while (j > 0 && stats[j - 1].cum_ns < tmp.cum_ns) {
            stats[j] = stats[j - 1]; j--;
        }
        stats[j] = tmp;
    }

    /* Print perf-style header + table. */
    uint64_t total_cum = (max_ts > min_ts) ? (max_ts - min_ts) : 1;
    printf("# yperf report — %s\n", in_file);
    printf("# records=%" PRIu64 "  symbols=%" PRIu64
           "  overflow=%" PRIu64 "  distinct_fns=%zu\n",
           h.num_records, h.num_symbols, h.overflow_count, nstats);
    printf("# wall time (capture window): %.3f ms\n", total_cum / 1e6);
    printf("#\n");
    printf("# %-7s %-7s %-9s %-12s %-12s %-12s  %s\n",
           "Cum%", "Self%", "Calls", "Cum_us", "Self_us", "Avg_us", "Function");
    printf("# %-7s %-7s %-9s %-12s %-12s %-12s  %s\n",
           "-----", "-----", "-----", "------", "-------", "------", "--------");

    size_t shown = ((size_t)top < nstats) ? (size_t)top : nstats;
    for (size_t i = 0; i < shown; i++) {
        struct fn_stat *s = &stats[i];
        double cum_pct  = 100.0 * (double)s->cum_ns  / (double)total_cum;
        double self_pct = 100.0 * (double)s->self_ns / (double)total_cum;
        double cum_us   = s->cum_ns  / 1000.0;
        double self_us  = s->self_ns / 1000.0;
        double avg_us   = cum_us / (double)s->calls;
        const char *nm  = sym_name(syms, h.num_symbols, s->fn_handle);
        printf("  %6.2f%% %6.2f%% %9" PRIu64 " %12.2f %12.2f %12.2f  %s\n",
               cum_pct, self_pct, s->calls, cum_us, self_us, avg_us, nm);
    }
    if (shown < nstats)
        printf("# ... %zu more functions not shown (use -n to widen)\n",
               nstats - shown);

    free(stack);
    free(stats);
    for (uint64_t k = 0; k < h.num_symbols; k++) free(syms[k].name);
    free(syms);
    free(recs);
    return 0;
}

/* ── dispatch ────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    if (argc < 2 ||
        strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        usage();
        return argc < 2 ? 2 : 0;
    }
    if (strcmp(argv[1], "record") == 0) return cmd_record(argc - 2, argv + 2);
    if (strcmp(argv[1], "report") == 0) return cmd_report(argc - 2, argv + 2);
    fprintf(stderr, "yperf: unknown subcommand '%s'\n", argv[1]);
    usage();
    return 2;
}
