/*
 * msdf-render-atlas — interactive atlas viewer for yetty MSDF .cdb files.
 *
 * Direct C port of the original C++ tool. The only behavioural change
 * is the glyph source: the C++ version drove a TTF through msdf::Context
 * to build an in-memory RGBA32Float atlas; this version reads any yetty
 * .cdb (RGBA8 per glyph), expands those bytes to floats and uploads them
 * to the same RGBA32Float storage texture. The render path — pipeline,
 * bind group, shader, sampler, input handling — is unchanged so the
 * visual output matches the original.
 *
 * Controls:
 *   mouse wheel        scroll through glyphs
 *   ctrl + wheel       zoom (centred on cursor)
 *   left drag          pan
 *   r                  reset view
 *   esc                quit
 */

#include <yetty/ymsdf-gen/ymsdf-gen.h>
#include <yetty/ywebgpu/limits.h>
#include <yetty/ywebgpu/request.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * CDB walker — same direct format walk as tools/cdb-viewer. Decodes every
 * glyph (codepoint, header, RGBA8 pixels) and shelf-packs them into a single
 * RGBA32Float atlas the renderer below can sample. The 8-bit→float expansion
 * happens here so the texture format and bindings stay identical to the
 * original C++ tool.
 *===========================================================================*/

#define CDB_HEADER_SIZE 2048
#define ATLAS_WIDTH     1024

struct atlas_glyph {
	uint32_t codepoint;
	uint16_t w, h;
	uint16_t atlas_x, atlas_y;
};

struct atlas {
	float *pixels;             /* RGBA32Float, atlas_w * atlas_h * 4 */
	int width, height;
	struct atlas_glyph *glyphs;
	int glyph_count;
	int glyph_capacity;
};

static uint32_t rd_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0]
	     | ((uint32_t)p[1] << 8)
	     | ((uint32_t)p[2] << 16)
	     | ((uint32_t)p[3] << 24);
}

static int atlas_grow_glyphs(struct atlas *a)
{
	int cap = a->glyph_capacity ? a->glyph_capacity * 2 : 1024;
	struct atlas_glyph *nb = realloc(a->glyphs, cap * sizeof(*nb));
	if (!nb) return -1;
	a->glyphs = nb;
	a->glyph_capacity = cap;
	return 0;
}

static int atlas_grow_pixels(struct atlas *a, int new_h)
{
	if (new_h <= a->height) return 0;
	size_t old_floats = (size_t)a->width * a->height * 4;
	size_t new_floats = (size_t)a->width * new_h * 4;
	float *nb = realloc(a->pixels, new_floats * sizeof(float));
	if (!nb) return -1;
	memset(nb + old_floats, 0, (new_floats - old_floats) * sizeof(float));
	a->pixels = nb;
	a->height = new_h;
	return 0;
}

static void atlas_blit_rgba8(struct atlas *a, int dst_x, int dst_y,
			     const uint8_t *src, int w, int h)
{
	for (int y = 0; y < h; y++) {
		float *dst = a->pixels + ((size_t)(dst_y + y) * a->width + dst_x) * 4;
		const uint8_t *row = src + (size_t)y * w * 4;
		for (int x = 0; x < w; x++) {
			dst[x * 4 + 0] = row[x * 4 + 0] / 255.0f;
			dst[x * 4 + 1] = row[x * 4 + 1] / 255.0f;
			dst[x * 4 + 2] = row[x * 4 + 2] / 255.0f;
			dst[x * 4 + 3] = row[x * 4 + 3] / 255.0f;
		}
	}
}

