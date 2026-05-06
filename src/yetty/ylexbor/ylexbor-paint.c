/*
 * ylexbor-paint — emit ypaint primitives from a laid-out box vector.
 *
 * Every YL_BOX_BLOCK with non-zero alpha background → ysdf box.
 * Every YL_BOX_INLINE_TEXT → TEXT_SPAN flyweight prim.
 * Every YL_BOX_INLINE_IMAGE → yimage prim with decoded pixels (or grey
 *                             placeholder when the fetch/decode fails).
 *
 * Image decoding: per-document cache keyed by the resolved absolute
 * URL of `<img src>`. Cache hits skip the fetch + decode. The same
 * decoded pixel buffer is re-serialized into a fresh yimage prim each
 * paint call, since ypaint takes ownership of the prim bytes via
 * add_prim — we keep the cache copy alive for repaints.
 *
 * Color packing matches what ypaint's shader expects: low byte = R,
 * high byte = A. Same convention ynetsurf-plotters.c uses.
 */

#include "ylexbor-internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if YETTY_HAVE_LIBPNG
#include <png.h>
#endif
#include <stb_image.h>
#if YETTY_HAVE_TURBOJPEG
#include <turbojpeg.h>
#endif

#include <yetty/ypaint-core/buffer.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/yimage/yimage-gen.h>


static uint32_t pack_rgba(struct yetty_ylexbor_color c)
{
	if (c.a == 0) return 0;
	return ((uint32_t)c.r) | ((uint32_t)c.g << 8) |
	       ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

#if YETTY_HAVE_LIBPNG
/* Decode a PNG byte stream into RGBA8 via libpng's simplified API
 * (png_image_*). Allocates *out_pixels (caller frees with free()).
 * Returns 1 on success, 0 on failure.
 *
 * libpng handles the full PNG feature surface — interlaced, palette,
 * 16-bit channel, color-profile-tagged — that stb_image's PNG decoder
 * either fails or mishandles on real-world payloads. The simplified
 * API normalizes everything to PNG_FORMAT_RGBA (matching the wire
 * format we feed yimage). */
static int decode_png(const uint8_t *bytes, size_t len,
		      uint32_t **out_pixels, int *out_w, int *out_h)
{
	png_image image = {0};
	image.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&image, bytes, len)) {
		png_image_free(&image);
		return 0;
	}
	image.format = PNG_FORMAT_RGBA;	/* normalize all variants to 8-bit RGBA */
	size_t total = (size_t)PNG_IMAGE_SIZE(image);
	if (total == 0 || image.width == 0 || image.height == 0) {
		png_image_free(&image);
		return 0;
	}
	uint32_t *pixels = malloc(total);
	if (!pixels) {
		png_image_free(&image);
		return 0;
	}
	if (!png_image_finish_read(&image, NULL, pixels,
				   /*row_stride=*/0, NULL)) {
		free(pixels);
		png_image_free(&image);
		return 0;
	}
	*out_pixels = pixels;
	*out_w = (int)image.width;
	*out_h = (int)image.height;
	png_image_free(&image);
	return 1;
}
#endif

#if YETTY_HAVE_TURBOJPEG
/* Decode a JPEG byte stream into RGBA8 via libjpeg-turbo. Same
 * malloc/free contract as decode_png. We use TJPF_RGBA so the byte
 * order matches what yimage stores (little-endian RGBA). */
static int decode_jpeg(const uint8_t *bytes, size_t len,
		       uint32_t **out_pixels, int *out_w, int *out_h)
{
	tjhandle d = tjInitDecompress();
	if (!d) return 0;
	int w = 0, h = 0, subsamp = 0, cs = 0;
	if (tjDecompressHeader3(d, bytes, (unsigned long)len,
				&w, &h, &subsamp, &cs) != 0 ||
	    w <= 0 || h <= 0) {
		tjDestroy(d);
		return 0;
	}
	uint32_t *pixels = malloc((size_t)w * (size_t)h * 4);
	if (!pixels) {
		tjDestroy(d);
		return 0;
	}
	if (tjDecompress2(d, bytes, (unsigned long)len,
			  (unsigned char *)pixels, w, /*pitch=*/0, h,
			  TJPF_RGBA, TJFLAG_FASTDCT) != 0) {
		free(pixels);
		tjDestroy(d);
		return 0;
	}
	tjDestroy(d);
	*out_pixels = pixels;
	*out_w = w;
	*out_h = h;
	return 1;
}
#endif

