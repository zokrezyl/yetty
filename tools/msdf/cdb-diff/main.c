/*
 * cdb-diff — compare two yetty MSDF .cdb files glyph-by-glyph.
 *
 * Used to narrow down where the GPU (WGSL compute shader) MSDF generator
 * diverges from the CPU (msdfgen) reference. Reports per-glyph metrics:
 *
 *   max_diff   max |median3_a - median3_b| over all pixels (post-render SDF)
 *   mean_diff  mean |median3_a - median3_b| (per-pixel)
 *   bad_pct    pct of pixels where median3 crosses the 0.5 inside/outside line
 *              (i.e. a pixel that's "inside" in one file and "outside" in the
 *              other — the visual artifact a user actually sees)
 *
 * Walks the CDB file format directly, just like tools/cdb-viewer; no ycdb
 * dependency. Glyphs are read into an in-memory array per file, sorted by
 * codepoint, then merge-walked.
 */

#include <yetty/ymsdf-gen/ymsdf-gen.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CDB_HEADER_SIZE 2048

struct glyph {
	uint32_t cp;
	struct yetty_ymsdf_gen_glyph_header hdr;
	uint8_t *pixels;       /* RGBA8, width*height*4 bytes — owned */
	size_t pixel_bytes;
};

struct glyph_set {
	struct glyph *items;
	size_t n;
	size_t cap;
};

/* Which metric drives the per-glyph ranking shown in the top-N table.
 * Defaults to max_diff for backwards compat, but bad_pct is usually
 * more useful for hunting visible artefacts (max_diff saturates at
 * 1.0 for any single-pixel sign flip, so 100s of glyphs tie). */
enum sort_by {
	SORT_MAX  = 0, /* by max_diff   */
	SORT_BAD  = 1, /* by bad_pct    */
	SORT_MEAN = 2, /* by mean_diff  */
};

struct opts {
	const char *path_a;
	const char *path_b;
	int top_n;             /* show top-N worst glyphs in detail (default 20) */
	int preview;           /* render side-by-side ASCII diff for top-N */
	uint32_t cp_lo;
	uint32_t cp_hi;
	float bad_threshold;   /* pixel counts as "bad" if |Δmedian3| >= threshold */
	enum sort_by sort_by;
};

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
"usage: %s [options] <a.cdb> <b.cdb>\n"
"\n"
"Compare two yetty MSDF .cdb files and rank glyphs by divergence.\n"
"\n"
"options:\n"
"  -n N                show top-N worst glyphs in detail (default 20)\n"
"  -p, --preview       ASCII side-by-side preview of top-N worst glyphs\n"
"  -r, --range LO:HI   only compare codepoints in [LO,HI] (hex or dec)\n"
"  -t, --threshold X   pixel is 'bad' if |Δmedian3| >= X (default 0.1)\n"
"  -s, --sort KEY      rank by KEY descending: 'max' (default), 'bad', 'mean'.\n"
"                      'bad'  = pct of pixels exceeding the threshold —\n"
"                              best signal for visible artefacts since\n"
"                              max saturates at 1.0 for any single sign-flip.\n"
"                      'mean' = average per-pixel |Δmedian3| across the glyph.\n"
"  -h, --help          this help\n"
"\n"
"output: header summary, then per-glyph rows sorted by the chosen key.\n",
		prog);
}

static uint32_t rd_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0]
	     | ((uint32_t)p[1] << 8)
	     | ((uint32_t)p[2] << 16)
	     | ((uint32_t)p[3] << 24);
}

static void fmt_cp(uint32_t cp, char *buf, size_t buflen)
{
	if (cp >= 0x20 && cp < 0x7F) {
		snprintf(buf, buflen, "'%c'", (char)cp);
	} else if (cp <= 0xFFFF) {
		snprintf(buf, buflen, "<U+%04X>", cp);
	} else {
		snprintf(buf, buflen, "<U+%06X>", cp);
	}
}

