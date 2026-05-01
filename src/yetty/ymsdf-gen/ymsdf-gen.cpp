/*
 * ymsdf-gen-cpu.cpp - CPU MSDF generator using msdfgen
 *
 * Taken from old tmp/yetty/src/yetty/msdf-gen/generator.cpp
 * Multi-threaded generation, outputs via ycdb C API.
 * Supports both CDB (non-monospace) and MS-CDB (monospace) formats.
 */

#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/ycdb/ycdb.h>
#include <yetty/ytrace/ytrace.h>

#include <msdfgen.h>
#include <msdfgen-ext.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <filesystem>

/*=============================================================================
 * Glyph header for CDB format (matches old MsdfGlyphData)
 *===========================================================================*/

struct msdf_glyph_header {
	uint32_t codepoint;
	uint16_t width;
	uint16_t height;
	float bearing_x;
	float bearing_y;
	float size_x;
	float size_y;
	float advance;
};

/*=============================================================================
 * Internal glyph result
 *===========================================================================*/

struct glyph_result {
	uint32_t codepoint;
	struct msdf_glyph_header header;
	std::vector<uint8_t> pixels; /* RGBA8 */
	bool success;
};

/*=============================================================================
 * Thread-safe work queue
 *===========================================================================*/

class WorkQueue {
public:
	void push(uint32_t cp) {
		std::lock_guard<std::mutex> lock(_mutex);
		_queue.push(cp);
	}
	bool pop(uint32_t &cp) {
		std::lock_guard<std::mutex> lock(_mutex);
		if (_queue.empty()) { return false;
}
		cp = _queue.front();
		_queue.pop();
		return true;
	}
private:
	std::queue<uint32_t> _queue;
	std::mutex _mutex;
};

/*=============================================================================
 * Single glyph MSDF generation (from old generator.cpp)
 *===========================================================================*/

struct worker_ctx {
	msdfgen::FreetypeHandle *ft;
	msdfgen::FontHandle *font;
	float font_size;
	float pixel_range;
};

static glyph_result generate_glyph(worker_ctx &ctx, uint32_t codepoint)
{
	glyph_result res;
	res.codepoint = codepoint;
	res.success = false;
	std::memset(&res.header, 0, sizeof(res.header));
	res.header.codepoint = codepoint;

	msdfgen::Shape shape;
	double advance;
	if (!msdfgen::loadGlyph(shape, ctx.font, codepoint, &advance)) {
		return res;
}

	msdfgen::FontMetrics metrics;
	msdfgen::getFontMetrics(metrics, ctx.font);
	double em = metrics.emSize > 0 ? metrics.emSize
		    : (metrics.ascenderY - metrics.descenderY);
	double scale = ctx.font_size / em;

	res.header.advance = static_cast<float>(advance * scale);

	if (shape.contours.empty()) {
		res.header.width = 0;
		res.header.height = 0;
		res.success = true;
		return res;
	}

	shape.normalize();
	shape.orientContours();

	msdfgen::Shape::Bounds bounds = shape.getBounds();

	int padding = static_cast<int>(std::ceil(ctx.pixel_range));
	double logical_w = (bounds.r - bounds.l) * scale;
	double logical_h = (bounds.t - bounds.b) * scale;
	int bmp_w = static_cast<int>(std::ceil(logical_w)) + padding * 2;
	int bmp_h = static_cast<int>(std::ceil(logical_h)) + padding * 2;

	if (bmp_w <= 0 || bmp_h <= 0) {
		return res;
}

	res.header.width = static_cast<uint16_t>(bmp_w);
	res.header.height = static_cast<uint16_t>(bmp_h);
	res.header.size_x = static_cast<float>(bmp_w);
	res.header.size_y = static_cast<float>(bmp_h);
	res.header.bearing_x = static_cast<float>(bounds.l * scale - padding);
	res.header.bearing_y = static_cast<float>(bounds.t * scale + padding);

	msdfgen::edgeColoringSimple(shape, 3.0);

	msdfgen::Bitmap<float, 3> msdf(bmp_w, bmp_h);
	msdfgen::Vector2 translate(
		padding / scale - bounds.l,
		padding / scale - bounds.b);
	msdfgen::generateMSDF(msdf, shape, ctx.pixel_range, scale, translate);

	/* Convert to RGBA8 with Y-flip */
	res.pixels.resize(bmp_w * bmp_h * 4);
	for (int y = 0; y < bmp_h; y++) {
		int src_y = bmp_h - 1 - y;
		for (int x = 0; x < bmp_w; x++) {
			size_t idx = (y * bmp_w + x) * 4;
			res.pixels[idx + 0] = static_cast<uint8_t>(
				std::clamp(msdf(x, src_y)[0] * 255.0f, 0.0f, 255.0f));
			res.pixels[idx + 1] = static_cast<uint8_t>(
				std::clamp(msdf(x, src_y)[1] * 255.0f, 0.0f, 255.0f));
			res.pixels[idx + 2] = static_cast<uint8_t>(
				std::clamp(msdf(x, src_y)[2] * 255.0f, 0.0f, 255.0f));
			res.pixels[idx + 3] = 255;
		}
	}

	res.success = true;
	return res;
}