/* Decode any other format (GIF, BMP, WebP via stb's loader, …) with
 * stb_image. Kept as a fallback so we don't lose support for the
 * formats libpng/turbojpeg don't handle. */
static int decode_other(const uint8_t *bytes, size_t len,
			uint32_t **out_pixels, int *out_w, int *out_h)
{
	int w = 0, h = 0, channels = 0;
	stbi_uc *pixels = stbi_load_from_memory(
		(const stbi_uc *)bytes, (int)len, &w, &h, &channels, 4);
	if (!pixels || w <= 0 || h <= 0) {
		if (pixels) stbi_image_free(pixels);
		return 0;
	}
	size_t npx = (size_t)w * (size_t)h;
	uint32_t *out = malloc(npx * sizeof(uint32_t));
	if (!out) {
		stbi_image_free(pixels);
		return 0;
	}
	memcpy(out, pixels, npx * sizeof(uint32_t));
	stbi_image_free(pixels);
	*out_pixels = out;
	*out_w = w;
	*out_h = h;
	return 1;
}

/* Identify image format by magic bytes and dispatch to the right
 * decoder. Returns 1 on success. */
static int decode_image(const uint8_t *bytes, size_t len,
			uint32_t **out_pixels, int *out_w, int *out_h)
{
	if (len >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' &&
	    bytes[2] == 'N' && bytes[3] == 'G' && bytes[4] == 0x0D &&
	    bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
#if YETTY_HAVE_LIBPNG
		if (decode_png(bytes, len, out_pixels, out_w, out_h)) return 1;
#endif
		/* PNG header but libpng absent or failed — fall back
		 * to stb (which tolerates some PNGs libpng rejects
		 * strictly, and is the only PNG path when libpng was
		 * not built in). */
		return decode_other(bytes, len, out_pixels, out_w, out_h);
	}
	if (len >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 &&
	    bytes[2] == 0xFF) {
#if YETTY_HAVE_TURBOJPEG
		if (decode_jpeg(bytes, len, out_pixels, out_w, out_h)) return 1;
#endif
		return decode_other(bytes, len, out_pixels, out_w, out_h);
	}
	return decode_other(bytes, len, out_pixels, out_w, out_h);
}

/* Look up `url` in the document's image cache. Returns the cache slot
 * (existing or freshly allocated) or NULL on alloc failure. The slot
 * may have failed=1 to indicate a previous fetch/decode error — caller
 * should fall through to the placeholder path in that case.
 * Exposed (non-static) so ylexbor-box.c can pre-decode at box-build
 * time to set the image's natural pixel dimensions on the box. */
struct yetty_ylexbor_img_cache_entry *yetty_ylexbor_img_cache_get_or_load(
	struct yetty_ylexbor *r, const char *url)
{
	for (int i = 0; i < r->img_cache_count; i++) {
		if (r->img_cache[i].url &&
		    strcmp(r->img_cache[i].url, url) == 0) {
			return &r->img_cache[i];
		}
	}
	if (r->img_cache_count == r->img_cache_cap) {
		int nc = r->img_cache_cap ? r->img_cache_cap * 2 : 8;
		struct yetty_ylexbor_img_cache_entry *p =
			realloc(r->img_cache, (size_t)nc * sizeof(*p));
		if (!p) return NULL;
		r->img_cache = p;
		r->img_cache_cap = nc;
	}
	struct yetty_ylexbor_img_cache_entry *e =
		&r->img_cache[r->img_cache_count++];
	memset(e, 0, sizeof(*e));
	e->url = strdup(url);
	if (!e->url) {
		r->img_cache_count--;
		return NULL;
	}

	long status = 0;
	size_t blen = 0;
	char *bytes = yetty_ylexbor_http_get(url, &blen, &status);
	if (!bytes || blen == 0 || (status != 0 && status != 200)) {
		free(bytes);
		e->failed = 1;
		return e;
	}