static int atlas_load_cdb(struct atlas *a, const char *path)
{
	memset(a, 0, sizeof(*a));
	a->width = ATLAS_WIDTH;

	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return -1;
	}
	uint8_t header[CDB_HEADER_SIZE];
	if (fread(header, 1, sizeof(header), f) != sizeof(header)) {
		fprintf(stderr, "%s: short CDB header\n", path);
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
		fprintf(stderr, "%s: invalid CDB\n", path);
		fclose(f);
		return -1;
	}

	int cursor_x = 1, cursor_y = 1, row_h = 0;
	uint8_t *vbuf = NULL;
	size_t vcap = 0;
	uint32_t pos = CDB_HEADER_SIZE;
	int rc = 0;
	while (pos < data_end) {
		uint8_t lens[8];
		if (fread(lens, 1, sizeof(lens), f) != sizeof(lens)) { rc = -1; break; }
		uint32_t klen = rd_u32_le(lens);
		uint32_t vlen = rd_u32_le(lens + 4);
		if ((uint64_t)pos + 8 + klen + vlen > data_end) { rc = -1; break; }

		uint32_t cp = 0;
		if (klen == 4) {
			uint8_t kb[4];
			if (fread(kb, 1, 4, f) != 4) { rc = -1; break; }
			cp = rd_u32_le(kb);
		} else if (fseek(f, klen, SEEK_CUR) != 0) {
			rc = -1; break;
		}
		if (vlen > vcap) {
			uint8_t *nb = realloc(vbuf, vlen);
			if (!nb) { rc = -1; break; }
			vbuf = nb;
			vcap = vlen;
		}
		if (fread(vbuf, 1, vlen, f) != vlen) { rc = -1; break; }
		pos += 8 + klen + vlen;

		if (klen != 4 || vlen < sizeof(struct yetty_ymsdf_gen_glyph_header))
			continue;
		struct yetty_ymsdf_gen_glyph_header h;
		memcpy(&h, vbuf, sizeof(h));
		size_t pix_bytes = vlen - sizeof(h);
		if (h.width == 0 || h.height == 0) continue;
		if (pix_bytes != (size_t)h.width * h.height * 4) continue;

		if (cursor_x + (int)h.width + 1 > a->width) {
			cursor_x = 1;
			cursor_y += row_h + 1;
			row_h = 0;
		}
		int needed_h = cursor_y + (int)h.height + 1;
		if (needed_h > a->height) {
			int target = a->height ? a->height * 2 : 256;
			if (target < needed_h + 64) target = needed_h + 64;
			if (atlas_grow_pixels(a, target) < 0) { rc = -1; break; }
		}
		atlas_blit_rgba8(a, cursor_x, cursor_y, vbuf + sizeof(h), h.width, h.height);

		if (a->glyph_count == a->glyph_capacity) {
			if (atlas_grow_glyphs(a) < 0) { rc = -1; break; }
		}
		struct atlas_glyph *g = &a->glyphs[a->glyph_count++];
		g->codepoint = cp;
		g->w = h.width;
		g->h = h.height;
		g->atlas_x = (uint16_t)cursor_x;
		g->atlas_y = (uint16_t)cursor_y;

		cursor_x += (int)h.width + 1;
		if ((int)h.height > row_h) row_h = (int)h.height;
	}
	free(vbuf);
	fclose(f);
	if (rc == 0) {
		int used_h = cursor_y + row_h + 1;
		if (used_h < 8) used_h = 8;
		used_h = (used_h + 7) & ~7;
		if (used_h > a->height) {
			if (atlas_grow_pixels(a, used_h) < 0) rc = -1;
		}
	}
	return rc;
}

static void atlas_free(struct atlas *a)
{
	free(a->pixels);
	free(a->glyphs);
	memset(a, 0, sizeof(*a));
}

/*=============================================================================
 * Renderer — direct C port of the original C++ main.cpp render path.
 *===========================================================================*/

static int windowWidth = 1280;
static int windowHeight = 720;

static WGPUInstance instance = NULL;
static WGPUSurface surface = NULL;
static WGPUAdapter adapter = NULL;
static WGPUDevice device = NULL;
static WGPUQueue queue = NULL;
static WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;

static WGPUTexture atlasTexture = NULL;
static WGPUTextureView atlasView = NULL;
static WGPURenderPipeline pipeline = NULL;
static WGPUBindGroup bindGroup = NULL;
static WGPUSampler sampler = NULL;
static WGPUBuffer uniformBuffer = NULL;
static WGPUBuffer glyphBuffer = NULL;

static struct atlas g_atlas;

/* Pan and zoom state (world coordinates are in cell units) */
static float panX = 0.0f;
static float panY = 0.0f;
static float zoom = 1.0f;
static float scrollY = 0.0f;
static int dragging = 0;
static double lastMouseX = 0, lastMouseY = 0;

static int gridCols = 80;

/* Per-channel visibility toggles. When a channel is OFF, the shader
 * masks the matching output component to 0, so you can see exactly
 * which channels contribute to a given pixel — handy for tracking down
 * MSDF artefacts where the median3 hides whose sign is wrong. CLI flags
 * --red/--green/--blue/--alpha override the default (everything on)
 * and r/g/b/a keys toggle them at runtime. */
static int chR = 1, chG = 1, chB = 1, chA = 1;

/* Display mode (cycled with the c key):
 *   0 = median3 of the MSDF (default — what the font shader actually uses)
 *   1 = raw RGB of the atlas (so you can see which channels disagree)
 *   2 = R only / 3 = G only / 4 = B only (one channel as greyscale)
 * Channel masks apply on top — e.g. raw RGB with G off shows R+B only. */
static int displayMode = 0;
static const char *DISPLAY_MODE_NAMES[5] = {
	"median3", "raw RGB", "R only", "G only", "B only"
};

/* Set by the SIGINT handler so Ctrl+C from the terminal exits cleanly
 * (rather than killing the process mid-frame and skipping WGPU cleanup). */