static float median3(float r, float g, float b)
{
	float ab_max = (r > g) ? r : g;
	float ab_min = (r > g) ? g : r;
	float t = (ab_max < b) ? ab_max : b;
	return (ab_min > t) ? ab_min : t;
}

static int glyph_set_push(struct glyph_set *s, struct glyph g)
{
	if (s->n == s->cap) {
		size_t ncap = s->cap ? s->cap * 2 : 256;
		struct glyph *nb = realloc(s->items, ncap * sizeof(*nb));
		if (!nb) return -1;
		s->items = nb;
		s->cap = ncap;
	}
	s->items[s->n++] = g;
	return 0;
}

static void glyph_set_free(struct glyph_set *s)
{
	for (size_t i = 0; i < s->n; i++) {
		free(s->items[i].pixels);
	}
	free(s->items);
}

static int cmp_glyph(const void *a, const void *b)
{
	uint32_t ca = ((const struct glyph *)a)->cp;
	uint32_t cb = ((const struct glyph *)b)->cp;
	if (ca < cb) return -1;
	if (ca > cb) return 1;
	return 0;
}

/* Walk a CDB file and load every (codepoint, header, pixels) record into the
 * glyph_set. Codepoints outside [cp_lo, cp_hi] are skipped. */
static int load_cdb(const char *path, struct glyph_set *out, uint32_t cp_lo, uint32_t cp_hi)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return -1;
	}

	uint8_t header[CDB_HEADER_SIZE];
	if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
		fprintf(stderr, "%s: short read of CDB header\n", path);
		fclose(f);
		return -1;
	}

	uint32_t data_end = UINT32_MAX;
	for (int b = 0; b < 256; b++) {
		uint32_t off = rd_u32_le(header + b * 8);
		uint32_t cnt = rd_u32_le(header + b * 8 + 4);
		if (cnt == 0) continue;
		if (off < data_end) data_end = off;
	}
	if (data_end == UINT32_MAX || data_end < CDB_HEADER_SIZE) {
		fprintf(stderr, "%s: invalid CDB (no data section)\n", path);
		fclose(f);
		return -1;
	}

	uint32_t pos = CDB_HEADER_SIZE;
	int rc = 0;
	while (pos < data_end) {
		uint8_t lens[8];
		if (fread(lens, 1, sizeof(lens), f) != sizeof(lens)) { rc = -1; break; }
		uint32_t klen = rd_u32_le(lens);
		uint32_t vlen = rd_u32_le(lens + 4);
		if ((uint64_t)pos + 8 + klen + vlen > data_end) { rc = -1; break; }

		uint32_t cp = 0;
		if (klen != 4) {
			if (fseek(f, klen, SEEK_CUR) != 0) { rc = -1; break; }
		} else {
			uint8_t kbuf[4];
			if (fread(kbuf, 1, 4, f) != 4) { rc = -1; break; }
			cp = rd_u32_le(kbuf);
		}

		uint8_t *vbuf = malloc(vlen);
		if (!vbuf) { rc = -1; break; }
		if (fread(vbuf, 1, vlen, f) != vlen) { free(vbuf); rc = -1; break; }

		int in_range = (klen == 4) && cp >= cp_lo && cp <= cp_hi;
		if (in_range && vlen >= sizeof(struct yetty_ymsdf_gen_glyph_header)) {
			struct glyph g = {0};
			g.cp = cp;
			memcpy(&g.hdr, vbuf, sizeof(g.hdr));
			size_t pix_bytes = vlen - sizeof(g.hdr);
			size_t expect = (size_t)g.hdr.width * g.hdr.height * 4;
			if (pix_bytes == expect && pix_bytes > 0) {
				g.pixels = malloc(pix_bytes);
				if (!g.pixels) { free(vbuf); rc = -1; break; }
				memcpy(g.pixels, vbuf + sizeof(g.hdr), pix_bytes);
				g.pixel_bytes = pix_bytes;
				if (glyph_set_push(out, g) < 0) {
					free(g.pixels);
					free(vbuf);
					rc = -1;
					break;
				}
			} else if (pix_bytes == 0) {
				/* empty bitmap (e.g. space) — keep for "missing" detection */
				if (glyph_set_push(out, g) < 0) {
					free(vbuf);
					rc = -1;
					break;
				}
			}
		}
		free(vbuf);

		pos += 8 + klen + vlen;
	}
	fclose(f);
	if (rc == 0) {
		qsort(out->items, out->n, sizeof(*out->items), cmp_glyph);
	}
	return rc;
}