	uint32_t *pixels = NULL;
	int w = 0, h = 0;
	int ok = decode_image((const uint8_t *)bytes, blen, &pixels, &w, &h);
	free(bytes);
	if (!ok) {
		if (getenv("YLEXBOR_DEBUG_PAINT")) {
			fprintf(stderr,
			    "[ylexbor:paint] image decode failed: %s\n", url);
		}
		e->failed = 1;
		return e;
	}
	e->pixels = pixels;
	e->w = w;
	e->h = h;
	return e;
}

/* Read `<img>`'s `src=` and produce an absolute URL by combining with
 * the document base. Returns NULL when the element has no src or the
 * URL can't be resolved. Caller frees with free(). */
static char *img_src_url(struct yetty_ylexbor *r, lxb_dom_element_t *el)
{
	if (!el) return NULL;
	size_t slen = 0;
	const lxb_char_t *src = lxb_dom_element_get_attribute(el,
		(const lxb_char_t *)"src", 3, &slen);
	if (!src || slen == 0) return NULL;
	char *raw = strndup((const char *)src, slen);
	if (!raw) return NULL;
	char *abs = yetty_ylexbor_resolve_url(r, raw);
	free(raw);
	return abs;
}

struct yetty_ycore_void_result yetty_ylexbor_paint(
	struct yetty_ylexbor *r, struct yetty_ypaint_core_buffer *buf)
{
	if (r == NULL || buf == NULL)
		return YETTY_ERR(yetty_ycore_void, "ylexbor_paint: null");

	const int debug = getenv("YLEXBOR_DEBUG_PAINT") != NULL;
	uint32_t z = 0;
	/* Track the maximum X/Y extent of every emitted prim so we can
	 * set the buffer's scene bounds at the end. Without this the
	 * scene rectangle is left at (0,0)–(0,0), and the GPU pipeline
	 * culls every prim whose bounds fall outside that degenerate
	 * rectangle — yielding "no image displayed" even though the
	 * prims and their pixel data are present in the wire bytes. */
	float scene_max_x = 0.0f, scene_max_y = 0.0f;