static volatile sig_atomic_t g_should_exit = 0;
static void on_sigint(int sig)
{
	(void)sig;
	g_should_exit = 1;
}

/* Shader — verbatim from the original tool. Greyscale = median3 of MSDF. */
static const char *shaderCode =
"struct Uniforms {\n"
"    panX: f32, panY: f32, zoom: f32, aspect: f32,\n"
"    atlasW: f32, atlasH: f32, scrollY: f32, cols: f32,\n"
"    totalGlyphs: f32, chR: f32, chG: f32, chB: f32,\n"
"    chA: f32, displayMode: f32, _pad2: f32, _pad3: f32,\n"
"};\n"
"struct GlyphRect { x: f32, y: f32, w: f32, h: f32, };\n"
"@group(0) @binding(0) var<uniform> uniforms: Uniforms;\n"
"@group(0) @binding(1) var atlasSampler: sampler;\n"
"@group(0) @binding(2) var atlasTexture: texture_2d<f32>;\n"
"@group(0) @binding(3) var<storage, read> glyphs: array<GlyphRect>;\n"
"struct VertexOutput {\n"
"    @builtin(position) position: vec4<f32>,\n"
"    @location(0) uv: vec2<f32>,\n"
"};\n"
"@vertex\n"
"fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {\n"
"    var pos = array<vec2<f32>, 3>(\n"
"        vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));\n"
"    var uv = array<vec2<f32>, 3>(\n"
"        vec2<f32>(0.0, 1.0), vec2<f32>(2.0, 1.0), vec2<f32>(0.0, -1.0));\n"
"    var out: VertexOutput;\n"
"    out.position = vec4<f32>(pos[idx], 0.0, 1.0);\n"
"    out.uv = uv[idx];\n"
"    return out;\n"
"}\n"
"@fragment\n"
"fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {\n"
"    let bg_color = vec4<f32>(0.15, 0.15, 0.15, 1.0);\n"
"    let grid_color = vec4<f32>(0.25, 0.25, 0.25, 1.0);\n"
"    let empty_color = vec4<f32>(0.1, 0.1, 0.12, 1.0);\n"
"    let screen = vec2<f32>(\n"
"        (in.uv.x - 0.5) * 2.0 * uniforms.aspect,\n"
"        (in.uv.y - 0.5) * 2.0);\n"
"    let world = vec2<f32>(\n"
"        screen.x / uniforms.zoom + uniforms.panX,\n"
"        screen.y / uniforms.zoom + uniforms.panY + uniforms.scrollY);\n"
"    let col = floor(world.x);\n"
"    let row = floor(world.y);\n"
"    let rawIdx = u32(max(row, 0.0)) * u32(uniforms.cols) + u32(max(col, 0.0));\n"
"    let safeIdx = min(rawIdx, u32(max(uniforms.totalGlyphs - 1.0, 0.0)));\n"
"    let localUV = fract(world);\n"
"    let glyph = glyphs[safeIdx];\n"
"    let margin = 0.02;\n"
"    let innerUV = (localUV - margin) / (1.0 - 2.0 * margin);\n"
"    let glyphW = max(glyph.w, 1.0);\n"
"    let glyphH = max(glyph.h, 1.0);\n"
"    let atlasUV = vec2<f32>(\n"
"        (glyph.x + clamp(innerUV.x, 0.0, 1.0) * glyphW) / uniforms.atlasW,\n"
"        (glyph.y + clamp(innerUV.y, 0.0, 1.0) * glyphH) / uniforms.atlasH);\n"
"    let msdf = textureSample(atlasTexture, atlasSampler, atlasUV);\n"
"    let median = max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b));\n"
"    // displayMode picks the base colour; channel masks then gate each\n"
"    // RGB output bit so you can see exactly which channels contribute.\n"
"    var base = vec4<f32>(median, median, median, 1.0);\n"
"    let mode = u32(uniforms.displayMode + 0.5);\n"
"    if mode == 1u { base = vec4<f32>(msdf.rgb, 1.0); }                          // raw RGB\n"
"    else if mode == 2u { base = vec4<f32>(msdf.r, msdf.r, msdf.r, 1.0); }       // R only\n"
"    else if mode == 3u { base = vec4<f32>(msdf.g, msdf.g, msdf.g, 1.0); }       // G only\n"
"    else if mode == 4u { base = vec4<f32>(msdf.b, msdf.b, msdf.b, 1.0); }       // B only\n"
"    let glyph_color = vec4<f32>(\n"
"        base.r * uniforms.chR,\n"
"        base.g * uniforms.chG,\n"
"        base.b * uniforms.chB,\n"
"        uniforms.chA);\n"
"    let edgeX = min(localUV.x, 1.0 - localUV.x);\n"
"    let edgeY = min(localUV.y, 1.0 - localUV.y);\n"
"    let edge = min(edgeX, edgeY);\n"
"    let is_grid_line = edge < margin;\n"
"    let out_of_bounds = col < 0.0 || col >= uniforms.cols || row < 0.0 || rawIdx >= u32(uniforms.totalGlyphs);\n"
"    let is_empty = glyph.w <= 0.0 || glyph.h <= 0.0;\n"
"    var color = glyph_color;\n"
"    color = select(color, empty_color, is_empty);\n"
"    color = select(color, grid_color, is_grid_line && !out_of_bounds);\n"
"    color = select(color, bg_color, out_of_bounds);\n"
"    return color;\n"
"}\n";

