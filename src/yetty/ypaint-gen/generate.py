#!/usr/bin/env python3
"""
Complex primitive code generator.

Reads YAML schema, generates ALL boilerplate:
- C header: struct definition, serialization API, factory API
- C source: serialization, factory, instance implementation
- WGSL: uniform struct and buffer bindings

User provides only: YAML schema + WGSL shader
Generator produces: everything else

Architecture:
- uniforms: section -> GPU uniform buffer (@group(0) @binding(0) var<uniform>)
- buffers: section -> GPU storage buffer(s) (@group(0) @binding(1+) var<storage, read>)

Usage: python generate.py <schema.yaml>
Output files written to same directory as schema.
"""

import sys
import yaml
from pathlib import Path

# Type info: (c_type, wgsl_type, size_bytes, render_uniform_type)
TYPES = {
    'f32': ('float', 'f32', 4, 'YETTY_YRENDER_UNIFORM_F32'),
    'u32': ('uint32_t', 'u32', 4, 'YETTY_YRENDER_UNIFORM_U32'),
    'i32': ('int32_t', 'i32', 4, 'YETTY_YRENDER_UNIFORM_I32'),
}


def load_schema(path):
    with open(path) as f:
        return yaml.safe_load(f)


def calculate_layout(schema):
    """Calculate layout for uniforms and buffers."""
    uniforms = []
    buffers = []

    # Uniforms - go to GPU uniform buffer
    for u in schema.get('uniforms', []):
        count = u.get('count', 1)
        t = TYPES[u['type']]
        uniforms.append({
            'name': u['name'],
            'type': u['type'],
            'c_type': t[0],
            'wgsl_type': t[1],
            'render_type': t[3],
            'count': count,
            'default': u.get('default'),
        })

    # Buffers - go to GPU storage buffer(s)
    for b in schema.get('buffers', []):
        buffers.append({
            'name': b['name'],
            'element_type': b['element_type'],
        })

    return uniforms, buffers


def generate_c_header(schema, uniforms, buffers):
    name = schema['name']
    NAME = name.upper()
    type_id = schema['type_id']
    if isinstance(type_id, str):
        type_id = int(type_id, 16)

    struct_fields = []
    for u in uniforms:
        if u['count'] > 1:
            struct_fields.append(f'    {u["c_type"]} {u["name"]}[{u["count"]}];')
        else:
            struct_fields.append(f'    {u["c_type"]} {u["name"]};')
    struct_fields_str = '\n'.join(struct_fields)

    buf_struct_fields = []
    for b in buffers:
        buf_struct_fields.append(f'    const uint32_t *{b["name"]};')
        buf_struct_fields.append(f'    size_t {b["name"]}_len;')
    buf_struct_fields_str = '\n'.join(buf_struct_fields)

    return f'''// Auto-generated from {name}.yaml - DO NOT EDIT
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/complex-prim-types.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define YETTY_{NAME}_TYPE_ID 0x{type_id:08x}u

// Uniforms struct (goes to GPU uniform buffer)
struct yetty_{name}_uniforms {{
{struct_fields_str}
}};

// Buffers struct (goes to GPU storage buffer)
struct yetty_{name}_buffers {{
{buf_struct_fields_str}
}};

//=============================================================================
// Serialization API
//=============================================================================

size_t yetty_{name}_serialized_size(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers);

struct yetty_ycore_size_result yetty_{name}_serialize(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers,
    uint8_t *out, size_t out_capacity);

//=============================================================================
// Factory API (creates binder with pre-compiled pipeline)
//=============================================================================

struct yetty_ypaint_core_concrete_factory *yetty_{name}_factory_create(void);
void yetty_{name}_factory_destroy(struct yetty_ypaint_core_concrete_factory *factory);

//=============================================================================
// YAML parser registration
//=============================================================================

struct yetty_ypaint_yaml_parser;
struct yetty_ycore_void_result yetty_{name}_register_yaml_factory(
    struct yetty_ypaint_yaml_parser *parser);

#ifdef __cplusplus
}}
#endif
'''