	if (debug) {
		fprintf(stderr, "[ylexbor:paint] total boxes=%u\n",
			r->boxes.size);
	}
	for (uint32_t i = 0; i < r->boxes.size; i++) {
		struct yetty_ylexbor_box *b = &r->boxes.data[i];
		if (b->w <= 0 || b->h <= 0) {
			if (debug) {
				fprintf(stderr,
				    "[ylexbor:paint] skip  i=%u kind=%d xy=%.0f,%.0f wh=%.0fx%.0f\n",
				    i, b->kind, b->x, b->y, b->w, b->h);
			}
			continue;
		}
		/* Grow scene extents to cover this box. We extend even
		 * for transparent / text boxes since they contribute glyph
		 * geometry; the GPU will simply skip empty regions. */
		float right  = b->x + b->w;
		float bottom = b->y + b->h;
		if (right  > scene_max_x) scene_max_x = right;
		if (bottom > scene_max_y) scene_max_y = bottom;

		switch (b->kind) {
		case YL_BOX_BLOCK: {
			if (debug) {
				fprintf(stderr,
				    "[ylexbor:paint] block i=%u xy=%.0f,%.0f wh=%.0fx%.0f bg=%02x%02x%02x%02x\n",
				    i, b->x, b->y, b->w, b->h,
				    b->bg.r, b->bg.g, b->bg.b, b->bg.a);
			}
			/* Skip transparent backgrounds — most blocks. */
			if (b->bg.a == 0) break;
			struct yetty_ysdf_box box = {
				.center_x = b->x + b->w * 0.5f,
				.center_y = b->y + b->h * 0.5f,
				.half_width = b->w * 0.5f,
				.half_height = b->h * 0.5f,
				.corner_radius = 0,
			};
			(void)yetty_ysdf_add_box(buf, z++,
				pack_rgba(b->bg), 0, 0, &box);
			break;
		}

		case YL_BOX_INLINE_IMAGE: {
			char *url = img_src_url(r, b->element);
			struct yetty_ylexbor_img_cache_entry *cached = NULL;
			if (url) cached = yetty_ylexbor_img_cache_get_or_load(r, url);
			free(url);

			if (!cached || cached->failed || !cached->pixels) {
				/* Fetch/decode failed (or no src) — fall back
				 * to the grey placeholder so at least the
				 * page geometry is preserved. */
				struct yetty_ysdf_box box = {
					.center_x = b->x + b->w * 0.5f,
					.center_y = b->y + b->h * 0.5f,
					.half_width = b->w * 0.5f,
					.half_height = b->h * 0.5f,
					.corner_radius = 0,
				};
				(void)yetty_ysdf_add_box(buf, z++,
					0xc0c0c0ffu, 0, 0, &box);
				if (debug) {
					fprintf(stderr,
					    "[ylexbor:paint] image (placeholder) i=%u xy=%.0f,%.0f wh=%.0fx%.0f\n",
					    i, b->x, b->y, b->w, b->h);
				}
				break;
			}

			/* Build a yimage prim sized to the box's allocated
			 * geometry — the decoded pixels carry the source
			 * dimensions, the bounds carry where to draw. The
			 * GPU does the resampling at sample time. */
			struct yetty_yimage_uniforms u = {
				.bounds_x = b->x,
				.bounds_y = b->y,
				.bounds_w = b->w > 0 ? b->w : (float)cached->w,
				.bounds_h = b->h > 0 ? b->h : (float)cached->h,
				.image_w  = (uint32_t)cached->w,
				.image_h  = (uint32_t)cached->h,
			};
			struct yetty_yimage_buffers bufs = {
				.pixels = cached->pixels,
				.pixels_len = (size_t)cached->w * (size_t)cached->h,
			};
			size_t need =
				yetty_yimage_uniforms_serialized_size(&u, &bufs);
			uint8_t *prim = malloc(need);
			if (!prim) break;
			struct yetty_ycore_size_result ser =
				yetty_yimage_uniforms_serialize(&u, &bufs,
								prim, need);
			if (YETTY_IS_ERR(ser)) {
				free(prim);
				break;
			}
			(void)yetty_ypaint_core_buffer_add_prim(buf, prim, need);
			free(prim);
			z++;
			if (debug) {
				fprintf(stderr,
				    "[ylexbor:paint] image i=%u xy=%.0f,%.0f wh=%.0fx%.0f src=%dx%d\n",
				    i, b->x, b->y, b->w, b->h,
				    cached->w, cached->h);
			}
			break;
		}

		case YL_BOX_INLINE_TEXT: {
			if (debug && b->text_len) {
				int n = b->text_len > 40 ? 40 : (int)b->text_len;
				fprintf(stderr,
				    "[ylexbor:paint] text  i=%u xy=%.0f,%.0f wh=%.0fx%.0f fg=%02x%02x%02x%02x \"%.*s\"\n",
				    i, b->x, b->y, b->w, b->h,
				    b->fg.r, b->fg.g, b->fg.b, b->fg.a,
				    n, b->text);
			}
			if (b->text == NULL || b->text_len == 0) break;
			struct yetty_ycore_buffer txt = {
				.data = (uint8_t *)b->text,
				.capacity = b->text_len,
				.size = b->text_len,
			};
			/* Baseline approximation: top + 0.8 * line height.
			 * Real metric needs FreeType ascent. */
			float baseline_y = b->y + b->font_size * 0.8f;
			(void)yetty_ypaint_core_buffer_add_text(
				buf, b->x, baseline_y, &txt,
				b->font_size, pack_rgba(b->fg),
				z++, /*font_id=*/-1, /*rotation=*/0.0f);
			break;
		}
		}
	}

	/* Publish the scene rectangle. Use viewport_w as a floor for X so
	 * a page with no full-width background still produces a sensible
	 * scene width matching the requested viewport. */
	float min_w = (float)r->viewport_w;
	if (scene_max_x < min_w) scene_max_x = min_w;
	yetty_ypaint_core_buffer_set_scene_bounds(buf,
		0.0f, 0.0f, scene_max_x, scene_max_y);
	if (debug) {
		fprintf(stderr,
		    "[ylexbor:paint] scene bounds = (0,0)-(%.0f,%.0f)\n",
		    scene_max_x, scene_max_y);
	}

	return YETTY_OK_VOID();
}