static int adapterReady = 0;
static int deviceReady = 0;

static void onAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter result,
			   WGPUStringView message, void *u1, void *u2)
{
	(void)message; (void)u1; (void)u2;
	if (status == WGPURequestAdapterStatus_Success) adapter = result;
	adapterReady = 1;
}

static void onDeviceReady(WGPURequestDeviceStatus status, WGPUDevice result,
			  WGPUStringView message, void *u1, void *u2)
{
	(void)message; (void)u1; (void)u2;
	if (status == WGPURequestDeviceStatus_Success) device = result;
	deviceReady = 1;
}

static void onUncapturedError(WGPUDevice const *d, WGPUErrorType type,
			      WGPUStringView message, void *u1, void *u2)
{
	(void)d; (void)u1; (void)u2;
	const char *typeStr = "Unknown";
	switch (type) {
	case WGPUErrorType_Validation:  typeStr = "Validation";  break;
	case WGPUErrorType_OutOfMemory: typeStr = "OutOfMemory"; break;
	case WGPUErrorType_Internal:    typeStr = "Internal";    break;
	default: break;
	}
	fprintf(stderr, "[WebGPU ERROR] %s: %.*s\n", typeStr,
		message.data ? (int)message.length : 0,
		message.data ? message.data : "");
}

static int initWebGPU(GLFWwindow *window)
{
	fprintf(stderr, "[WebGPU] Initializing...\n");

	WGPUInstanceDescriptor instanceDesc = {0};
	instance = wgpuCreateInstance(&instanceDesc);

	surface = glfwCreateWindowWGPUSurface(instance, window);

	WGPURequestAdapterOptions adapterOpts = {0};
	adapterOpts.compatibleSurface = surface;
	WGPURequestAdapterCallbackInfo adapterCallback = {0};
	adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
	adapterCallback.callback = onAdapterReady;
	wgpuInstanceRequestAdapter(instance, &adapterOpts, adapterCallback);
	while (!adapterReady) {
		wgpuInstanceProcessEvents(instance);
		glfwPollEvents();
	}
	if (!adapter) {
		fprintf(stderr, "[WebGPU] adapter request failed\n");
		return 0;
	}

	WGPUUncapturedErrorCallbackInfo errorInfo = {0};
	errorInfo.callback = onUncapturedError;

	/* Use the same helper as the rest of yetty — it clamps each limit
	 * to what the adapter actually supports, so requesting "as much as
	 * possible" doesn't fail outright on more constrained backends. */
	WGPULimits requiredLimits;
	yetty_ywebgpu_fill_default_limits(adapter, NULL, &requiredLimits);

	WGPUDeviceDescriptor deviceDesc = {0};
	deviceDesc.requiredLimits = &requiredLimits;
	deviceDesc.uncapturedErrorCallbackInfo = errorInfo;

	WGPURequestDeviceCallbackInfo deviceCallback = {0};
	deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
	deviceCallback.callback = onDeviceReady;
	wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallback);
	while (!deviceReady) {
		wgpuInstanceProcessEvents(instance);
		glfwPollEvents();
	}
	if (!device) {
		fprintf(stderr, "[WebGPU] device request failed\n");
		return 0;
	}

	queue = wgpuDeviceGetQueue(device);

	WGPUSurfaceCapabilities caps = {0};
	wgpuSurfaceGetCapabilities(surface, adapter, &caps);
	surfaceFormat = caps.formats[0];

	WGPUSurfaceConfiguration surfaceConfig = {0};
	surfaceConfig.device = device;
	surfaceConfig.format = surfaceFormat;
	surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
	surfaceConfig.width = windowWidth;
	surfaceConfig.height = windowHeight;
	surfaceConfig.presentMode = WGPUPresentMode_Fifo;
	surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
	wgpuSurfaceConfigure(surface, &surfaceConfig);

	fprintf(stderr, "[WebGPU] Initialized\n");
	return 1;
}

/* Allocate and upload the RGBA32Float atlas texture from g_atlas.pixels.
 * Replaces what the C++ tool got from msdf::Atlas::getTextureView(). */