def generate_c_source(schema, uniforms, buffers):
    name = schema['name']
    NAME = name.upper()
    libraries = schema.get('libraries', [])

    # Calculate sizes
    uniforms_word_count = sum(u['count'] for u in uniforms)
    buffer_len_fields = len(buffers)

    lib_includes = '\n'.join([f'#include <yetty/{lib}/compiler.h>' for lib in libraries])

    # Generate buffer length sum expression
    buf_len_sum_parts = [f'buffers->{b["name"]}_len' for b in buffers]
    buf_len_sum = ' + '.join(buf_len_sum_parts) if buf_len_sum_parts else '0'

    # Generate buffer length writes
    buf_len_writes = '\n'.join([f'    *p++ = (uint32_t)buffers->{b["name"]}_len;' for b in buffers])

    # Generate buffer data copies
    buf_copies = '\n'.join([f'''    if (buffers->{b["name"]} && buffers->{b["name"]}_len > 0)
        memcpy(p, buffers->{b["name"]}, buffers->{b["name"]}_len * sizeof(uint32_t));
    p += buffers->{b["name"]}_len;''' for b in buffers])

    # Generate uniform setup code (populate rs->uniforms)
    uniform_setup = []
    uniform_idx = 0
    for u in uniforms:
        if u['count'] > 1:
            for i in range(u['count']):
                uniform_setup.append(f'''    strncpy(rs->uniforms[{uniform_idx}].name, "{u["name"]}_{i}", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{uniform_idx}].type = {u["render_type"]};
    rs->uniforms[{uniform_idx}].u32 = 0;''')
                uniform_idx += 1
        else:
            uniform_setup.append(f'''    strncpy(rs->uniforms[{uniform_idx}].name, "{u["name"]}", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{uniform_idx}].type = {u["render_type"]};
    rs->uniforms[{uniform_idx}].u32 = 0;''')
            uniform_idx += 1

    # Zoom state — tacked on AFTER schema uniforms and NOT present in the wire
    # format. Two independent uniform pairs with SEPARATE semantics:
    #   visual_zoom_* : non-intrusive (Ctrl+Scroll, mouse-anchored)
    #   cell_zoom_*   : intrusive (Ctrl+Shift+Scroll, cell-size scale)
    # Plus viewport_w/h (read from the target each frame). Shader composes
    # both transforms at fs_main entry — SDF math re-evaluates at the zoomed
    # coordinate, crisp at any scale.
    vz_scale_idx = uniform_idx
    vz_off_x_idx = uniform_idx + 1
    vz_off_y_idx = uniform_idx + 2
    cz_scale_idx = uniform_idx + 3
    cz_off_x_idx = uniform_idx + 4
    cz_off_y_idx = uniform_idx + 5
    vp_w_idx    = uniform_idx + 6
    vp_h_idx    = uniform_idx + 7
    uniform_setup.append(f'''    strncpy(rs->uniforms[{vz_scale_idx}].name, "visual_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{vz_scale_idx}].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[{vz_scale_idx}].f32 = 1.0f;
    strncpy(rs->uniforms[{vz_off_x_idx}].name, "visual_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{vz_off_x_idx}].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[{vz_off_x_idx}].f32 = 0.0f;
    strncpy(rs->uniforms[{vz_off_y_idx}].name, "visual_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{vz_off_y_idx}].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[{vz_off_y_idx}].f32 = 0.0f;
    strncpy(rs->uniforms[{cz_scale_idx}].name, "cell_zoom_scale", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{cz_scale_idx}].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[{cz_scale_idx}].f32 = 1.0f;
    strncpy(rs->uniforms[{cz_off_x_idx}].name, "cell_zoom_off_x", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{cz_off_x_idx}].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[{cz_off_x_idx}].f32 = 0.0f;
    strncpy(rs->uniforms[{cz_off_y_idx}].name, "cell_zoom_off_y", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{cz_off_y_idx}].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[{cz_off_y_idx}].f32 = 0.0f;
    strncpy(rs->uniforms[{vp_w_idx}].name, "viewport_w", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{vp_w_idx}].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[{vp_w_idx}].f32 = 0.0f;
    strncpy(rs->uniforms[{vp_h_idx}].name, "viewport_h", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{vp_h_idx}].type = YETTY_YRENDER_UNIFORM_F32;
    rs->uniforms[{vp_h_idx}].f32 = 0.0f;''')
    uniform_idx += 8

    uniform_setup_str = '\n'.join(uniform_setup)
    total_uniform_count = uniform_idx

    # Generate uniform update code (from wire format to rs->uniforms)
    uniform_update = []
    wire_offset = 0
    uniform_idx = 0
    for u in uniforms:
        if u['count'] > 1:
            for i in range(u['count']):
                if u['type'] == 'f32':
                    uniform_update.append(f'    rs->uniforms[{uniform_idx}].f32 = *(float *)&payload[{wire_offset + i}];')
                else:
                    uniform_update.append(f'    rs->uniforms[{uniform_idx}].u32 = payload[{wire_offset + i}];')
                uniform_idx += 1
            wire_offset += u['count']
        else:
            if u['type'] == 'f32':
                uniform_update.append(f'    rs->uniforms[{uniform_idx}].f32 = *(float *)&payload[{wire_offset}];')
            else:
                uniform_update.append(f'    rs->uniforms[{uniform_idx}].u32 = payload[{wire_offset}];')
            uniform_idx += 1
            wire_offset += 1
    uniform_update_str = '\n'.join(uniform_update)

    # Buffer offset in wire format (after uniforms)
    buffer_data_offset = uniforms_word_count + buffer_len_fields

    # Library children setup - accessor lib is children[0], external libs start at [1]
    lib_children_parts = [f'''    // Accessor library (generated uniforms accessors)
    rs->children[0] = (struct yetty_yrender_gpu_resource_set *)&{name}_lib_rs;
    rs->children_count = 1;''']
    for i, lib in enumerate(libraries):
        lib_children_parts.append(f'''    // Library: {lib}
    const struct yetty_yrender_gpu_resource_set *{lib}_rs =
        yetty_{lib}_get_shader_resource_set();
    if ({lib}_rs) {{
        rs->children[{i + 1}] = (struct yetty_yrender_gpu_resource_set *){lib}_rs;
        rs->children_count = {i + 2};
    }}''')
    lib_children = '\n'.join(lib_children_parts)

    return f'''// Auto-generated from {name}.yaml - DO NOT EDIT
//
// Two-tier complex-prim model:
//   - factory owns ONE shared yetty_yrender_pipeline (compiled once at
//     compile_pipeline time from a template resource_set; the pipeline
//     carries the WGPUShaderModule + bind_group_layout + WGPURenderPipeline
//     + shared quad VB).
//   - each instance owns its OWN heap-allocated yetty_yrender_gpu_resource_set
//     (per-instance uniform values + storage buffer pointer) and its own
//     gpu_resource_binder (per-instance WGPUUniformBuffer + WGPUStorageBuffer
//     + WGPUBindGroup), referencing the factory's pipeline by const pointer.
//   - factory holds zoom state as plain floats; instances read it at render
//     time and write into their own RS uniforms (no shared mutable RS).

#include <yetty/{name}/{name}-gen.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/pipeline.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ypaint-core/complex-prim-types.h>
#include <yetty/ytrace.h>
#include <stdlib.h>
#include <string.h>
{lib_includes}

extern const unsigned char g{name}_shaderData[];
extern const unsigned int g{name}_shaderSize;
extern const unsigned char g{name}_lib_shaderData[];
extern const unsigned int g{name}_lib_shaderSize;

/* Static resource set for accessor library ({name}-gen.wgsl).
 * Read-only after init; safely shared across all instances as a child. */
static struct yetty_yrender_gpu_resource_set {name}_lib_rs;
static bool {name}_lib_rs_initialized = false;

static void {name}_init_lib_rs(void)
{{
    if ({name}_lib_rs_initialized)
        return;
    memset(&{name}_lib_rs, 0, sizeof({name}_lib_rs));
    yetty_yrender_shader_code_set(&{name}_lib_rs.shader,
        (const char *)g{name}_lib_shaderData, g{name}_lib_shaderSize);
    {name}_lib_rs_initialized = true;
}}

struct {name}_factory {{
    struct yetty_ypaint_core_concrete_factory base;
    /* Shared, compiled once. NULL until compile_pipeline. */
    struct yetty_yrender_pipeline *pipeline;
    /* Template RS: shape definition for both the pipeline and per-instance
     * RSes. Children point to the shared static library RSes. */
    struct yetty_yrender_gpu_resource_set template_rs;
    int template_initialized;

    WGPUDevice device;
    WGPUQueue queue;
    struct yetty_yrender_gpu_allocator *allocator;

    /* Zoom state — written by the canvas into the factory, read by each
     * instance render() and pushed into the instance's own RS uniforms. */
    float visual_zoom_scale;
    float visual_zoom_off_x;
    float visual_zoom_off_y;
    float cell_zoom_scale;
    float cell_zoom_off_x;
    float cell_zoom_off_y;
}};

static struct {name}_factory *{name}_factory_from_base(struct yetty_ypaint_core_concrete_factory *base)
{{
    return (struct {name}_factory *)base;
}}

//=============================================================================
// Serialization
//=============================================================================

size_t yetty_{name}_serialized_size(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers)
{{
    (void)uniforms;
    // Wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    size_t total_buf_words = {buf_len_sum};
    return (2 + {uniforms_word_count} + {buffer_len_fields} + total_buf_words) * sizeof(uint32_t);
}}

struct yetty_ycore_size_result yetty_{name}_serialize(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers,
    uint8_t *out, size_t out_capacity)
{{
    if (!uniforms || !buffers)
        return YETTY_ERR(yetty_ycore_size, "null argument");
    if (!out)
        return YETTY_ERR(yetty_ycore_size, "out is NULL");

    size_t total_buf_words = {buf_len_sum};
    size_t required = (2 + {uniforms_word_count} + {buffer_len_fields} + total_buf_words) * sizeof(uint32_t);
    if (out_capacity < required)
        return YETTY_ERR(yetty_ycore_size, "buffer too small");

    uint32_t *p = (uint32_t *)out;
    *p++ = YETTY_{NAME}_TYPE_ID;
    *p++ = (uint32_t)(required - 2 * sizeof(uint32_t));

    // Copy uniforms as raw words
    memcpy(p, uniforms, sizeof(struct yetty_{name}_uniforms));
    p += {uniforms_word_count};

    // Write buffer lengths
{buf_len_writes}

    // Copy buffer data
{buf_copies}

    return YETTY_OK(yetty_ycore_size, required);
}}

//=============================================================================
// Resource Set Setup — populates a target RS with this prim's structure
// (uniform names/types, buffer descriptor, library children + own shader
// code). Same shape used for the factory's template_rs (pipeline-build) and
// for each per-instance RS (binder-build) — they're memcpy clones.
//=============================================================================

static void {name}_populate_rs(struct yetty_yrender_gpu_resource_set *rs)
{{
    {name}_init_lib_rs();

    memset(rs, 0, sizeof(*rs));
    strncpy(rs->namespace, "{name}", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&rs->shader,
        (const char *)g{name}_shaderData, g{name}_shaderSize);

{lib_children}

    // Setup uniforms (values set later during render)
{uniform_setup_str}
    rs->uniform_count = {total_uniform_count};

    // Setup storage buffer for buffer data
    rs->buffer_count = 1;
    strncpy(rs->buffers[0].name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->buffers[0].readonly = 1;
}}

//=============================================================================
// Instance Rendering — uses self->resource_set + self->binder; the factory
// supplies only the shared pipeline + zoom state.
//=============================================================================

static struct yetty_ycore_void_result
{name}_instance_render(struct yetty_ypaint_core_complex_prim_instance *self,
                       struct yetty_yrender_target *target, float x, float y)
{{
    if (!self || !self->buffer_data || !self->factory)
        return YETTY_ERR(yetty_ycore_void, "invalid instance");
    if (!self->resource_set || !self->binder)
        return YETTY_ERR(yetty_ycore_void, "instance not finalised");

    struct {name}_factory *factory = {name}_factory_from_base(self->factory);
    if (!factory->pipeline)
        return YETTY_ERR(yetty_ycore_void, "factory pipeline not initialized");

    struct yetty_yrender_gpu_resource_set *rs = self->resource_set;

    // Parse wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    const uint32_t *data = (const uint32_t *)self->buffer_data;
    const uint32_t *payload = data + 2;  // skip type_id and payload_size

    // Update uniforms from wire format
{uniform_update_str}

    // Pull current zoom state from the factory into this instance's RS.
    rs->uniforms[{vz_scale_idx}].f32 =
        factory->visual_zoom_scale > 0.0f ? factory->visual_zoom_scale : 1.0f;
    rs->uniforms[{vz_off_x_idx}].f32 = factory->visual_zoom_off_x;
    rs->uniforms[{vz_off_y_idx}].f32 = factory->visual_zoom_off_y;
    rs->uniforms[{cz_scale_idx}].f32 =
        factory->cell_zoom_scale > 0.0f ? factory->cell_zoom_scale : 1.0f;
    rs->uniforms[{cz_off_x_idx}].f32 = factory->cell_zoom_off_x;
    rs->uniforms[{cz_off_y_idx}].f32 = factory->cell_zoom_off_y;

    // Visual-zoom viewport — read from the target every frame.
    rs->uniforms[{vp_w_idx}].f32 = target->viewport.w;
    rs->uniforms[{vp_h_idx}].f32 = target->viewport.h;

    // Override bounds_x / bounds_y with the caller-provided screen position
    // (wire bounds are the pre-scroll origin; x,y are the post-scroll pane
    // position the instance should render at).
    rs->uniforms[0].f32 = x;
    rs->uniforms[1].f32 = y;

    // Get buffer data (after uniforms and length fields)
    const uint32_t *buffer_data = payload + {buffer_data_offset};
    size_t buffer_words = payload[{uniforms_word_count}];  // first buffer length

    // Update storage buffer
    rs->buffers[0].data = (uint8_t *)buffer_data;
    rs->buffers[0].size = buffer_words * sizeof(uint32_t);
    rs->buffers[0].dirty = 1;

    // Update the per-instance binder. Each instance has its own GPU
    // uniform_buffer / storage_buffer / bind_group, so concurrent renders
    // of multiple instances do NOT trample each other's data.
    struct yetty_ycore_void_result res = self->binder->ops->update(self->binder);
    if (YETTY_IS_ERR(res))
        return YETTY_ERR(yetty_ycore_void, "binder update failed", res);

    // Get target view and create render pass
    WGPUTextureView view = target->ops->get_view(target);
    if (!view)
        return YETTY_ERR(yetty_ycore_void, "failed to get target view");

    WGPUCommandEncoderDescriptor enc_desc = {{0}};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(factory->device, &enc_desc);
    if (!encoder)
        return YETTY_ERR(yetty_ycore_void, "failed to create encoder");

    // Render pass with LoadOp=Load to preserve existing content
    WGPURenderPassColorAttachment color_attachment = {{0}};
    color_attachment.view = view;
    color_attachment.loadOp = WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {{0}};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {{
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "failed to begin render pass");
    }}

    wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f,
        target->viewport.w, target->viewport.h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0,
        (uint32_t)target->viewport.w, (uint32_t)target->viewport.h);

    float w = self->bounds.max.x - self->bounds.min.x;
    float h = self->bounds.max.y - self->bounds.min.y;

    // Pipeline + quad VB are shared (factory). Bind group is per-instance.
    yetty_yrender_pipeline_bind(factory->pipeline, pass);
    self->binder->ops->bind(self->binder, pass, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);  // fullscreen triangle

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {{0}};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(factory->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    ydebug("{name}_instance_render: rendered at (%.1f, %.1f) size (%.1f x %.1f) inst=%p",
           x, y, w, h, (void *)self);
    return YETTY_OK_VOID();
}}

//=============================================================================
// Factory Implementation
//=============================================================================

static struct yetty_ycore_void_result
{name}_compile_pipeline(struct yetty_ypaint_core_concrete_factory *self,
                        WGPUDevice device, WGPUQueue queue,
                        WGPUTextureFormat target_format,
                        struct yetty_yrender_gpu_allocator *allocator)
{{
    struct {name}_factory *factory = {name}_factory_from_base(self);

    if (factory->pipeline) {{
        ydebug("{name}: factory pipeline already initialized");
        return YETTY_OK_VOID();
    }}

    factory->device = device;
    factory->queue = queue;
    factory->allocator = allocator;
    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    {name}_populate_rs(&factory->template_rs);
    factory->template_initialized = 1;

    struct yetty_yrender_pipeline_ptr_result pr = yetty_yrender_pipeline_create(
        device, target_format, allocator, &factory->template_rs);
    if (YETTY_IS_ERR(pr))
        return YETTY_ERR(yetty_ycore_void, "{name} pipeline_create failed", pr);
    factory->pipeline = pr.value;

    yinfo("{name}: pipeline compiled (shared across all instances)");
    return YETTY_OK_VOID();
}}

static WGPURenderPipeline {name}_get_pipeline(struct yetty_ypaint_core_concrete_factory *self)
{{
    struct {name}_factory *factory = {name}_factory_from_base(self);
    return factory->pipeline ? yetty_yrender_pipeline_get_pipeline(factory->pipeline) : NULL;
}}

static struct yetty_ypaint_core_complex_prim_instance_ptr_result
{name}_create_instance(struct yetty_ypaint_core_concrete_factory *self,
                       const void *buffer_data, size_t size, uint32_t rolling_row)
{{
    if (!buffer_data || size < sizeof(struct yetty_ypaint_complex_prim))
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "invalid buffer data");

    struct {name}_factory *factory = {name}_factory_from_base(self);
    if (!factory->pipeline)
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "{name} factory pipeline not compiled");

    struct yetty_ypaint_core_complex_prim_instance *instance =
        calloc(1, sizeof(struct yetty_ypaint_core_complex_prim_instance));
    if (!instance)
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "allocation failed");

    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {{
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "buffer alloc failed");
    }}
    memcpy(instance->buffer_data, buffer_data, size);
    instance->buffer_size = size;
    instance->type = YETTY_{NAME}_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->render = {name}_instance_render;

    struct rectangle_result aabb_res = yetty_ypaint_complex_prim_aabb(buffer_data);
    if (YETTY_IS_OK(aabb_res))
        instance->bounds = aabb_res.value;

    /* Per-instance RS. Same shape as the factory template (so the binder
     * flattens to the same layout the pipeline was compiled against), but
     * with per-instance buffer/uniform values (set in render). */
    instance->resource_set = malloc(sizeof(struct yetty_yrender_gpu_resource_set));
    if (!instance->resource_set) {{
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "rs alloc failed");
    }}
    memcpy(instance->resource_set, &factory->template_rs,
           sizeof(struct yetty_yrender_gpu_resource_set));

    /* Point the storage buffer descriptor at this instance's bytecode now,
     * so the binder's first finalize allocates a GPU buffer of the right
     * size and queueWriteBuffers the data. */
    {{
        const uint32_t *data = (const uint32_t *)instance->buffer_data;
        const uint32_t *payload = data + 2;
        size_t buffer_words = payload[{uniforms_word_count}];
        const uint32_t *buffer_payload = payload + {buffer_data_offset};
        instance->resource_set->buffers[0].data = (uint8_t *)buffer_payload;
        instance->resource_set->buffers[0].size = buffer_words * sizeof(uint32_t);
        instance->resource_set->buffers[0].dirty = 1;
    }}

    /* Per-instance binder bound to the factory's shared pipeline. Owns
     * its OWN uniform_buffer / storage_buffer / bind_group. */
    struct yetty_yrender_gpu_resource_binder_result br =
        yetty_yrender_gpu_resource_binder_create_with_pipeline(
            factory->device, factory->queue, factory->allocator, factory->pipeline);
    if (YETTY_IS_ERR(br)) {{
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "instance binder create failed", br);
    }}
    instance->binder = br.value;

    struct yetty_ycore_void_result sr =
        instance->binder->ops->submit(instance->binder, instance->resource_set);
    if (YETTY_IS_ERR(sr)) {{
        instance->binder->ops->destroy(instance->binder);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "binder submit failed", sr);
    }}

    struct yetty_ycore_void_result fr = instance->binder->ops->finalize(instance->binder);
    if (YETTY_IS_ERR(fr)) {{
        instance->binder->ops->destroy(instance->binder);
        free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "binder finalize failed", fr);
    }}

    return YETTY_OK(yetty_ypaint_core_complex_prim_instance_ptr, instance);
}}

static void {name}_destroy_instance(struct yetty_ypaint_core_concrete_factory *self,
                                    struct yetty_ypaint_core_complex_prim_instance *instance)
{{
    (void)self;
    if (!instance)
        return;
    if (instance->binder)
        instance->binder->ops->destroy(instance->binder);
    free(instance->resource_set);
    free(instance->buffer_data);
    free(instance);
}}

static struct yetty_yrender_gpu_resource_set *{name}_get_shared_rs(
    struct yetty_ypaint_core_concrete_factory *self)
{{
    /* Returns the structural template, NOT a mutable per-instance RS. */
    struct {name}_factory *factory = {name}_factory_from_base(self);
    return factory->template_initialized ? &factory->template_rs : NULL;
}}

static struct yetty_ycore_void_result
{name}_set_visual_zoom(struct yetty_ypaint_core_concrete_factory *self,
                       float scale, float off_x, float off_y)
{{
    struct {name}_factory *factory = {name}_factory_from_base(self);
    factory->visual_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->visual_zoom_off_x = off_x;
    factory->visual_zoom_off_y = off_y;
    return YETTY_OK_VOID();
}}

static struct yetty_ycore_void_result
{name}_set_cell_zoom(struct yetty_ypaint_core_concrete_factory *self,
                     float scale, float off_x, float off_y)
{{
    struct {name}_factory *factory = {name}_factory_from_base(self);
    factory->cell_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->cell_zoom_off_x = off_x;
    factory->cell_zoom_off_y = off_y;
    ydebug("{name}_set_cell_zoom: scale=%.3f off=(%.1f,%.1f)", scale, off_x, off_y);
    return YETTY_OK_VOID();
}}

struct yetty_ypaint_core_concrete_factory *yetty_{name}_factory_create(void)
{{
    struct {name}_factory *factory = calloc(1, sizeof(struct {name}_factory));
    if (!factory)
        return NULL;

    factory->base.type_id = YETTY_{NAME}_TYPE_ID;
    factory->base.compile_pipeline = {name}_compile_pipeline;
    factory->base.get_pipeline = {name}_get_pipeline;
    factory->base.create_instance = {name}_create_instance;
    factory->base.destroy_instance = {name}_destroy_instance;
    factory->base.get_shared_rs = {name}_get_shared_rs;
    factory->base.set_visual_zoom = {name}_set_visual_zoom;
    factory->base.set_cell_zoom = {name}_set_cell_zoom;

    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    return &factory->base;
}}

void yetty_{name}_factory_destroy(struct yetty_ypaint_core_concrete_factory *self)
{{
    if (!self)
        return;

    struct {name}_factory *factory = {name}_factory_from_base(self);

    if (factory->pipeline)
        yetty_yrender_pipeline_destroy(factory->pipeline);
    free(factory);
}}
'''


