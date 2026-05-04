/*
 * cdb-viewer — stream the contents of a yetty MSDF .cdb file to stdout.
 *
 * Each .cdb record is a glyph: key = codepoint (u32 LE), value =
 * yetty_ymsdf_gen_glyph_header (28 bytes, see yetty/ymsdf-gen/ymsdf-gen.h)
 * followed by width*height*4 bytes of RGBA8 MSDF pixels.
 *
 * The reader walks the CDB file directly (no ycdb dependency): the format's
 * data section runs from byte 2048 to the first per-bucket hash table, with
 * records stored sequentially as (klen u32 LE, vlen u32 LE, key, value).
 *
 * Output is one record per block, line-based, designed to pipe through
 * `less`, `grep`, etc.
 */

#include <yetty/ymsdf-gen/ymsdf-gen.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CDB_HEADER_SIZE 2048

struct opts {
	const char *path;
	int preview;          /* ASCII bitmap preview */
	int preview_full;     /* don't downsample */
	uint32_t cp_lo;       /* inclusive */
	uint32_t cp_hi;       /* inclusive */
	int summary_only;
};

static void usage(FILE *out, const char *prog)
{
	fprintf(out,
"usage: %s [options] <path-to.cdb>\n"
"\n"
"Stream every glyph record in a yetty MSDF .cdb file to stdout. Output is\n"
"line-based and pipe-friendly (less, grep, awk).\n"
"\n"
"options:\n"
"  -p, --preview              include an ASCII MSDF preview per glyph\n"
"  -P, --preview-full         like --preview, but no downsampling\n"
"  -r, --range LO:HI          only show codepoints in [LO,HI] (hex or dec)\n"
"  -s, --summary              skip per-record output, print stats only\n"
"  -h, --help                 this help\n",
		prog);
}

/* Parse a u32 from a 4-byte little-endian buffer. */
static uint32_t rd_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0]
	     | ((uint32_t)p[1] << 8)
	     | ((uint32_t)p[2] << 16)
	     | ((uint32_t)p[3] << 24);
}

/* Pretty-print a UTF-32 codepoint. ASCII printable as-is, others as <U+XXXX>. */
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

/* Median of three (matches the shader's median3). */
static float median3(float r, float g, float b)
{
	float ab_max = (r > g) ? r : g;
	float ab_min = (r > g) ? g : r;
	float t = (ab_max < b) ? ab_max : b;
	return (ab_min > t) ? ab_min : t;
}

/* Render the glyph as ASCII. RGBA8, MSDF: SDF value is median(R,G,B) in 0..1.
 * Inside-glyph: SDF >= 0.5. We render to roughly `target_w` columns; if that's
 * smaller than the bitmap we sample. Vertical samples halved (terminal cells
 * are ~2:1 tall:wide). */
static void render_preview(const struct yetty_ymsdf_gen_glyph_header *hdr,
			   const uint8_t *px, int target_w, int full)
{
	if (hdr->width == 0 || hdr->height == 0) {
		printf("    (empty glyph, no bitmap)\n");
		return;
	}
	int w = hdr->width;
	int h = hdr->height;
	int out_w = full ? w : (w > target_w ? target_w : w);
	int out_h = full ? h : (h * out_w + w - 1) / w;  /* preserve aspect */
	if (out_w < 1) out_w = 1;
	if (out_h < 1) out_h = 1;
	/* Account for cell aspect ~2:1 tall — halve rows when not full. */
	if (!full)
		out_h = (out_h + 1) / 2;
	if (out_h < 1) out_h = 1;

	for (int oy = 0; oy < out_h; oy++) {
		fputs("    ", stdout);
		int sy0 = (int)((long)oy       * h / out_h);
		int sy1 = (int)((long)(oy + 1) * h / out_h);
		if (sy1 <= sy0) sy1 = sy0 + 1;
		for (int ox = 0; ox < out_w; ox++) {
			int sx0 = (int)((long)ox       * w / out_w);
			int sx1 = (int)((long)(ox + 1) * w / out_w);
			if (sx1 <= sx0) sx1 = sx0 + 1;
			float acc = 0.0f;
			int n = 0;
			for (int sy = sy0; sy < sy1; sy++) {
				for (int sx = sx0; sx < sx1; sx++) {
					const uint8_t *p = px + ((size_t)sy * w + sx) * 4;
					float r = p[0] / 255.0f;
					float g = p[1] / 255.0f;
					float b = p[2] / 255.0f;
					acc += median3(r, g, b);
					n++;
				}
			}
			float sd = (n > 0) ? acc / (float)n : 0.0f;
			char c;
			if      (sd >= 0.60f) c = '#';
			else if (sd >= 0.50f) c = '+';   /* edge band */
			else if (sd >= 0.40f) c = '.';
			else                  c = ' ';
			putchar(c);
		}
		putchar('\n');
	}
}