static int initAtlasTexture(void)
{
	WGPUTextureDescriptor td = {0};
	td.size.width = g_atlas.width;
	td.size.height = g_atlas.height;
	td.size.depthOrArrayLayers = 1;
	td.format = WGPUTextureFormat_RGBA32Float;
	td.dimension = WGPUTextureDimension_2D;
	td.mipLevelCount = 1;
	td.sampleCount = 1;
	td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
	atlasTexture = wgpuDeviceCreateTexture(device, &td);
	if (!atlasTexture) return 0;
	atlasView = wgpuTextureCreateView(atlasTexture, NULL);

	WGPUTexelCopyTextureInfo dst = {0};
	dst.texture = atlasTexture;
	WGPUTexelCopyBufferLayout layout = {0};
	layout.bytesPerRow = (uint32_t)g_atlas.width * 16u; /* RGBA32Float */
	layout.rowsPerImage = (uint32_t)g_atlas.height;
	WGPUExtent3D ext = {(uint32_t)g_atlas.width, (uint32_t)g_atlas.height, 1};
	wgpuQueueWriteTexture(queue, &dst, g_atlas.pixels,
			      (size_t)g_atlas.width * g_atlas.height * 4 * sizeof(float),
			      &layout, &ext);
	return 1;
}

static int initRendering(void)
{
	if (!initAtlasTexture()) return 0;

	int totalGlyphCount = g_atlas.glyph_count;
	fprintf(stderr, "[Render] Atlas size: %dx%d, glyphs: %d\n",
		g_atlas.width, g_atlas.height, totalGlyphCount);

	int n = totalGlyphCount > 0 ? totalGlyphCount : 1;
	size_t glyphBufSize = (size_t)n * 4 * sizeof(float);
	float *glyphRects = calloc((size_t)n * 4, sizeof(float));
	for (int i = 0; i < totalGlyphCount; i++) {
		glyphRects[i * 4 + 0] = (float)g_atlas.glyphs[i].atlas_x;
		glyphRects[i * 4 + 1] = (float)g_atlas.glyphs[i].atlas_y;
		glyphRects[i * 4 + 2] = (float)g_atlas.glyphs[i].w;
		glyphRects[i * 4 + 3] = (float)g_atlas.glyphs[i].h;
	}
	WGPUBufferDescriptor glyphBufDesc = {0};
	glyphBufDesc.size = glyphBufSize;
	glyphBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
	glyphBuffer = wgpuDeviceCreateBuffer(device, &glyphBufDesc);
	wgpuQueueWriteBuffer(queue, glyphBuffer, 0, glyphRects, glyphBufSize);
	free(glyphRects);

	WGPUShaderSourceWGSL wgslDesc = {0};
	wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
	wgslDesc.code.data = shaderCode;
	wgslDesc.code.length = WGPU_STRLEN;
	WGPUShaderModuleDescriptor shaderDesc = {0};
	shaderDesc.nextInChain = &wgslDesc.chain;
	WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

	WGPUSamplerDescriptor samplerDesc = {0};
	samplerDesc.magFilter = WGPUFilterMode_Nearest;
	samplerDesc.minFilter = WGPUFilterMode_Nearest;
	samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
	samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
	samplerDesc.maxAnisotropy = 1;
	sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

	/* 16 floats = 64 bytes — uniforms grew to carry the 4 channel-mask
	 * floats plus the original layout. */
	WGPUBufferDescriptor uniformDesc = {0};
	uniformDesc.size = 64;
	uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

	WGPUBindGroupLayoutEntry layoutEntries[4] = {0};
	layoutEntries[0].binding = 0;
	layoutEntries[0].visibility = WGPUShaderStage_Fragment;
	layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
	layoutEntries[1].binding = 1;
	layoutEntries[1].visibility = WGPUShaderStage_Fragment;
	layoutEntries[1].sampler.type = WGPUSamplerBindingType_NonFiltering;
	layoutEntries[2].binding = 2;
	layoutEntries[2].visibility = WGPUShaderStage_Fragment;
	layoutEntries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
	layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
	layoutEntries[3].binding = 3;
	layoutEntries[3].visibility = WGPUShaderStage_Fragment;
	layoutEntries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

	WGPUBindGroupLayoutDescriptor layoutDesc = {0};
	layoutDesc.entryCount = 4;
	layoutDesc.entries = layoutEntries;
	WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

	WGPUBindGroupEntry bindEntries[4] = {0};
	bindEntries[0].binding = 0;
	bindEntries[0].buffer = uniformBuffer;
	bindEntries[0].size = 64;
	bindEntries[1].binding = 1;
	bindEntries[1].sampler = sampler;
	bindEntries[2].binding = 2;
	bindEntries[2].textureView = atlasView;
	bindEntries[3].binding = 3;
	bindEntries[3].buffer = glyphBuffer;
	bindEntries[3].size = glyphBufSize;
	WGPUBindGroupDescriptor bindGroupDesc = {0};
	bindGroupDesc.layout = bindGroupLayout;
	bindGroupDesc.entryCount = 4;
	bindGroupDesc.entries = bindEntries;
	bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

	WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {0};
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout;
	WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

	WGPUColorTargetState colorTarget = {0};
	colorTarget.format = surfaceFormat;
	colorTarget.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragmentState = {0};
	fragmentState.module = shaderModule;
	fragmentState.entryPoint.data = "fs_main";
	fragmentState.entryPoint.length = WGPU_STRLEN;
	fragmentState.targetCount = 1;
	fragmentState.targets = &colorTarget;

	WGPURenderPipelineDescriptor pipelineDesc = {0};
	pipelineDesc.layout = pipelineLayout;
	pipelineDesc.vertex.module = shaderModule;
	pipelineDesc.vertex.entryPoint.data = "vs_main";
	pipelineDesc.vertex.entryPoint.length = WGPU_STRLEN;
	pipelineDesc.fragment = &fragmentState;
	pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	pipelineDesc.multisample.count = 1;
	pipelineDesc.multisample.mask = ~0u;

	pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

	wgpuShaderModuleRelease(shaderModule);
	wgpuBindGroupLayoutRelease(bindGroupLayout);
	wgpuPipelineLayoutRelease(pipelineLayout);

	fprintf(stderr, "[Render] Pipeline created\n");
	return pipeline != NULL;
}