struct diff_row {
	uint32_t cp;
	uint16_t w_a, h_a;
	uint16_t w_b, h_b;
	float max_diff;
	float mean_diff;
	float bad_pct;          /* % pixels above threshold */
	int dim_mismatch;
};

/* Compare pixel data; assumes both bitmaps are same dimensions.
 * Diff metric is on median3(R,G,B) — the actual SDF value rendered to screen. */
static void compute_diff(const struct glyph *a, const struct glyph *b,
			 float threshold, struct diff_row *row)
{
	int w = a->hdr.width;
	int h = a->hdr.height;
	int n = w * h;
	if (n == 0) {
		row->max_diff = 0.0f;
		row->mean_diff = 0.0f;
		row->bad_pct = 0.0f;
		return;
	}
	double sum = 0.0;
	float maxd = 0.0f;
	int bad = 0;
	for (int i = 0; i < n; i++) {
		const uint8_t *pa = a->pixels + (size_t)i * 4;
		const uint8_t *pb = b->pixels + (size_t)i * 4;
		float ma = median3(pa[0] / 255.0f, pa[1] / 255.0f, pa[2] / 255.0f);
		float mb = median3(pb[0] / 255.0f, pb[1] / 255.0f, pb[2] / 255.0f);
		float d = fabsf(ma - mb);
		if (d > maxd) maxd = d;
		sum += d;
		if (d >= threshold) bad++;
	}
	row->max_diff = maxd;
	row->mean_diff = (float)(sum / n);
	row->bad_pct = (float)bad * 100.0f / (float)n;
}

static int cmp_row_max_desc(const void *a, const void *b)
{
	float fa = ((const struct diff_row *)a)->max_diff;
	float fb = ((const struct diff_row *)b)->max_diff;
	if (fa > fb) return -1;
	if (fa < fb) return 1;
	return 0;
}

static int cmp_row_bad_desc(const void *a, const void *b)
{
	float fa = ((const struct diff_row *)a)->bad_pct;
	float fb = ((const struct diff_row *)b)->bad_pct;
	if (fa > fb) return -1;
	if (fa < fb) return 1;
	/* tie-break by max_diff so output is stable when many glyphs are 0% */
	float ma = ((const struct diff_row *)a)->max_diff;
	float mb = ((const struct diff_row *)b)->max_diff;
	if (ma > mb) return -1;
	if (ma < mb) return 1;
	return 0;
}

static int cmp_row_mean_desc(const void *a, const void *b)
{
	float fa = ((const struct diff_row *)a)->mean_diff;
	float fb = ((const struct diff_row *)b)->mean_diff;
	if (fa > fb) return -1;
	if (fa < fb) return 1;
	return 0;
}

/* Render side-by-side ASCII previews of bitmap A | bitmap B | diff map.
 * Rows are halved for terminal aspect ratio. */
