/*
 * term-grid-scan POC
 *
 * Benchmarks scanning a yetty text-grid for shader-glyph cells.
 *
 * Cell layout matches src/yetty/yvterm/text-layer.wgsl (12 bytes / cell):
 *     u32[0] = glyph_index    <-- the field we scan
 *     u32[1] = fg | bg.r<<24
 *     u32[2] = bg.gb | attrs<<16
 *
 * "Shader glyph" cells are those with (glyph_index & 0x80000000) != 0
 * i.e. the high bit (top half of the 32-bit index space) is reserved.
 *
 * Each frame:
 *   1. Mutate ~mutate_pct of cells (PRNG; preserves the chosen density).
 *   2. Scan whole grid, collect (row,col) of every shader-glyph cell.
 *   3. Time only the scan.
 *
 * Variants: scalar branchless / AVX2 (gather) / NEON (vld3q_u32).
 */

#define _POSIX_C_SOURCE 199309L

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#define SHADER_GLYPH_BIT 0x80000000u

struct cell {
	uint32_t glyph;
	uint32_t fg_bgr;
	uint32_t bgg_bgb_attrs;
};
_Static_assert(sizeof(struct cell) == 12, "cell must be 12 bytes (matches WGSL)");

struct config {
	uint32_t rows;
	uint32_t cols;
	uint32_t frames;
	uint32_t warmup;
	double   density;
	double   mutate_pct;
	uint32_t seed;
	const char *which;  /* "all" | "scalar" | "avx2" | "neon" */
};