static void updateUniforms(void)
{
	float windowAspect = (float)windowWidth / (float)windowHeight;
	struct {
		float panX, panY;
		float zoom;
		float aspect;
		float atlasW, atlasH;
		float scrollY;
		float cols;
		float totalGlyphs;
		float chR, chG, chB, chA;
		float displayMode;
		float _pad2, _pad3;
	} u = {
		panX, panY, zoom, windowAspect,
		(float)g_atlas.width, (float)g_atlas.height,
		scrollY,
		(float)gridCols,
		(float)g_atlas.glyph_count,
		(float)chR, (float)chG, (float)chB, (float)chA,
		(float)displayMode,
		0, 0
	};
	wgpuQueueWriteBuffer(queue, uniformBuffer, 0, &u, 64);
}

static void render(void)
{
	updateUniforms();

	WGPUSurfaceTexture surfaceTexture;
	wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
	if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
	    surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
		return;

	WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, NULL);

	WGPUCommandEncoderDescriptor encoderDesc = {0};
	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

	WGPURenderPassColorAttachment colorAttachment = {0};
	colorAttachment.view = targetView;
	colorAttachment.loadOp = WGPULoadOp_Clear;
	colorAttachment.storeOp = WGPUStoreOp_Store;
	colorAttachment.clearValue.r = 0.1; colorAttachment.clearValue.g = 0.1;
	colorAttachment.clearValue.b = 0.1; colorAttachment.clearValue.a = 1.0;
	colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

	WGPURenderPassDescriptor passDesc = {0};
	passDesc.colorAttachmentCount = 1;
	passDesc.colorAttachments = &colorAttachment;

	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
	wgpuRenderPassEncoderSetPipeline(pass, pipeline);
	wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, NULL);
	wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
	wgpuRenderPassEncoderEnd(pass);

	WGPUCommandBufferDescriptor cmdBufDesc = {0};
	WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufDesc);
	wgpuQueueSubmit(queue, 1, &cmdBuffer);

	wgpuCommandBufferRelease(cmdBuffer);
	wgpuRenderPassEncoderRelease(pass);
	wgpuCommandEncoderRelease(encoder);
	wgpuTextureViewRelease(targetView);

	wgpuSurfacePresent(surface);
}

static void cleanup(void)
{
	if (glyphBuffer) wgpuBufferRelease(glyphBuffer);
	if (uniformBuffer) wgpuBufferRelease(uniformBuffer);
	if (bindGroup) wgpuBindGroupRelease(bindGroup);
	if (pipeline) wgpuRenderPipelineRelease(pipeline);
	if (sampler) wgpuSamplerRelease(sampler);
	if (atlasView) wgpuTextureViewRelease(atlasView);
	if (atlasTexture) wgpuTextureRelease(atlasTexture);
	atlas_free(&g_atlas);
}

static void resetView(void)
{
	float windowAspect = (float)windowWidth / (float)windowHeight;
	zoom = 2.0f * windowAspect / (float)gridCols;
	panX = (float)gridCols / 2.0f;
	panY = 1.0f / zoom;
	scrollY = 0.0f;
}