def generate_wgsl_bindings(schema, uniforms, buffers):
    """Generate WGSL uniform struct and buffer bindings."""
    name = schema['name']
    NAME = name.upper()

    # Build uniform struct fields with WGSL alignment
    # Note: binder auto-generates the uniform struct from rs->uniforms,
    # but we still need accessor functions for the shader to use

    out = f'''// Auto-generated from {name}.yaml - DO NOT EDIT
// Uniform accessors - read from uniforms struct generated by binder
// Buffer accessors - read from storage buffer
'''

    # Generate uniform accessor functions
    # The binder creates: struct Uniforms {{ {name}_field: type; ... }}
    # Accessible via: uniforms.{name}_field
    out += '\n// Uniform accessors\n'
    for u in uniforms:
        if u['count'] > 1:
            out += f'''
fn {name}_get_{u["name"]}(idx: u32) -> {u["wgsl_type"]} {{
    // Array uniform: {name}_{u["name"]}_0 through {name}_{u["name"]}_{u["count"]-1}
    // Access via switch since WGSL doesn't support dynamic struct field access
'''
            out += '    switch idx {\n'
            for i in range(u['count']):
                out += f'        case {i}u: {{ return uniforms.{name}_{u["name"]}_{i}; }}\n'
            out += f'        default: {{ return uniforms.{name}_{u["name"]}_0; }}\n'
            out += '    }\n}\n'
        else:
            out += f'''
fn {name}_get_{u["name"]}() -> {u["wgsl_type"]} {{
    return uniforms.{name}_{u["name"]};
}}
'''

    # Generate buffer accessor comment
    out += '\n// Buffer data is in storage_buffer (binding 1+)\n'
    out += '// Access via: storage_buffer[offset]\n'

    return out


