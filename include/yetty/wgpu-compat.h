#pragma once

// Compatibility macros for WebGPU API differences between:
// - wgpu-native v27+ (desktop/Android): Uses WGPUStringView, new callback style
// - Emscripten (web): Uses const char*, old callback style

#include <webgpu/webgpu.h>

#if YETTY_WEB
// Emscripten uses older WebGPU API with const char* strings
#define WGPU_STR(s) (s)
#define WGPU_STR_NULL nullptr
#define WGPU_USE_OLD_API 1

// Type aliases for renamed WebGPU types (wgpu-native v27 names -> Emscripten names)
using WGPUTexelCopyBufferLayout = WGPUTextureDataLayout;
using WGPUTexelCopyTextureInfo = WGPUImageCopyTexture;

// Shader source: WGPUShaderSourceWGSL -> WGPUShaderModuleWGSLDescriptor
using WGPUShaderSourceWGSL = WGPUShaderModuleWGSLDescriptor;
#define WGPUSType_ShaderSourceWGSL WGPUSType_ShaderModuleWGSLDescriptor

// Shader code: in Emscripten it's const char*, in v27 it's WGPUStringView
#define WGPU_SHADER_CODE(desc, src) (desc).code = (src).c_str()

// Render pass color attachment: clearColor -> clearValue
#define WGPU_COLOR_ATTACHMENT_CLEAR(attachment, r, g, b, a) \
    (attachment).clearValue = {(r), (g), (b), (a)}

// Mipmap filter (both have the enum, but use compat macro for consistency)
#define WGPU_MIPMAP_FILTER_LINEAR WGPUMipmapFilterMode_Linear
#define WGPU_MIPMAP_FILTER_NEAREST WGPUMipmapFilterMode_Nearest

#else
// wgpu-native v27+ uses WGPUStringView
#define WGPU_STR(s) (WGPUStringView{ .data = (s), .length = WGPU_STRLEN })
#define WGPU_STR_NULL (WGPUStringView{ .data = nullptr, .length = 0 })
#define WGPU_USE_OLD_API 0

// Shader code: in v27 it's WGPUStringView
#define WGPU_SHADER_CODE(desc, src) (desc).code = { .data = (src).c_str(), .length = (src).size() }

// Render pass color attachment uses clearColor in v27
#define WGPU_COLOR_ATTACHMENT_CLEAR(attachment, r, g, b, a) \
    (attachment).clearColor = {(r), (g), (b), (a)}

// Mipmap filter
#define WGPU_MIPMAP_FILTER_LINEAR WGPUMipmapFilterMode_Linear
#define WGPU_MIPMAP_FILTER_NEAREST WGPUMipmapFilterMode_Nearest
#endif