static void framebufferSizeCallback(GLFWwindow *window, int width, int height)
{
	(void)window;
	if (width == 0 || height == 0) return;

	windowWidth = width;
	windowHeight = height;

	WGPUSurfaceConfiguration surfaceConfig = {0};
	surfaceConfig.device = device;
	surfaceConfig.format = surfaceFormat;
	surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
	surfaceConfig.width = windowWidth;
	surfaceConfig.height = windowHeight;
	surfaceConfig.presentMode = WGPUPresentMode_Fifo;
	surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;
	wgpuSurfaceConfigure(surface, &surfaceConfig);
}

static void scrollCallback(GLFWwindow *window, double xoffset, double yoffset)
{
	(void)xoffset;
	int ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
			   glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
	if (ctrlPressed) {
		double mouseX, mouseY;
		glfwGetCursorPos(window, &mouseX, &mouseY);
		float windowAspect = (float)windowWidth / (float)windowHeight;
		float screenX = ((float)mouseX / windowWidth - 0.5f) * 2.0f * windowAspect;
		float screenY = ((float)mouseY / windowHeight - 0.5f) * 2.0f;
		float worldX = screenX / zoom + panX;
		float worldY = screenY / zoom + panY;
		float zoomFactor = (yoffset > 0) ? 0.8f : 1.25f;
		float newZoom = zoom * zoomFactor;
		if (newZoom < 0.005f) newZoom = 0.005f;
		if (newZoom > 50.0f) newZoom = 50.0f;
		panX = worldX - screenX / newZoom;
		panY = worldY - screenY / newZoom;
		zoom = newZoom;
	} else {
		scrollY -= (float)yoffset * 3.0f;
		if (scrollY < 0.0f) scrollY = 0.0f;
		int totalRows = (g_atlas.glyph_count + gridCols - 1) / gridCols;
		float maxRow = (float)(totalRows > 0 ? totalRows - 1 : 0);
		if (scrollY > maxRow) scrollY = maxRow;
	}
}

static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
	(void)mods;
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		dragging = (action == GLFW_PRESS);
		glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
	}
}

/* GLFW char callback delivers the *layout-translated* codepoint the user
 * actually typed, not a physical scancode — so the r/g/b/a/c shortcuts
 * follow the user's keyboard layout (Dvorak, AZERTY, etc.). Polling
 * glfwGetKey(GLFW_KEY_R) instead would always check the QWERTY-R slot. */
static void charCallback(GLFWwindow *window, unsigned int codepoint)
{
	(void)window;
	switch (codepoint) {
	case 'r': case 'R':
		chR = !chR;
		fprintf(stderr, "[ch] R=%d G=%d B=%d A=%d\n", chR, chG, chB, chA);
		break;
	case 'g': case 'G':
		chG = !chG;
		fprintf(stderr, "[ch] R=%d G=%d B=%d A=%d\n", chR, chG, chB, chA);
		break;
	case 'b': case 'B':
		chB = !chB;
		fprintf(stderr, "[ch] R=%d G=%d B=%d A=%d\n", chR, chG, chB, chA);
		break;
	case 'a': case 'A':
		chA = !chA;
		fprintf(stderr, "[ch] R=%d G=%d B=%d A=%d\n", chR, chG, chB, chA);
		break;
	case 'c': case 'C':
		/* Advance the cycle and reset channel masks so each stop is a
		 * clean view — leftover r/g/b/a toggles from earlier inspection
		 * would otherwise tint the median or single-channel modes. */
		displayMode = (displayMode + 1) % 5;
		chR = chG = chB = chA = 1;
		fprintf(stderr, "[mode] %d (%s)  [ch reset: R=1 G=1 B=1 A=1]\n",
			displayMode, DISPLAY_MODE_NAMES[displayMode]);
		break;
	default:
		break;
	}
}

/* Key callback handles non-printable keys that have no character — only
 * ESC for now (reset view). Letter shortcuts go through charCallback so
 * they respect the user's keyboard layout. */
static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	(void)window; (void)scancode; (void)mods;
	if (action != GLFW_PRESS) return;
	if (key == GLFW_KEY_ESCAPE) {
		resetView();
	}
}

static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
	(void)window;
	if (dragging) {
		float windowAspect = (float)windowWidth / (float)windowHeight;
		float dx = (float)(xpos - lastMouseX) / windowWidth * 2.0f * windowAspect / zoom;
		float dy = (float)(ypos - lastMouseY) / windowHeight * 2.0f / zoom;
		panX -= dx;
		panY -= dy;
		lastMouseX = xpos;
		lastMouseY = ypos;
	}
}