def generate_yaml_parser(schema, uniforms, buffers):
    """Generate YAML parsing code from schema."""
    name = schema['name']
    NAME = name.upper()

    # Generate defaults from schema
    defaults = []
    for u in uniforms:
        if u['default'] is not None:
            suffix = 'f' if u['type'] == 'f32' else ''
            defaults.append(f"    uniforms.{u['name']} = {u['default']}{suffix};")
    defaults_code = '\n'.join(defaults)

    # Generate yaml_mapping handling from schema
    yaml_mapping = schema.get('yaml_mapping', {})
    mapping_checks = []
    for yaml_key, field_list in yaml_mapping.items():
        fields = ', '.join([f"uniforms.{f} = array_vals[{i}]" for i, f in enumerate(field_list)])
        mapping_checks.append(
            f'                if (strcmp(prop_key, "{yaml_key}") == 0 && array_idx >= {len(field_list)}) {{\n'
            f'                    {fields};\n'
            f'                }}'
        )
    mapping_code = ' else '.join(mapping_checks) if mapping_checks else '/* no yaml_mapping */'

    # Generate yaml_flags handling from schema
    yaml_flags = schema.get('yaml_flags', {})
    flag_checks = []
    for flag_name, flag_value in yaml_flags.items():
        flag_checks.append(
            f'                if (strcmp(prop_key, "{flag_name}") == 0)\n'
            f'                    uniforms.flags = (uniforms.flags & ~{flag_value:#04x}) | ((strcmp(val, "true") == 0) ? {flag_value:#04x} : 0);'
        )
    flags_code = '\n                else '.join(flag_checks) if flag_checks else '/* no yaml_flags */'

    return f'''// Auto-generated from {name}.yaml - DO NOT EDIT
// YAML parser factory for {name} complex primitive

#include <yetty/{name}/{name}-gen.h>
#include <yetty/ypaint-yaml/ypaint-yaml.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/ytrace.h>
#include <yaml.h>
#include <stdlib.h>
#include <string.h>

#define {NAME}_MAX_FUNCTIONS 8

static int {name}_hex_digit(char c)
{{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}}

static uint32_t {name}_parse_color(const char *s)
{{
    if (!s) return 0;
    if (*s == '#') s++;
    size_t len = 0;
    const char *p = s;
    while (*p && {name}_hex_digit(*p) >= 0) {{ len++; p++; }}
    if (len != 6 && len != 8) return 0;
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++)
        v = (v << 4) | (uint32_t){name}_hex_digit(s[i]);
    return (len == 6) ? (0xFF000000 | v) : v;
}}

static const uint32_t {NAME}_COLOR_PALETTE[8] = {{
    0xFFFF6B6B, 0xFF4ECDC4, 0xFFFFE66D, 0xFF95E1D3,
    0xFFF38181, 0xFFAA96DA, 0xFF72D6C9, 0xFFFCBF49,
}};

static struct yetty_ycore_void_result
{name}_yaml_factory(struct yetty_ypaint_core_buffer *buffer,
                    yaml_parser_t *yaml_parser,
                    const char *primitive_type_name)
{{
    (void)primitive_type_name;

    struct yetty_{name}_uniforms uniforms = {{0}};
{defaults_code}

    char exprs[{NAME}_MAX_FUNCTIONS][256] = {{{{0}}}};
    int func_count = 0;

    char prop_key[64] = {{0}};
    int expect_value = 0;
    int in_array = 0, in_functions = 0, in_func_item = 0;
    int array_idx = 0;
    float array_vals[8] = {{0}};

    yaml_event_t event;
    int depth = 0, done = 0;

    while (!done) {{
        if (!yaml_parser_parse(yaml_parser, &event))
            return YETTY_ERR(yetty_ycore_void, "yaml parse error");

        switch (event.type) {{
        case YAML_MAPPING_START_EVENT:
            depth++;
            if (in_functions && !in_func_item) {{
                in_func_item = 1;
                expect_value = 0;
                if (func_count < {NAME}_MAX_FUNCTIONS)
                    uniforms.colors[func_count] = {NAME}_COLOR_PALETTE[func_count % 8];
            }}
            break;
        case YAML_MAPPING_END_EVENT:
            depth--;
            if (in_func_item) {{
                in_func_item = 0;
                if (func_count < {NAME}_MAX_FUNCTIONS)
                    func_count++;
            }}
            if (depth == 0) done = 1;
            break;
        case YAML_SEQUENCE_START_EVENT:
            if (strcmp(prop_key, "functions") == 0)
                in_functions = 1;
            else {{
                in_array = 1;
                array_idx = 0;
            }}
            break;
        case YAML_SEQUENCE_END_EVENT:
            if (in_functions) {{
                in_functions = 0;
            }} else if (in_array) {{
                {mapping_code}
                in_array = 0;
            }}
            expect_value = 0;
            break;
        case YAML_SCALAR_EVENT: {{
            const char *val = (const char *)event.data.scalar.value;
            if (in_array) {{
                if (array_idx < 8)
                    array_vals[array_idx++] = strtof(val, NULL);
            }} else if (in_func_item) {{
                if (!expect_value) {{
                    strncpy(prop_key, val, sizeof(prop_key) - 1);
                    expect_value = 1;
                }} else {{
                    if (strcmp(prop_key, "expr") == 0 && func_count < {NAME}_MAX_FUNCTIONS)
                        strncpy(exprs[func_count], val, 255);
                    else if (strcmp(prop_key, "color") == 0 && func_count < {NAME}_MAX_FUNCTIONS)
                        uniforms.colors[func_count] = {name}_parse_color(val);
                    expect_value = 0;
                }}
            }} else if (!expect_value) {{
                strncpy(prop_key, val, sizeof(prop_key) - 1);
                expect_value = 1;
            }} else {{
                {flags_code}
                expect_value = 0;
            }}
            break;
        }}
        default:
            break;
        }}
        yaml_event_delete(&event);
    }}

    uniforms.function_count = (uint32_t)func_count;

    uint32_t bc_buf[1024] = {{0}};
    size_t bc_count = 0;

    if (func_count > 0) {{
        char multi_expr[2048] = {{0}};
        size_t off = 0;
        for (int i = 0; i < func_count; i++) {{
            if (i > 0 && off < sizeof(multi_expr) - 2) {{
                multi_expr[off++] = ';';
                multi_expr[off++] = ' ';
            }}
            size_t len = strlen(exprs[i]);
            if (off + len < sizeof(multi_expr)) {{
                memcpy(multi_expr + off, exprs[i], len);
                off += len;
            }}
        }}

        struct yetty_yfsvm_program_result prog_res =
            yetty_yfsvm_compile_multi_expr(multi_expr, off);
        if (YETTY_IS_OK(prog_res))
            bc_count = yetty_yfsvm_program_serialize(&prog_res.value, bc_buf, 1024);
    }}

    struct yetty_{name}_buffers bufs = {{
        .bytecode = bc_buf,
        .bytecode_len = bc_count,
    }};

    size_t required = yetty_{name}_serialized_size(&uniforms, &bufs);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf)
        return YETTY_ERR(yetty_ycore_void, "malloc failed");

    struct yetty_ycore_size_result ser_res =
        yetty_{name}_serialize(&uniforms, &bufs, prim_buf, required);
    if (YETTY_IS_ERR(ser_res)) {{
        free(prim_buf);
        return YETTY_ERR(yetty_ycore_void, ser_res.error.msg);
    }}

    struct yetty_ypaint_id_result id_res =
        yetty_ypaint_core_buffer_add_prim(buffer, prim_buf, required);
    free(prim_buf);

    if (id_res.error)
        return YETTY_ERR(yetty_ycore_void, "add_prim failed");

    return YETTY_OK_VOID();
}}

struct yetty_ycore_void_result
yetty_{name}_register_yaml_factory(struct yetty_ypaint_yaml_parser *parser)
{{
    return yetty_ypaint_yaml_parser_register(parser, "{name}", {name}_yaml_factory);
}}
'''