/* xorshift32 — fast deterministic PRNG */
static inline uint32_t xs32(uint32_t *s)
{
	uint32_t x = *s;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

/* Generate a glyph value where p(shader) == density. */
static inline uint32_t gen_glyph(uint32_t *prng, uint32_t threshold)
{
	uint32_t r1 = xs32(prng);
	uint32_t r2 = xs32(prng);
	if (r2 < threshold)
		return r1 | SHADER_GLYPH_BIT;
	return r1 & ~SHADER_GLYPH_BIT;
}

static void grid_init(struct cell *grid, size_t n, double density, uint32_t seed)
{
	uint32_t prng = seed ? seed : 1;
	uint32_t threshold = (uint32_t)(density * 4294967296.0);
	if (density >= 1.0) threshold = UINT32_MAX;
	for (size_t i = 0; i < n; i++) {
		grid[i].glyph = gen_glyph(&prng, threshold);
		grid[i].fg_bgr = xs32(&prng);
		grid[i].bgg_bgb_attrs = xs32(&prng);
	}
}

/* Mutate `mutate_count` random cells. Density is preserved on average. */
static void grid_mutate(struct cell *grid, size_t n, size_t mutate_count,
			uint32_t threshold, uint32_t *prng)
{
	for (size_t k = 0; k < mutate_count; k++) {
		uint32_t r = xs32(prng);
		size_t i = (size_t)r % n;  /* good enough for benchmark */
		grid[i].glyph = gen_glyph(prng, threshold);
	}
}

/* ===================================================================
 * Scan implementations
 * Return count of hits; positions written to out_idx[0..count).
 * =================================================================== */

static size_t scan_scalar(const struct cell *cells, size_t n, uint32_t *out_idx)
{
	size_t count = 0;
	for (size_t i = 0; i < n; i++) {
		uint32_t g = cells[i].glyph;
		out_idx[count] = (uint32_t)i;
		count += (g >> 31);  /* branchless: 1 if high bit set */
	}
	return count;
}

#if defined(__AVX2__)
/* AVX2 v1: gather. High-latency, splits into 8 µops, gets bound by load-port. */
static size_t scan_avx2(const struct cell *cells, size_t n, uint32_t *out_idx)
{
	size_t count = 0;
	const uint32_t *base = (const uint32_t *)cells;

	/* Cell stride is 12B = 3 u32s. Gather 8 glyphs at u32-indices
	 * { 0, 3, 6, 9, 12, 15, 18, 21 } from base + i*3. */
	const __m256i indices = _mm256_setr_epi32(0, 3, 6, 9, 12, 15, 18, 21);

	size_t n_aligned = (n / 8) * 8;
	size_t i = 0;
	for (; i < n_aligned; i += 8) {
		__m256i v = _mm256_i32gather_epi32(
			(const int *)(base + i * 3), indices, 4);
		/* Extract sign bits — bit 31 set <=> shader glyph. */
		int bits = _mm256_movemask_ps(_mm256_castsi256_ps(v));
		while (bits) {
			int b = __builtin_ctz((unsigned)bits);
			out_idx[count++] = (uint32_t)(i + (size_t)b);
			bits &= bits - 1;
		}
	}
	for (; i < n; i++) {
		if (cells[i].glyph & SHADER_GLYPH_BIT)
			out_idx[count++] = (uint32_t)i;
	}
	return count;
}

/* AVX2 v2: direct loads + movemask, manual bit shuffle (no PEXT).
 *
 * 8 cells span exactly 96 bytes (8 * 12). Load three 32B vectors:
 *
 *   A (bytes  0..31)  u32 lanes: { g0, fg0, bg0, g1, fg1, bg1, g2, fg2 }
 *   B (bytes 32..63)  u32 lanes: { bg2, g3, fg3, bg3, g4, fg4, bg4, g5 }
 *   C (bytes 64..95)  u32 lanes: { fg5, bg5, g6, fg6, bg6, g7, fg7, bg7 }
 *
 * vmovmskps gives the sign bit of each u32 lane. Glyph sign bits live at:
 *   A: lanes 0, 3, 6   → glyphs 0, 1, 2
 *   B: lanes 1, 4, 7   → glyphs 3, 4, 5
 *   C: lanes 2, 5      → glyphs 6, 7
 *
 * Avoid PEXT (microcoded on Zen2 at ~40 cycles). Use plain shifts/ANDs. */
static size_t scan_avx2_v2(const struct cell *cells, size_t n, uint32_t *out_idx)
{
	size_t count = 0;
	const uint8_t *bytes = (const uint8_t *)cells;

	size_t n_aligned = (n / 8) * 8;
	size_t i = 0;
	for (; i < n_aligned; i += 8) {
		const uint8_t *p = bytes + i * 12;

		__m256i a = _mm256_loadu_si256((const __m256i *)(p +  0));
		__m256i b = _mm256_loadu_si256((const __m256i *)(p + 32));
		__m256i c = _mm256_loadu_si256((const __m256i *)(p + 64));

		uint32_t mA = (uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(a));
		uint32_t mB = (uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(b));
		uint32_t mC = (uint32_t)_mm256_movemask_ps(_mm256_castsi256_ps(c));

		/* Pack chosen bits into a contiguous 8-bit glyph mask. */
		uint32_t bits =
			((mA     ) & 0x01u) |   /* mA bit 0 -> bit 0 (g0) */
			((mA >> 2) & 0x02u) |   /* mA bit 3 -> bit 1 (g1) */
			((mA >> 4) & 0x04u) |   /* mA bit 6 -> bit 2 (g2) */
			((mB << 2) & 0x08u) |   /* mB bit 1 -> bit 3 (g3) */
			((mB     ) & 0x10u) |   /* mB bit 4 -> bit 4 (g4) */
			((mB >> 2) & 0x20u) |   /* mB bit 7 -> bit 5 (g5) */
			((mC << 4) & 0x40u) |   /* mC bit 2 -> bit 6 (g6) */
			((mC << 2) & 0x80u);    /* mC bit 5 -> bit 7 (g7) */

		while (bits) {
			int b_idx = __builtin_ctz(bits);
			out_idx[count++] = (uint32_t)(i + (size_t)b_idx);
			bits &= bits - 1;
		}
	}
	for (; i < n; i++) {
		if (cells[i].glyph & SHADER_GLYPH_BIT)
			out_idx[count++] = (uint32_t)i;
	}
	return count;
}
#endif

#if defined(__ARM_NEON)
static size_t scan_neon(const struct cell *cells, size_t n, uint32_t *out_idx)
{
	size_t count = 0;
	const uint32_t *base = (const uint32_t *)cells;
	const uint32x4_t weights = { 1, 2, 4, 8 };

	size_t n_aligned = (n / 4) * 4;
	size_t i = 0;
	for (; i < n_aligned; i += 4) {
		/* vld3q_u32 deinterleaves stride-3 u32 stream — free for
		 * our 12B cell layout. .val[0] = 4 glyph_indices. */
		uint32x4x3_t v = vld3q_u32(base + i * 3);
		uint32x4_t signs = vshrq_n_u32(v.val[0], 31);
		uint32x4_t weighted = vmulq_u32(signs, weights);
		uint32_t bitmask = vaddvq_u32(weighted);  /* AArch64 */
		while (bitmask) {
			int b = __builtin_ctz(bitmask);
			out_idx[count++] = (uint32_t)(i + (size_t)b);
			bitmask &= bitmask - 1;
		}
	}
	for (; i < n; i++) {
		if (cells[i].glyph & SHADER_GLYPH_BIT)
			out_idx[count++] = (uint32_t)i;
	}
	return count;
}
#endif

/* ===================================================================
 * Timing & stats
 * =================================================================== */

static inline uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

struct stats {
	double avg_us;
	double min_us;
	double max_us;
	double p50_us;
	double p99_us;
	double cells_per_sec;
	uint64_t total_hits;
};

static struct stats compute_stats(uint64_t *ns, uint32_t frames, size_t cells)
{
	qsort(ns, frames, sizeof(uint64_t), cmp_u64);
	uint64_t sum = 0;
	for (uint32_t i = 0; i < frames; i++) sum += ns[i];

	struct stats s = {0};
	s.avg_us = (double)sum / (double)frames / 1000.0;
	s.min_us = (double)ns[0] / 1000.0;
	s.max_us = (double)ns[frames - 1] / 1000.0;
	s.p50_us = (double)ns[frames / 2] / 1000.0;
	s.p99_us = (double)ns[(uint32_t)((double)frames * 0.99)] / 1000.0;
	s.cells_per_sec = (double)cells * 1e9 / ((double)sum / (double)frames);
	return s;
}

/* ===================================================================
 * Runner
 * =================================================================== */

typedef size_t (*scan_fn)(const struct cell *, size_t, uint32_t *);

static struct stats run_one(const char *name, scan_fn fn,
			    struct cell *grid, size_t n,
			    uint32_t *positions, uint64_t *ns_buf,
			    const struct config *cfg, uint32_t threshold)
{
	uint32_t prng = cfg->seed + 0xA5A5A5A5u;  /* offset so mutation differs from init */
	size_t mutate_count = (size_t)((double)n * cfg->mutate_pct);
	if (mutate_count == 0) mutate_count = 1;

	/* Warmup */
	for (uint32_t f = 0; f < cfg->warmup; f++) {
		grid_mutate(grid, n, mutate_count, threshold, &prng);
		(void)fn(grid, n, positions);
	}

	uint64_t total_hits = 0;
	for (uint32_t f = 0; f < cfg->frames; f++) {
		grid_mutate(grid, n, mutate_count, threshold, &prng);
		uint64_t t0 = now_ns();
		size_t hits = fn(grid, n, positions);
		uint64_t t1 = now_ns();
		ns_buf[f] = t1 - t0;
		total_hits += hits;
	}

	struct stats s = compute_stats(ns_buf, cfg->frames, n);
	s.total_hits = total_hits;

	double measured_density = (double)total_hits / ((double)cfg->frames * (double)n);
	printf("%-8s avg=%8.2f us  min=%8.2f  p50=%8.2f  p99=%8.2f  max=%8.2f  "
	       "%6.2f Mcells/s  density=%.4f\n",
	       name, s.avg_us, s.min_us, s.p50_us, s.p99_us, s.max_us,
	       s.cells_per_sec / 1e6, measured_density);
	return s;
}

/* ===================================================================
 * CLI
 * =================================================================== */

static void usage(const char *prog)
{
	fprintf(stderr,
"Usage: %s [options]\n"
"  --rows N             grid rows (default 200)\n"
"  --cols N             grid cols (default 1000)\n"
"  --frames N           timed frames (default 1000)\n"
"  --warmup N           warmup frames (default 50)\n"
"  --shader-density F   fraction of cells in shader range (default 0.01)\n"
"  --mutate-pct F       fraction of cells mutated per frame (default 0.05)\n"
"  --seed N             PRNG seed (default 1)\n"
"  --scan WHICH         scalar | avx2 | neon | all (default all)\n",
	prog);
}

static int parse_args(int argc, char **argv, struct config *cfg)
{
	cfg->rows = 200;
	cfg->cols = 1000;
	cfg->frames = 1000;
	cfg->warmup = 50;
	cfg->density = 0.01;
	cfg->mutate_pct = 0.05;
	cfg->seed = 1;
	cfg->which = "all";

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); exit(0); }
		if (!v) { fprintf(stderr, "missing value for %s\n", a); return -1; }
		if      (!strcmp(a, "--rows"))           cfg->rows = (uint32_t)strtoul(v, NULL, 10);
		else if (!strcmp(a, "--cols"))           cfg->cols = (uint32_t)strtoul(v, NULL, 10);
		else if (!strcmp(a, "--frames"))         cfg->frames = (uint32_t)strtoul(v, NULL, 10);
		else if (!strcmp(a, "--warmup"))         cfg->warmup = (uint32_t)strtoul(v, NULL, 10);
		else if (!strcmp(a, "--shader-density")) cfg->density = strtod(v, NULL);
		else if (!strcmp(a, "--mutate-pct"))     cfg->mutate_pct = strtod(v, NULL);
		else if (!strcmp(a, "--seed"))           cfg->seed = (uint32_t)strtoul(v, NULL, 10);
		else if (!strcmp(a, "--scan"))           cfg->which = v;
		else { fprintf(stderr, "unknown arg: %s\n", a); return -1; }
		i++;
	}
	if (cfg->rows == 0 || cfg->cols == 0 || cfg->frames == 0) {
		fprintf(stderr, "rows/cols/frames must be > 0\n");
		return -1;
	}
	return 0;
}