/*=============================================================================
 * Worker thread (from old generator.cpp)
 *===========================================================================*/

static void worker_thread(WorkQueue &queue,
			  std::vector<glyph_result> &results,
			  std::mutex &results_mutex,
			  std::atomic<size_t> &completed,
			  const std::string &font_path,
			  float font_size, float pixel_range)
{
	auto ft = msdfgen::initializeFreetype();
	if (!ft) { return;
}

	auto font = msdfgen::loadFont(ft, font_path.c_str());
	if (!font) {
		msdfgen::deinitializeFreetype(ft);
		return;
	}

	worker_ctx ctx{ft, font, font_size, pixel_range};

	uint32_t cp;
	while (queue.pop(cp)) {
		auto res = generate_glyph(ctx, cp);
		{
			std::lock_guard<std::mutex> lock(results_mutex);
			results.push_back(std::move(res));
		}
		++completed;
	}

	msdfgen::destroyFont(font);
	msdfgen::deinitializeFreetype(ft);
}

/*=============================================================================
 * Charset (from old generator.cpp)
 *===========================================================================*/

static std::vector<uint32_t> get_default_charset(bool nerd_fonts, bool cjk)
{
	std::vector<uint32_t> cs;

	auto add = [&](uint32_t lo, uint32_t hi) {
		for (uint32_t c = lo; c <= hi; c++) { cs.push_back(c);
}
	};

	add(0x20, 0x7E);       /* Basic Latin */
	add(0xA0, 0xFF);       /* Latin-1 Supplement */
	add(0x100, 0x17F);     /* Latin Extended-A */
	add(0x180, 0x24F);     /* Latin Extended-B */
	add(0x370, 0x3FF);     /* Greek */
	add(0x400, 0x4FF);     /* Cyrillic */
	add(0x2000, 0x206F);   /* General Punctuation */
	add(0x20A0, 0x20CF);   /* Currency */
	add(0x2190, 0x21FF);   /* Arrows */
	add(0x2200, 0x22FF);   /* Math Operators */
	add(0x2500, 0x257F);   /* Box Drawing */
	add(0x2580, 0x259F);   /* Block Elements */
	add(0x25A0, 0x25FF);   /* Geometric Shapes */

	if (nerd_fonts) {
		add(0xE0A0, 0xE0D7);   /* Powerline */
		add(0xE5FA, 0xE6AC);   /* Seti-UI */
		add(0xE700, 0xE7C5);   /* Devicons */
		add(0xE200, 0xE2A9);   /* Font Awesome Ext */
		add(0xE300, 0xE3E3);   /* Weather */
		add(0xF400, 0xF532);   /* Octicons */
		add(0xEA60, 0xEBEB);   /* Codicons */
		add(0xF0001, 0xF1AF0); /* Material Design */
	}

	if (cjk) {
		add(0x3000, 0x303F);
		add(0x3040, 0x309F);
		add(0x30A0, 0x30FF);
		add(0x31F0, 0x31FF);
		add(0x4E00, 0x9FFF);
		add(0xAC00, 0xD7AF);
		add(0x1100, 0x11FF);
		add(0x3130, 0x318F);
		add(0xFF00, 0xFFEF);
		add(0x3100, 0x312F);
	}

	return cs;
}