def main():
    if len(sys.argv) != 2:
        print(f'Usage: {sys.argv[0]} <schema.yaml>', file=sys.stderr)
        sys.exit(1)

    schema_path = Path(sys.argv[1])
    schema = load_schema(schema_path)
    uniforms, buffers = calculate_layout(schema)

    name = schema['name']
    src_dir = schema_path.parent
    # Header goes to include/yetty/<module>/, source files stay in src/yetty/<module>/
    # Schema is at src/yetty/<module>/<module>.yaml
    # Include dir is at include/yetty/<module>/
    include_dir = src_dir.parent.parent.parent / 'include' / 'yetty' / name
    include_dir.mkdir(parents=True, exist_ok=True)

    header = generate_c_header(schema, uniforms, buffers)
    source = generate_c_source(schema, uniforms, buffers)
    wgsl = generate_wgsl_bindings(schema, uniforms, buffers)
    yaml_parser = generate_yaml_parser(schema, uniforms, buffers)

    (include_dir / f'{name}-gen.h').write_text(header + '\n')
    (src_dir / f'{name}-gen.c').write_text(source + '\n')
    (src_dir / f'{name}-gen.wgsl').write_text(wgsl + '\n')
    (src_dir / f'{name}-gen-yaml.c').write_text(yaml_parser + '\n')

    print(f'Generated:')
    print(f'  {include_dir / f"{name}-gen.h"}')
    print(f'  {src_dir / f"{name}-gen.c"}')
    print(f'  {src_dir / f"{name}-gen.wgsl"}')
    print(f'  {src_dir / f"{name}-gen-yaml.c"}')


if __name__ == '__main__':
    main()