static void render_side_by_side(const struct glyph *a, const struct glyph *b)
{
	int w = a->hdr.width;
	int h = a->hdr.height;
	int target_w = (w > 32) ? 32 : w;
	int out_w = target_w;
	int out_h = (h * out_w + w - 1) / w;
	if (out_h > 1) out_h = (out_h + 1) / 2;
	if (out_h < 1) out_h = 1;

	printf("    %-*s   %-*s   %-*s\n",
	       out_w, "CPU (a)", out_w, "GPU (b)", out_w, "diff");
	for (int oy = 0; oy < out_h; oy++) {
		int sy0 = (int)((long)oy       * h / out_h);
		int sy1 = (int)((long)(oy + 1) * h / out_h);
		if (sy1 <= sy0) sy1 = sy0 + 1;

		fputs("    ", stdout);
		for (int side = 0; side < 3; side++) {
			for (int ox = 0; ox < out_w; ox++) {
				int sx0 = (int)((long)ox       * w / out_w);
				int sx1 = (int)((long)(ox + 1) * w / out_w);
				if (sx1 <= sx0) sx1 = sx0 + 1;
				float acc = 0.0f;
				int n = 0;
				for (int sy = sy0; sy < sy1; sy++) {
					for (int sx = sx0; sx < sx1; sx++) {
						const uint8_t *pa = a->pixels + ((size_t)sy * w + sx) * 4;
						const uint8_t *pb = b->pixels + ((size_t)sy * w + sx) * 4;
						float ma = median3(pa[0]/255.0f, pa[1]/255.0f, pa[2]/255.0f);
						float mb = median3(pb[0]/255.0f, pb[1]/255.0f, pb[2]/255.0f);
						float v;
						if (side == 0) v = ma;
						else if (side == 1) v = mb;
						else v = fabsf(ma - mb);
						acc += v;
						n++;
					}
				}
				float v = (n > 0) ? acc / (float)n : 0.0f;
				char c;
				if (side < 2) {
					if      (v >= 0.60f) c = '#';
					else if (v >= 0.50f) c = '+';
					else if (v >= 0.40f) c = '.';
					else                 c = ' ';
				} else {
					if      (v >= 0.40f) c = '#';
					else if (v >= 0.20f) c = '+';
					else if (v >= 0.10f) c = '.';
					else                 c = ' ';
				}
				putchar(c);
			}
			if (side < 2) fputs("   ", stdout);
		}
		putchar('\n');
	}
}

static int parse_range(const char *spec, uint32_t *lo, uint32_t *hi)
{
	const char *colon = strchr(spec, ':');
	if (!colon) return -1;
	char buf[64];
	size_t n = (size_t)(colon - spec);
	if (n >= sizeof(buf)) return -1;
	memcpy(buf, spec, n);
	buf[n] = '\0';
	char *end;
	unsigned long v = strtoul(buf, &end, 0);
	if (*end != '\0') return -1;
	*lo = (uint32_t)v;
	v = strtoul(colon + 1, &end, 0);
	if (*end != '\0') return -1;
	*hi = (uint32_t)v;
	return 0;
}