/* Print one glyph record. */
static void print_record(uint32_t cp, const uint8_t *value, size_t vlen,
			 const struct opts *o)
{
	char cpbuf[16];
	fmt_cp(cp, cpbuf, sizeof(cpbuf));

	if (vlen < sizeof(struct yetty_ymsdf_gen_glyph_header)) {
		printf("cp=0x%06X %s  <truncated record: %zu bytes>\n",
		       cp, cpbuf, vlen);
		return;
	}

	struct yetty_ymsdf_gen_glyph_header hdr;
	memcpy(&hdr, value, sizeof(hdr));
	const uint8_t *px = value + sizeof(hdr);
	size_t pixels_expected = (size_t)hdr.width * hdr.height * 4;
	size_t pixels_actual   = vlen - sizeof(hdr);

	float ascent  = hdr.bearing_y;
	float descent = hdr.size_y - hdr.bearing_y;

	printf("cp=0x%06X %-9s "
	       "bmp=%ux%u size=(%.2f,%.2f) bear=(%.2f,%.2f) adv=%.2f "
	       "asc=%.2f desc=%.2f bytes=%zu\n",
	       cp, cpbuf,
	       hdr.width, hdr.height,
	       (double)hdr.size_x,    (double)hdr.size_y,
	       (double)hdr.bearing_x, (double)hdr.bearing_y,
	       (double)hdr.advance,
	       (double)ascent,        (double)descent,
	       vlen);

	if (hdr.codepoint != cp) {
		printf("  WARN: header.codepoint=0x%06X != key 0x%06X\n",
		       hdr.codepoint, cp);
	}
	if (pixels_actual != pixels_expected) {
		printf("  WARN: pixel bytes mismatch: have %zu, expected %zu\n",
		       pixels_actual, pixels_expected);
		return;
	}

	if (o->preview || o->preview_full)
		render_preview(&hdr, px, 64, o->preview_full);
}

struct stats {
	uint64_t count;
	uint32_t cp_min, cp_max;
	uint32_t w_min, w_max;
	uint32_t h_min, h_max;
	float ascent_max;
	float descent_max;
	float adv_min, adv_max;
	uint64_t total_pixel_bytes;
};

static void stats_init(struct stats *s)
{
	memset(s, 0, sizeof(*s));
	s->cp_min = UINT32_MAX;
	s->w_min = UINT32_MAX;
	s->h_min = UINT32_MAX;
	s->adv_min = 1e30f;
}

static void stats_update(struct stats *s, uint32_t cp,
			 const struct yetty_ymsdf_gen_glyph_header *hdr,
			 size_t pixel_bytes)
{
	s->count++;
	if (cp < s->cp_min) s->cp_min = cp;
	if (cp > s->cp_max) s->cp_max = cp;
	if (hdr->width  > 0 && hdr->width  < s->w_min) s->w_min = hdr->width;
	if (hdr->width  > s->w_max) s->w_max = hdr->width;
	if (hdr->height > 0 && hdr->height < s->h_min) s->h_min = hdr->height;
	if (hdr->height > s->h_max) s->h_max = hdr->height;
	if (hdr->advance > 0.0f) {
		if (hdr->advance < s->adv_min) s->adv_min = hdr->advance;
		if (hdr->advance > s->adv_max) s->adv_max = hdr->advance;
	}
	float asc  = hdr->bearing_y;
	float desc = hdr->size_y - hdr->bearing_y;
	if (asc  > s->ascent_max)  s->ascent_max  = asc;
	if (desc > s->descent_max) s->descent_max = desc;
	s->total_pixel_bytes += pixel_bytes;
}

static void stats_print(const struct stats *s)
{
	printf("\n=== summary ===\n");
	printf("records:        %lu\n", (unsigned long)s->count);
	if (s->count == 0) return;
	printf("codepoints:     0x%06X .. 0x%06X\n", s->cp_min, s->cp_max);
	printf("bitmap width:   %u .. %u px\n", s->w_min, s->w_max);
	printf("bitmap height:  %u .. %u px\n", s->h_min, s->h_max);
	printf("advance:        %.2f .. %.2f (CDB units)\n",
	       (double)s->adv_min, (double)s->adv_max);
	printf("max ascent:     %.2f (above baseline, CDB units)\n",
	       (double)s->ascent_max);
	printf("max descent:    %.2f (below baseline, CDB units)\n",
	       (double)s->descent_max);
	printf("ascent+descent: %.2f\n",
	       (double)(s->ascent_max + s->descent_max));
	printf("pixel bytes:    %lu\n", (unsigned long)s->total_pixel_bytes);
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
	int i = 1;
	for (; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] != '-' || a[1] == '\0') break;
		if (!strcmp(a, "--")) { i++; break; }
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout, argv[0]);
			exit(0);
		} else if (!strcmp(a, "-p") || !strcmp(a, "--preview")) {
			o->preview = 1;
		} else if (!strcmp(a, "-P") || !strcmp(a, "--preview-full")) {
			o->preview = 1;
			o->preview_full = 1;
		} else if (!strcmp(a, "-s") || !strcmp(a, "--summary")) {
			o->summary_only = 1;
		} else if (!strcmp(a, "-r") || !strcmp(a, "--range")) {
			if (++i >= argc) {
				fprintf(stderr, "%s: --range needs LO:HI\n", argv[0]);
				return -1;
			}
			if (parse_range(argv[i], &o->cp_lo, &o->cp_hi) < 0) {
				fprintf(stderr, "%s: bad range %s\n", argv[0], argv[i]);
				return -1;
			}
		} else {
			fprintf(stderr, "%s: unknown option %s\n", argv[0], a);
			return -1;
		}
	}
	if (i >= argc) {
		fprintf(stderr, "%s: missing <path-to.cdb>\n", argv[0]);
		return -1;
	}
	o->path = argv[i];
	return 0;
}