static void printUsage(const char *prog)
{
	printf("MSDF Atlas Viewer (Grid Mode)\n\n"
	       "Usage: %s [options] <path-to.cdb>\n\n"
	       "Options:\n"
	       "      --cols N       Grid columns (default: 80)\n"
	       "      --red          Show only the red channel (combine with --green etc.)\n"
	       "      --green        Show only the green channel\n"
	       "      --blue         Show only the blue channel\n"
	       "      --alpha        Show only the alpha channel\n"
	       "                     (no channel flag = all channels visible)\n"
	       "  -h, --help         Show this help\n\n"
	       "Controls (lowercase keys, no shift needed):\n"
	       "  mouse wheel        Scroll through glyphs\n"
	       "  Ctrl+wheel         Zoom to cursor\n"
	       "  mouse drag         Pan the view\n"
	       "  r / g / b / a      Toggle red / green / blue / alpha channel\n"
	       "  c                  Cycle display mode (median3 / raw RGB / R / G / B)\n"
	       "  esc                Reset view\n"
	       "  Ctrl+C             Exit\n",
	       prog);
}

int main(int argc, char *argv[])
{
	const char *cdbPath = NULL;
	int channelFlagSeen = 0;
	int wantR = 0, wantG = 0, wantB = 0, wantA = 0;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (!strcmp(arg, "--cols") && i + 1 < argc) {
			gridCols = atoi(argv[++i]);
			if (gridCols < 1) gridCols = 1;
		} else if (!strcmp(arg, "--red"))   { wantR = 1; channelFlagSeen = 1; }
		else if (!strcmp(arg, "--green")) { wantG = 1; channelFlagSeen = 1; }
		else if (!strcmp(arg, "--blue"))  { wantB = 1; channelFlagSeen = 1; }
		else if (!strcmp(arg, "--alpha")) { wantA = 1; channelFlagSeen = 1; }
		else if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
			printUsage(argv[0]);
			return 0;
		} else if (arg[0] == '-' && arg[1] != '\0') {
			fprintf(stderr, "%s: unknown option %s\n", argv[0], arg);
			printUsage(argv[0]);
			return 2;
		} else if (!cdbPath) {
			cdbPath = arg;
		} else {
			fprintf(stderr, "%s: extra argument %s\n", argv[0], arg);
			return 2;
		}
	}
	if (!cdbPath) {
		printUsage(argv[0]);
		return 2;
	}
	/* If any channel flag was passed, enable only those; otherwise all on. */
	if (channelFlagSeen) {
		chR = wantR; chG = wantG; chB = wantB; chA = wantA;
	}

	/* Trap SIGINT so Ctrl+C from the terminal exits the loop cleanly
	 * (otherwise the default handler kills the process mid-frame and
	 * skips the WGPU/GLFW cleanup paths). */
	signal(SIGINT, on_sigint);

	printf("========================================\n"
	       "MSDF Atlas Viewer (Grid Mode)\n"
	       "========================================\n"
	       "CDB:          %s\n"
	       "Grid columns: %d\n"
	       "========================================\n\n",
	       cdbPath, gridCols);

	if (atlas_load_cdb(&g_atlas, cdbPath) < 0) {
		fprintf(stderr, "Failed to load CDB: %s\n", cdbPath);
		return 1;
	}
	fprintf(stderr, "[Atlas] %d glyphs packed into %dx%d\n",
		g_atlas.glyph_count, g_atlas.width, g_atlas.height);

	if (!glfwInit()) {
		fprintf(stderr, "Failed to initialize GLFW\n");
		atlas_free(&g_atlas);
		return 1;
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *window = glfwCreateWindow(windowWidth, windowHeight,
					      "MSDF-WGSL Atlas Viewer", NULL, NULL);

	glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
	glfwSetScrollCallback(window, scrollCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetCursorPosCallback(window, cursorPosCallback);
	glfwSetCharCallback(window, charCallback);
	glfwSetKeyCallback(window, keyCallback);

	if (!initWebGPU(window)) {
		fprintf(stderr, "Failed to initialize WebGPU\n");
		glfwTerminate();
		atlas_free(&g_atlas);
		return 1;
	}

	if (!initRendering()) {
		fprintf(stderr, "Failed to initialize rendering\n");
		cleanup();
		glfwTerminate();
		return 1;
	}

	resetView();

	int totalRows = (g_atlas.glyph_count + gridCols - 1) / gridCols;
	fprintf(stderr, "[Grid] %d glyphs, %d cols, %d rows\n",
		g_atlas.glyph_count, gridCols, totalRows);
	fprintf(stderr, "[Main] Starting render loop... "
		"(scroll, ctrl+scroll, drag, R, ESC)\n");

	while (!glfwWindowShouldClose(window) && !g_should_exit) {
		/* Layout-aware: r/g/b/a/c go through glfwSetCharCallback, ESC
		 * through glfwSetKeyCallback (no character to translate).
		 * Both fire from inside glfwPollEvents — no polling here. */
		glfwPollEvents();
		render();
	}

	cleanup();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