static int parse_args(int argc, char **argv, struct opts *o)
{
	o->cp_lo = 0;
	o->cp_hi = UINT32_MAX;
	o->top_n = 20;
	o->bad_threshold = 0.1f;
	int posi = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout, argv[0]);
			exit(0);
		} else if (!strcmp(a, "-n")) {
			if (++i >= argc) return -1;
			o->top_n = atoi(argv[i]);
		} else if (!strcmp(a, "-p") || !strcmp(a, "--preview")) {
			o->preview = 1;
		} else if (!strcmp(a, "-r") || !strcmp(a, "--range")) {
			if (++i >= argc) return -1;
			if (parse_range(argv[i], &o->cp_lo, &o->cp_hi) < 0) {
				fprintf(stderr, "%s: bad range %s\n", argv[0], argv[i]);
				return -1;
			}
		} else if (!strcmp(a, "-t") || !strcmp(a, "--threshold")) {
			if (++i >= argc) return -1;
			o->bad_threshold = (float)atof(argv[i]);
		} else if (!strcmp(a, "-s") || !strcmp(a, "--sort")) {
			if (++i >= argc) return -1;
			if      (!strcmp(argv[i], "max"))  o->sort_by = SORT_MAX;
			else if (!strcmp(argv[i], "bad"))  o->sort_by = SORT_BAD;
			else if (!strcmp(argv[i], "mean")) o->sort_by = SORT_MEAN;
			else {
				fprintf(stderr, "%s: --sort takes max | bad | mean\n", argv[0]);
				return -1;
			}
		} else if (a[0] == '-' && a[1] != '\0') {
			fprintf(stderr, "%s: unknown option %s\n", argv[0], a);
			return -1;
		} else if (!o->path_a) {
			o->path_a = a;
		} else if (!o->path_b) {
			o->path_b = a;
		} else {
			fprintf(stderr, "%s: extra argument %s\n", argv[0], a);
			return -1;
		}
		(void)posi;
	}
	if (!o->path_a || !o->path_b) {
		fprintf(stderr, "%s: need two .cdb paths\n", argv[0]);
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct opts o = {0};
	if (parse_args(argc, argv, &o) < 0) {
		usage(stderr, argv[0]);
		return 2;
	}

	struct glyph_set sa = {0}, sb = {0};
	if (load_cdb(o.path_a, &sa, o.cp_lo, o.cp_hi) < 0) {
		glyph_set_free(&sa);
		return 1;
	}
	if (load_cdb(o.path_b, &sb, o.cp_lo, o.cp_hi) < 0) {
		glyph_set_free(&sa);
		glyph_set_free(&sb);
		return 1;
	}

	printf("# a = %s   (%zu glyphs)\n", o.path_a, sa.n);
	printf("# b = %s   (%zu glyphs)\n", o.path_b, sb.n);
	printf("# threshold = %.3f (pixels with |Δmedian3| >= threshold count as 'bad')\n",
	       (double)o.bad_threshold);
	printf("\n");

	/* Merge-walk both sorted arrays. */
	size_t ia = 0, ib = 0;
	struct diff_row *rows = NULL;
	size_t rows_n = 0, rows_cap = 0;
	int only_a = 0, only_b = 0, dim_mismatch = 0;
	while (ia < sa.n || ib < sb.n) {
		uint32_t ca = (ia < sa.n) ? sa.items[ia].cp : UINT32_MAX;
		uint32_t cb = (ib < sb.n) ? sb.items[ib].cp : UINT32_MAX;
		if (ca < cb) {
			only_a++;
			ia++;
			continue;
		}
		if (cb < ca) {
			only_b++;
			ib++;
			continue;
		}
		/* same codepoint */
		const struct glyph *ga = &sa.items[ia];
		const struct glyph *gb = &sb.items[ib];

		if (rows_n == rows_cap) {
			size_t nc = rows_cap ? rows_cap * 2 : 256;
			struct diff_row *nb = realloc(rows, nc * sizeof(*nb));
			if (!nb) { fprintf(stderr, "OOM\n"); return 1; }
			rows = nb;
			rows_cap = nc;
		}
		struct diff_row r = {0};
		r.cp = ga->cp;
		r.w_a = ga->hdr.width;
		r.h_a = ga->hdr.height;
		r.w_b = gb->hdr.width;
		r.h_b = gb->hdr.height;

		if (ga->hdr.width != gb->hdr.width || ga->hdr.height != gb->hdr.height) {
			r.dim_mismatch = 1;
			dim_mismatch++;
		} else if (ga->pixels && gb->pixels) {
			compute_diff(ga, gb, o.bad_threshold, &r);
		}
		rows[rows_n++] = r;
		ia++;
		ib++;
	}

	printf("matched glyphs:    %zu\n", rows_n);
	printf("only in a:         %d\n", only_a);
	printf("only in b:         %d\n", only_b);
	printf("dim mismatches:    %d\n", dim_mismatch);

	/* Aggregate stats over comparable glyphs (no dim mismatch, both have pixels). */
	double sum_max = 0.0, sum_mean = 0.0, sum_bad = 0.0;
	float global_max = 0.0f;
	size_t cmp_n = 0;
	for (size_t i = 0; i < rows_n; i++) {
		if (rows[i].dim_mismatch) continue;
		if (rows[i].w_a == 0 || rows[i].h_a == 0) continue;
		sum_max += rows[i].max_diff;
		sum_mean += rows[i].mean_diff;
		sum_bad += rows[i].bad_pct;
		if (rows[i].max_diff > global_max) global_max = rows[i].max_diff;
		cmp_n++;
	}
	if (cmp_n > 0) {
		printf("comparable glyphs: %zu\n", cmp_n);
		printf("avg max_diff:      %.4f\n", sum_max / cmp_n);
		printf("avg mean_diff:     %.4f\n", sum_mean / cmp_n);
		printf("avg bad_pct:       %.2f%%\n", sum_bad / cmp_n);
		printf("global max_diff:   %.4f\n", (double)global_max);
	}
	printf("\n");

	/* Sort by the user-selected key (default: max_diff). */
	int (*cmp)(const void *, const void *) = cmp_row_max_desc;
	const char *sort_label = "max_diff";
	if (o.sort_by == SORT_BAD)  { cmp = cmp_row_bad_desc;  sort_label = "bad_pct";  }
	if (o.sort_by == SORT_MEAN) { cmp = cmp_row_mean_desc; sort_label = "mean_diff"; }
	qsort(rows, rows_n, sizeof(*rows), cmp);

	int top = o.top_n < (int)rows_n ? o.top_n : (int)rows_n;
	printf("=== top %d glyphs by %s ===\n", top, sort_label);
	printf("%-10s  %-9s  %-11s  %-11s  %-9s  %-9s  %-9s\n",
	       "cp", "char", "size_a", "size_b", "max_diff", "mean_diff", "bad_pct");
	for (int i = 0; i < top; i++) {
		const struct diff_row *r = &rows[i];
		char cpbuf[16];
		fmt_cp(r->cp, cpbuf, sizeof(cpbuf));
		if (r->dim_mismatch) {
			printf("0x%06X  %-9s  %4ux%-5u  %4ux%-5u  %-9s  %-9s  %-9s\n",
			       r->cp, cpbuf, r->w_a, r->h_a, r->w_b, r->h_b,
			       "DIM_MIS", "DIM_MIS", "DIM_MIS");
		} else {
			printf("0x%06X  %-9s  %4ux%-5u  %4ux%-5u  %-9.4f  %-9.4f  %-7.2f%%\n",
			       r->cp, cpbuf, r->w_a, r->h_a, r->w_b, r->h_b,
			       (double)r->max_diff, (double)r->mean_diff,
			       (double)r->bad_pct);
		}
	}

	if (o.preview) {
		printf("\n=== top %d preview ===\n", top);
		/* For preview we need original glyph data — find them in the sets.
		 * Both sets are sorted by cp, so binary-search. */
		for (int i = 0; i < top; i++) {
			const struct diff_row *r = &rows[i];
			if (r->dim_mismatch) continue;
			if (r->w_a == 0 || r->h_a == 0) continue;

			/* Binary search a */
			size_t lo = 0, hi = sa.n;
			while (lo < hi) {
				size_t m = lo + (hi - lo) / 2;
				if (sa.items[m].cp < r->cp) lo = m + 1;
				else hi = m;
			}
			if (lo >= sa.n || sa.items[lo].cp != r->cp) continue;
			const struct glyph *ga = &sa.items[lo];

			lo = 0; hi = sb.n;
			while (lo < hi) {
				size_t m = lo + (hi - lo) / 2;
				if (sb.items[m].cp < r->cp) lo = m + 1;
				else hi = m;
			}
			if (lo >= sb.n || sb.items[lo].cp != r->cp) continue;
			const struct glyph *gb = &sb.items[lo];

			char cpbuf[16];
			fmt_cp(r->cp, cpbuf, sizeof(cpbuf));
			printf("\n--- 0x%06X %s  max_diff=%.3f bad_pct=%.1f%% ---\n",
			       r->cp, cpbuf, (double)r->max_diff, (double)r->bad_pct);
			render_side_by_side(ga, gb);
		}
	}

	free(rows);
	glyph_set_free(&sa);
	glyph_set_free(&sb);
	return 0;
}