int main(int argc, char **argv)
{
	struct opts o = {0};
	if (parse_args(argc, argv, &o) < 0) {
		usage(stderr, argv[0]);
		return 2;
	}

	FILE *f = fopen(o.path, "rb");
	if (!f) {
		fprintf(stderr, "open %s: %s\n", o.path, strerror(errno));
		return 1;
	}

	uint8_t header[CDB_HEADER_SIZE];
	if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
		fprintf(stderr, "%s: short read of CDB header\n", o.path);
		fclose(f);
		return 1;
	}

	/* The data section ends at the first byte of any per-bucket hash
	 * table. Each bucket entry is (offset_u32_LE, count_u32_LE); buckets
	 * with count 0 still have a (possibly garbage) offset that doesn't
	 * point at a real table — skip those. */
	uint32_t data_end = UINT32_MAX;
	for (int b = 0; b < 256; b++) {
		uint32_t off = rd_u32_le(header + b * 8);
		uint32_t cnt = rd_u32_le(header + b * 8 + 4);
		if (cnt == 0) continue;
		if (off < data_end) data_end = off;
	}
	if (data_end == UINT32_MAX) {
		fprintf(stderr, "%s: empty CDB (no buckets populated)\n", o.path);
		fclose(f);
		return 1;
	}
	if (data_end < CDB_HEADER_SIZE) {
		fprintf(stderr, "%s: invalid CDB (data_end=0x%X < header)\n",
			o.path, data_end);
		fclose(f);
		return 1;
	}

	if (!o.summary_only) {
		printf("# %s\n", o.path);
		printf("# data section: 0x%08X .. 0x%08X (%u bytes)\n",
		       (unsigned)CDB_HEADER_SIZE, (unsigned)data_end,
		       (unsigned)(data_end - CDB_HEADER_SIZE));
	}

	struct stats stats;
	stats_init(&stats);

	uint32_t pos = CDB_HEADER_SIZE;
	uint8_t *value_buf = NULL;
	size_t value_cap = 0;
	int rc = 0;

	while (pos < data_end) {
		uint8_t lens[8];
		if (fread(lens, 1, sizeof(lens), f) != sizeof(lens)) {
			fprintf(stderr, "short read at offset 0x%X\n", pos);
			rc = 1;
			break;
		}
		uint32_t klen = rd_u32_le(lens);
		uint32_t vlen = rd_u32_le(lens + 4);

		if ((uint64_t)pos + 8 + klen + vlen > data_end) {
			fprintf(stderr,
				"record at 0x%X exceeds data section "
				"(klen=%u vlen=%u)\n", pos, klen, vlen);
			rc = 1;
			break;
		}

		uint32_t cp = 0;
		if (klen != 4) {
			fprintf(stderr,
				"WARN: record at 0x%X has klen=%u "
				"(expected 4 for codepoint)\n", pos, klen);
			/* Skip key bytes we can't interpret */
			if (fseek(f, klen, SEEK_CUR) != 0) { rc = 1; break; }
		} else {
			uint8_t kbuf[4];
			if (fread(kbuf, 1, 4, f) != 4) { rc = 1; break; }
			cp = rd_u32_le(kbuf);
		}

		if (vlen > value_cap) {
			uint8_t *nb = realloc(value_buf, vlen);
			if (!nb) {
				fprintf(stderr, "alloc %u bytes failed\n", vlen);
				rc = 1;
				break;
			}
			value_buf = nb;
			value_cap = vlen;
		}
		if (fread(value_buf, 1, vlen, f) != vlen) {
			fprintf(stderr, "short read of value at 0x%X\n", pos);
			rc = 1;
			break;
		}

		int in_range = (klen == 4) && cp >= o.cp_lo && cp <= o.cp_hi;
		if (in_range) {
			if (vlen >= sizeof(struct yetty_ymsdf_gen_glyph_header)) {
				struct yetty_ymsdf_gen_glyph_header hdr;
				memcpy(&hdr, value_buf, sizeof(hdr));
				size_t pix = vlen - sizeof(hdr);
				stats_update(&stats, cp, &hdr, pix);
			}
			if (!o.summary_only)
				print_record(cp, value_buf, vlen, &o);
		}

		pos += 8 + klen + vlen;
	}

	free(value_buf);
	fclose(f);
	stats_print(&stats);
	return rc;
}