static std::vector<uint32_t> get_font_charset(const std::string &font_path)
{
	std::vector<uint32_t> cs;
	FT_Library lib;
	if (FT_Init_FreeType(&lib)) { return cs;
}

	FT_Face face;
	if (FT_New_Face(lib, font_path.c_str(), 0, &face)) {
		FT_Done_FreeType(lib);
		return cs;
	}

	FT_ULong charcode;
	FT_UInt idx;
	charcode = FT_Get_First_Char(face, &idx);
	while (idx != 0) {
		cs.push_back(static_cast<uint32_t>(charcode));
		charcode = FT_Get_Next_Char(face, charcode, &idx);
	}

	FT_Done_Face(face);
	FT_Done_FreeType(lib);
	return cs;
}

/*=============================================================================
 * Write CDB (non-monospace format)
 *===========================================================================*/

static struct yetty_ycore_void_result
write_cdb(const char *path, const std::vector<glyph_result> &results)
{
	struct yetty_ycdb_writer_result wr = yetty_ycdb_writer_create(path);
	if (YETTY_IS_ERR(wr)) {
		return yetty_ycore_err( wr.error.msg);
}

	for (const auto &r : results) {
		if (!r.success) { continue;
}

		size_t val_size = sizeof(struct msdf_glyph_header) + r.pixels.size();
		std::vector<char> val(val_size);
		std::memcpy(val.data(), &r.header, sizeof(r.header));
		if (!r.pixels.empty()) {
			std::memcpy(val.data() + sizeof(r.header),
				    r.pixels.data(), r.pixels.size());
}

		uint32_t key = r.codepoint;
		struct yetty_ycore_void_result add_res = yetty_ycdb_writer_add(
			wr.value, &key, sizeof(key), val.data(), val_size);
		if (YETTY_IS_ERR(add_res)) {
			yetty_ycdb_writer_finish(wr.value);
			return add_res;
		}
	}

	return yetty_ycdb_writer_finish(wr.value);
}

/*=============================================================================
 * Public API
 *===========================================================================*/

extern "C" struct yetty_ycore_void_result
yetty_ymsdf_gen_config_cpu_generate(const struct yetty_ymsdf_gen_config *config)
{
	if (!config || !config->ttf_path || !config->output_dir) {
		return yetty_ycore_err( "invalid config");
}

	float font_size = config->font_size > 0 ? config->font_size : 32.0f;
	float pixel_range = config->pixel_range > 0 ? config->pixel_range : 4.0f;
	int thread_count = config->thread_count;
	if (thread_count <= 0) {
		thread_count = static_cast<int>(std::thread::hardware_concurrency());
}
	if (thread_count < 1) { thread_count = 1;
}

	/* Build charset */
	std::vector<uint32_t> charset;
	if (config->all_glyphs) {
		charset = get_font_charset(config->ttf_path);
	} else {
		charset = get_default_charset(config->include_nerd_fonts,
					      config->include_cjk);
}

	if (charset.empty()) {
		return yetty_ycore_err( "empty charset");
}

	/* Work queue */
	WorkQueue queue;
	for (uint32_t cp : charset) {
		queue.push(cp);
}

	std::vector<glyph_result> results;
	std::mutex results_mutex;
	std::atomic<size_t> completed{0};

	/* Launch workers */
	std::vector<std::thread> workers;
	for (int i = 0; i < thread_count; i++) {
		workers.emplace_back(worker_thread,
			std::ref(queue), std::ref(results),
			std::ref(results_mutex), std::ref(completed),
			std::string(config->ttf_path),
			font_size, pixel_range);
	}

	for (auto &t : workers) {
		t.join();
}

	/* Extract font name for output file */
	std::filesystem::path fp(config->ttf_path);
	std::string font_name = fp.stem().string();
	std::string output_path = std::string(config->output_dir) + "/" + font_name + ".cdb";

	return write_cdb(output_path.c_str(), results);
}