static bool want(const char *which, const char *name)
{
	return !strcmp(which, "all") || !strcmp(which, name);
}

int main(int argc, char **argv)
{
	struct config cfg;
	if (parse_args(argc, argv, &cfg) < 0) return 1;

	size_t n = (size_t)cfg.rows * (size_t)cfg.cols;
	struct cell *grid = aligned_alloc(64, ((n * sizeof(struct cell) + 63) / 64) * 64);
	uint32_t *positions = malloc(n * sizeof(uint32_t));
	uint64_t *ns_buf = malloc(cfg.frames * sizeof(uint64_t));
	if (!grid || !positions || !ns_buf) {
		fprintf(stderr, "alloc failed\n");
		return 1;
	}

	grid_init(grid, n, cfg.density, cfg.seed);
	uint32_t threshold = (uint32_t)(cfg.density * 4294967296.0);
	if (cfg.density >= 1.0) threshold = UINT32_MAX;

	printf("grid=%ux%u (%zu cells, %.2f MB)  density=%.4f  mutate=%.2f%%  "
	       "frames=%u (warmup=%u)  seed=%u\n",
	       cfg.rows, cfg.cols, n,
	       (double)(n * sizeof(struct cell)) / (1024.0 * 1024.0),
	       cfg.density, cfg.mutate_pct * 100.0, cfg.frames, cfg.warmup, cfg.seed);
	printf("variant   ----time per frame----                                   throughput     hits\n");

	if (want(cfg.which, "scalar"))
		(void)run_one("scalar", scan_scalar, grid, n, positions, ns_buf, &cfg, threshold);

#if defined(__AVX2__)
	if (want(cfg.which, "avx2"))
		(void)run_one("avx2", scan_avx2, grid, n, positions, ns_buf, &cfg, threshold);
	if (want(cfg.which, "avx2v2") || want(cfg.which, "avx2_v2"))
		(void)run_one("avx2v2", scan_avx2_v2, grid, n, positions, ns_buf, &cfg, threshold);
#else
	if (!strcmp(cfg.which, "avx2") || !strcmp(cfg.which, "avx2v2")) {
		fprintf(stderr, "avx2 not built (target lacks __AVX2__)\n");
	}
#endif

#if defined(__ARM_NEON)
	if (want(cfg.which, "neon"))
		(void)run_one("neon", scan_neon, grid, n, positions, ns_buf, &cfg, threshold);
#else
	if (!strcmp(cfg.which, "neon")) {
		fprintf(stderr, "neon not built (target lacks __ARM_NEON)\n");
	}
#endif

	free(grid);
	free(positions);
	free(ns_buf);
	return 0;
}
