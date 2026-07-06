#!/usr/bin/env python3
"""
Composite code generator.

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


# Texture format -> (atlas_index, WGPU constant value, WGSL type, atlas binding name)
# WGPUTextureFormat enum values from <webgpu/webgpu.h>: R8Unorm=0x01, RGBA8Unorm=0x12.
TEXTURE_FORMATS = {
    'r8':    ('R8Unorm',    'WGPUTextureFormat_R8Unorm',    'texture_2d<f32>', 'atlas_r8'),
    'rgba8': ('RGBA8Unorm', 'WGPUTextureFormat_RGBA8Unorm', 'texture_2d<f32>', 'atlas_rgba8'),
}

# Texture format -> bytes per pixel. Used by create_instance to bound the
# pixel region (tex_w*tex_h*bpp) against the bytes the wire record actually
# carries, so an attacker-supplied dimension cannot drive an out-of-bounds
# upload into a GPU texture.
TEXTURE_FORMAT_BPP = {
    'r8':    1,
    'rgba8': 4,
}

# Upper bound on a single texture dimension read off the wire. Far above any
# real GPU maxTextureDimension2D, but small enough that tex_w*tex_h*bpp cannot
# overflow the 64-bit product used in the bounds check.
TEXTURE_MAX_DIM = 32768

SAMPLER_FILTERS = {
    'linear':  1,  # WGPUFilterMode_Linear
    'nearest': 0,  # WGPUFilterMode_Nearest
}


def calculate_layout(schema):
    """Calculate layout for uniforms, buffers, and textures.

    Returns (uniforms, buffers, textures). Each buffer carries a
    `diverted` flag — true when its bytes are routed into a texture and so
    should NOT be exposed as a storage-buffer binding (pipeline + binder
    skip it; wire format is unchanged). Each texture resolves its
    `pixels_buffer` / `width_uniform` / `height_uniform` references to wire
    offsets so the generated _create_instance can extract them in O(1).
    """
    uniforms = []
    buffers = []
    textures = []

    # Uniforms - go to GPU uniform buffer
    uniform_wire_offset = {}  # name -> first word offset in wire payload
    cursor = 0
    for u in schema.get('uniforms', []):
        count = u.get('count', 1)
        t = TYPES[u['type']]
        uniform_wire_offset[u['name']] = cursor
        uniforms.append({
            'name': u['name'],
            'type': u['type'],
            'c_type': t[0],
            'wgsl_type': t[1],
            'render_type': t[3],
            'count': count,
            'default': u.get('default'),
            'wire_offset': cursor,
        })
        cursor += count

    # Buffers - some go to the GPU storage buffer, some are diverted to textures
    for b in schema.get('buffers', []):
        buffers.append({
            'name': b['name'],
            # RS binding name. The FIRST storage buffer keeps the literal
            # "buffer" for backwards compatibility (existing shaders);
            # later ones default to "<name>_buffer" unless the yaml sets
            # rs_name explicitly.
            'rs_name': b.get('rs_name'),
            'element_type': b['element_type'],
            # array: true — variable-count list of element buffers,
            # count-prefixed on the wire; forces the MERGED storage layout
            # (one RS buffer spanning the whole post-uniform payload).
            'array': b.get('array', False),
            'diverted': False,
        })

    # Textures - declared in schema, resolved here against buffers + uniforms
    schema_textures = schema.get('textures', []) or []
    buffers_by_name = {b['name']: b for b in buffers}
    for t in schema_textures:
        fmt_key = t['format']
        if fmt_key not in TEXTURE_FORMATS:
            raise ValueError(f"unknown texture format '{fmt_key}' in '{t['name']}'")
        fmt_short, fmt_const, wgsl_type, atlas_name = TEXTURE_FORMATS[fmt_key]
        sampler_key = t.get('sampler', 'linear')
        if sampler_key not in SAMPLER_FILTERS:
            raise ValueError(f"unknown sampler '{sampler_key}' in texture '{t['name']}'")
        sampler_filter = SAMPLER_FILTERS[sampler_key]

        pixels_buffer_name = t.get('pixels_buffer')
        if pixels_buffer_name is not None:
            if pixels_buffer_name not in buffers_by_name:
                raise ValueError(
                    f"texture '{t['name']}': pixels_buffer '{pixels_buffer_name}' "
                    f"is not declared in buffers:")
            buffers_by_name[pixels_buffer_name]['diverted'] = True

        width_uniform = t.get('width_uniform')
        height_uniform = t.get('height_uniform')
        if width_uniform is not None and width_uniform not in uniform_wire_offset:
            raise ValueError(
                f"texture '{t['name']}': width_uniform '{width_uniform}' is not declared")
        if height_uniform is not None and height_uniform not in uniform_wire_offset:
            raise ValueError(
                f"texture '{t['name']}': height_uniform '{height_uniform}' is not declared")

        textures.append({
            'name': t['name'],
            'format': fmt_key,
            'format_const': fmt_const,
            'format_bpp': TEXTURE_FORMAT_BPP[fmt_key],
            'wgsl_type': wgsl_type,
            'atlas_name': atlas_name,
            'sampler_filter': sampler_filter,
            'pixels_buffer': pixels_buffer_name,
            'width_uniform': width_uniform,
            'height_uniform': height_uniform,
            'width_wire_offset': uniform_wire_offset.get(width_uniform),
            'height_wire_offset': uniform_wire_offset.get(height_uniform),
        })

    return uniforms, buffers, textures


def yaml_factory_mode(schema):
    """Return the yaml-factory mode: 'default' (current yfsvm-style) or 'none' (skip)."""
    mode = schema.get('yaml_factory', 'default')
    if mode in (None, False, 'none', 'skip'):
        return 'none'
    if mode == 'manual':
        # Registration decl only — a hand-written <name>-yaml.c implements it.
        return 'manual'
    return 'default'


def external_uniforms(schema):
    """Server-side uniform slots: present in the RS (and WGSL accessors) but
    NOT on the wire — some hand-written companion (e.g. yplot-time.c) writes
    them directly into the instance RS."""
    return schema.get('external_uniforms', []) or []


def merged_storage(buffers):
    """True when any buffer is array-valued: the whole post-uniform payload
    becomes ONE self-describing RS storage buffer the shader walks."""
    arrays = [b for b in buffers if b.get('array')]
    if not arrays:
        return False
    if len(arrays) > 1 or not buffers[-1].get('array'):
        raise ValueError('array: true is only supported on the LAST buffer, once')
    return True


def update_mode(schema):
    """'hooks' (generic hook dispatch), 'extern' (hand-written
    yetty_<name>_instance_update in a companion TU), or None."""
    if hooks_enabled(schema):
        return 'hooks'
    if schema.get('update') == 'extern':
        return 'extern'
    return None


def hooks_enabled(schema):
    """True when the schema opts into the prim-specific hook surface
    the factory emits. Hooks are extern decls the generated factory
    calls at fixed points; a hand-written <name>-hooks.c implements
    them. Keeps CPU/GPU sync (uniform names, types, offsets, buffer
    layouts, texture descriptors) in the generated factory while
    letting stateful prims (decoders, per-frame texture writes, …)
    plug in without forking the generator output.

    When enabled the generator emits, at the top of <name>-gen.c:

      extern struct yetty_ycore_void_result <name>_hook_instance_create(
          struct yetty_ydraw_composite *instance,
          const void *buffer_data, size_t size);
      extern void <name>_hook_instance_destroy(
          struct yetty_ydraw_composite *instance);
      extern struct yetty_ycore_void_result <name>_hook_instance_update(
          struct yetty_ydraw_composite *instance,
          const void *payload, size_t size);
      extern struct yetty_ycore_void_result <name>_hook_instance_render_pre(
          struct yetty_ydraw_composite *instance,
          struct yetty_ydraw_target *target, float x, float y);

    Insertion points:
      create_instance — after RS clone + wire wiring, BEFORE binder
                        ops->submit (so the hook can resize textures
                        before atlas pack). On error returned by the
                        hook, the standard rollback runs.
      destroy_instance — BEFORE the binder/RS/buffer/instance frees.
      render          — AFTER all uniform writes from the wire, BEFORE
                        binder->update (so the hook can write the
                        texture and set dirty=1 with the freshly
                        decoded frame).
      update_instance — the factory's vtable entry is set to a wrapper
                        that forwards to the hook (one source of truth
                        for the CMD_UPDATE schema lives in <name>-hooks.c).
    """
    return bool(schema.get('hooks', False))


def generate_c_header(schema, uniforms, buffers, textures, yaml_mode):
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

    element_ctype = {'u32': 'uint32_t', 'f32': 'float'}
    array_structs = []
    buf_struct_fields = []
    for b in buffers:
        if b.get('array'):
            ctype = element_ctype[b['element_type']]
            array_structs.append(f'''/* One `{b["name"]}` entry. The wire encoding repeats [len][samples...]
 * per entry, prefixed by a single [{b["name"]}_count]. */
struct yetty_{name}_{b["name"]}_buffer {{
    const {ctype} *samples;
    size_t count; /* in {ctype} elements */
}};
''')
            buf_struct_fields.append(
                f'    const struct yetty_{name}_{b["name"]}_buffer *{b["name"]};')
            buf_struct_fields.append(f'    size_t {b["name"]}_count;')
        else:
            buf_struct_fields.append(f'    const uint32_t *{b["name"]};')
            buf_struct_fields.append(f'    size_t {b["name"]}_len;')
    buf_struct_fields_str = '\n'.join(buf_struct_fields)
    array_structs_str = ''.join(array_structs)

    uniforms_word_count = sum(u['count'] for u in uniforms)
    ext_defines = ''
    externals = external_uniforms(schema)
    if externals:
        # RS slot indices of the server-side uniforms: wire uniforms, then
        # zoom(6) + viewport(2), then one region slot per texture, then the
        # externals in declaration order.
        base_slot = uniforms_word_count + 8 + len(textures)
        for ei, ext in enumerate(externals):
            ext_defines += (f'\n/* RS slot of the server-side `{ext["name"]}` uniform '
                            f'(not on the wire). */\n'
                            f'#define YETTY_{NAME}_UNIFORM_{ext["name"].upper()}_SLOT '
                            f'{base_slot + ei}u')
        ext_defines += '\n'

    mode = update_mode(schema)
    extern_api = ''
    if mode == 'extern':
        extern_api += f'''
/* CMD_UPDATE entry point — implemented by hand in a companion TU (the
 * decode of target_field/body is {name}-specific). Installed as the
 * per-instance ops->update at create time. */
struct yetty_ycore_void_result yetty_{name}_instance_update(
    struct yetty_ydraw_composite *instance, uint32_t target_field,
    const void *body, size_t body_size);
'''
    if schema.get('lifecycle_extern'):
        extern_api += f'''
/* Lifecycle callouts — implemented by hand in a companion TU. `created`
 * runs after a successful create (failure is non-fatal: the figure still
 * renders); `destroying` runs FIRST in instance destroy, before any
 * teardown. */
struct yetty_ycore_void_result yetty_{name}_instance_created(
    struct yetty_ydraw_composite *instance);
void yetty_{name}_instance_destroying(struct yetty_ydraw_composite *instance);
'''

    yaml_section = '' if yaml_mode == 'none' else f'''
//=============================================================================
// YAML parser registration
//=============================================================================

struct yetty_ydraw_yaml_parser;
struct yetty_ycore_void_result yetty_{name}_register_yaml_factory(
    struct yetty_ydraw_yaml_parser *parser);
'''

    return f'''// Auto-generated from {name}.yaml - DO NOT EDIT
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {{
#endif

/* Forward-declared so this header stays GPU-less and can be included by
 * client-side wire emitters that don't link Dawn. The full types live in
 * yetty/ydraw-factory/composite-factory.h (server side). */
struct yetty_ydraw_concrete_factory;
struct yetty_ydraw_composite;

#define YETTY_{NAME}_TYPE_ID 0x{type_id:08x}u

/* Number of u32 words the uniforms occupy in the wire (and as a prefix in
 * the payload before the storage region). */
#define YETTY_{NAME}_UNIFORMS_WORDS {uniforms_word_count}u
{ext_defines}
// Uniforms struct (goes to GPU uniform buffer)
struct yetty_{name}_uniforms {{
{struct_fields_str}
}};

{array_structs_str}// Buffers struct (goes to GPU storage buffer)
struct yetty_{name}_buffers {{
{buf_struct_fields_str}
}};
{extern_api}

//=============================================================================
// Serialization API
//=============================================================================

size_t yetty_{name}_uniforms_serialized_size(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers);

struct yetty_ycore_size_result yetty_{name}_uniforms_serialize(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers,
    uint8_t *out, size_t out_capacity);

//=============================================================================
// Factory API (creates binder with pre-compiled pipeline)
//=============================================================================

struct yetty_ydraw_concrete_factory *yetty_{name}_factory_create(void);
void yetty_{name}_factory_destroy(struct yetty_ydraw_concrete_factory *factory);
{yaml_section}
#ifdef __cplusplus
}}
#endif
'''


def generate_c_wire_source(schema, uniforms, buffers):
    """Emit *-gen-wire.c: pure wire-format serialize helpers (no GPU).
    Lives in yetty_{name}_core so client tools / riscv64 builds can include it
    without dragging in Dawn / WebGPU.
    """
    name = schema['name']
    NAME = name.upper()

    uniforms_word_count = sum(u['count'] for u in uniforms)
    buffer_len_fields = len(buffers)

    if merged_storage(buffers):
        return generate_c_wire_source_merged(schema, uniforms, buffers)

    buf_len_sum_parts = [f'buffers->{b["name"]}_len' for b in buffers]
    buf_len_sum = ' + '.join(buf_len_sum_parts) if buf_len_sum_parts else '0'

    buf_len_writes = '\n'.join(
        [f'    *p++ = (uint32_t)buffers->{b["name"]}_len;' for b in buffers])

    buf_copies = '\n'.join([f'''    if (buffers->{b["name"]} && buffers->{b["name"]}_len > 0)
        memcpy(p, buffers->{b["name"]}, buffers->{b["name"]}_len * sizeof(uint32_t));
    p += buffers->{b["name"]}_len;''' for b in buffers])

    return f'''// Auto-generated from {name}.yaml - DO NOT EDIT
//
// Wire-format helpers for the {name} composite. Pure CPU code: packs
// caller-supplied uniforms + buffers into the on-the-wire byte layout. Lives
// in yetty_{name}_core (no Dawn, no WebGPU, safe for riscv64 / wasm / any
// cross-target without a GPU).

#include <yetty/{name}/{name}-gen.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <string.h>

size_t yetty_{name}_uniforms_serialized_size(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers)
{{
    (void)uniforms;
    // Wire format: [type_id][payload_size][uniforms...][buffer_lens...][buffer_data...]
    size_t total_buf_words = {buf_len_sum};
    return (2 + {uniforms_word_count} + {buffer_len_fields} + total_buf_words) * sizeof(uint32_t);
}}

struct yetty_ycore_size_result yetty_{name}_uniforms_serialize(
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
'''


def generate_c_wire_source_merged(schema, uniforms, buffers):
    """Wire helpers for the MERGED storage layout (one array-valued buffer):
      [type_id][payload_size][uniforms...]
      [len_0][buf_0...]... (fixed buffers, in declaration order)
      [<array>_count] then per entry: [len_i][samples_i...]
    Everything after the uniforms is handed verbatim to the GPU storage
    buffer; the shader walks the same header at render time."""
    name = schema['name']
    NAME = name.upper()
    fixed = [b for b in buffers if not b.get('array')]
    arr = next(b for b in buffers if b.get('array'))
    elem_ctype = {'u32': 'uint32_t', 'f32': 'float'}[arr['element_type']]

    fixed_words = '\n'.join(
        f'    w += 1 /* {b["name"]}_len */ + buffers->{b["name"]}_len;' for b in fixed)
    fixed_writes = '\n'.join(f'''    *p++ = (uint32_t)buffers->{b["name"]}_len;
    if (buffers->{b["name"]} && buffers->{b["name"]}_len > 0) {{
        memcpy(p, buffers->{b["name"]}, buffers->{b["name"]}_len * sizeof(uint32_t));
        p += buffers->{b["name"]}_len;
    }}''' for b in fixed)

    return f'''// Auto-generated from {name}.yaml - DO NOT EDIT
//
// Wire-format helpers for the {name} composite. Pure CPU code: packs
// caller-supplied uniforms + buffers into the on-the-wire byte layout. Lives
// in yetty_{name}_core (no Dawn, no WebGPU, safe for riscv64 / wasm / any
// cross-target without a GPU).
//
// Wire layout (u32 words):
//   [0]              type_id
//   [1]              payload_size (bytes after this header)
//   [2 ..]           uniforms (YETTY_{NAME}_UNIFORMS_WORDS words)
//   --- storage payload (handed verbatim to the GPU storage_buffer) ---
//   per fixed buffer: [len][data...]
//   [{arr["name"]}_count], then per entry: [len_i][samples_i...]

#include <yetty/{name}/{name}-gen.h>
#include <yetty/ycore/result.h>

#include <stdint.h>
#include <string.h>

/* Total words the storage payload occupies (everything after the uniforms,
 * i.e. what gets handed to the GPU storage_buffer). */
static size_t storage_words(const struct yetty_{name}_buffers *buffers)
{{
    size_t w = 1 /* {arr["name"]}_count */;
{fixed_words}
    for (size_t i = 0; i < buffers->{arr["name"]}_count; i++) {{
        w += 1 /* len_i */ + buffers->{arr["name"]}[i].count;
    }}
    return w;
}}

size_t yetty_{name}_uniforms_serialized_size(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers)
{{
    (void)uniforms;
    size_t total_words = 2 /* type_id + payload_size */
                         + YETTY_{NAME}_UNIFORMS_WORDS + storage_words(buffers);
    return total_words * sizeof(uint32_t);
}}

struct yetty_ycore_size_result yetty_{name}_uniforms_serialize(
    const struct yetty_{name}_uniforms *uniforms,
    const struct yetty_{name}_buffers *buffers,
    uint8_t *out, size_t out_capacity)
{{
    if (!uniforms || !buffers) {{
        return YETTY_ERR(yetty_ycore_size, "null argument");
    }}
    if (!out) {{
        return YETTY_ERR(yetty_ycore_size, "out is NULL");
    }}

    size_t required = yetty_{name}_uniforms_serialized_size(uniforms, buffers);
    if (out_capacity < required) {{
        return YETTY_ERR(yetty_ycore_size, "buffer too small");
    }}

    uint32_t *p = (uint32_t *)out;
    *p++ = YETTY_{NAME}_TYPE_ID;
    *p++ = (uint32_t)(required - 2 * sizeof(uint32_t));

    /* Uniforms: copy as raw u32 words — all 4-byte scalars, packs without
     * padding, so a memcpy reproduces the wire layout exactly. */
    memcpy(p, uniforms, YETTY_{NAME}_UNIFORMS_WORDS * sizeof(uint32_t));
    p += YETTY_{NAME}_UNIFORMS_WORDS;

    /* Storage payload: fixed buffers first, then the variable list. */
{fixed_writes}

    *p++ = (uint32_t)buffers->{arr["name"]}_count;
    for (size_t i = 0; i < buffers->{arr["name"]}_count; i++) {{
        const struct yetty_{name}_{arr["name"]}_buffer *entry = &buffers->{arr["name"]}[i];
        *p++ = (uint32_t)entry->count;
        if (entry->samples && entry->count > 0) {{
            memcpy(p, entry->samples, entry->count * sizeof({elem_ctype}));
            p += entry->count;
        }}
    }}

    return YETTY_OK(yetty_ycore_size, required);
}}
'''


def generate_c_source(schema, uniforms, buffers, textures):
    name = schema['name']
    NAME = name.upper()
    libraries = schema.get('libraries', [])

    # Calculate sizes
    uniforms_word_count = sum(u['count'] for u in uniforms)
    buffer_len_fields = len(buffers)

    # Buffers exposed as a storage-buffer binding (not diverted into textures).
    # Diverted buffers stay in the wire format but are routed to texture data
    # in _create_instance — they don't appear in rs->buffers[].
    storage_buffers = [b for b in buffers if not b.get('diverted')]
    has_textures = len(textures) > 0
    needs_webgpu_h = has_textures  # for WGPUTextureFormat_* constants

    # compiler.h (bytecode API) + shader-rs.h (the library's shared
    # GPU resource set consumed as a binder child).
    lib_includes = '\n'.join([f'#include <yetty/{lib}/compiler.h>\n#include <yetty/{lib}/shader-rs.h>' for lib in libraries])

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

    # Per-texture region uniform — vec4(u0, v0, u1, v1) of the atlas slice.
    # The binder resolves atlas position at finalize time and writes the
    # vec4 here BEFORE uploading the uniform buffer. Default (0,0,1,1)
    # keeps the shader sane for primitives whose textures haven't been
    # packed yet (e.g. during initial pipeline compile with placeholder
    # 1x1 templates).
    for t in textures:
        uniform_setup.append(f'''    strncpy(rs->uniforms[{uniform_idx}].name, "{t["name"]}_region", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{uniform_idx}].type = YETTY_YRENDER_UNIFORM_VEC4;
    rs->uniforms[{uniform_idx}].vec4[0] = 0.0f;
    rs->uniforms[{uniform_idx}].vec4[1] = 0.0f;
    rs->uniforms[{uniform_idx}].vec4[2] = 1.0f;
    rs->uniforms[{uniform_idx}].vec4[3] = 1.0f;''')
        uniform_idx += 1

    # Server-side (external) uniforms — RS slots the wire never carries;
    # a hand-written companion TU writes them into the instance RS (see the
    # YETTY_<NAME>_UNIFORM_<EXT>_SLOT define in the generated header).
    for ext in external_uniforms(schema):
        ext_render_type = 'YETTY_YRENDER_UNIFORM_F32' if ext['type'] == 'f32' else 'YETTY_YRENDER_UNIFORM_U32'
        ext_zero = 'f32 = 0.0f' if ext['type'] == 'f32' else 'u32 = 0'
        uniform_setup.append(f'''    /* `{ext["name"]}` — server-side, not on the wire. */
    strncpy(rs->uniforms[{uniform_idx}].name, "{ext["name"]}", YETTY_YRENDER_NAME_MAX - 1);
    rs->uniforms[{uniform_idx}].type = {ext_render_type};
    rs->uniforms[{uniform_idx}].{ext_zero};''')
        uniform_idx += 1

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
                    uniform_update.append(f'    rs->uniforms[{uniform_idx}].f32 = *(float *)&payload[{wire_offset + i}]; /* {u["name"]}_{i} */')
                else:
                    uniform_update.append(f'    rs->uniforms[{uniform_idx}].u32 = payload[{wire_offset + i}]; /* {u["name"]}_{i} */')
                uniform_idx += 1
            wire_offset += u['count']
        else:
            if u['type'] == 'f32':
                uniform_update.append(f'    rs->uniforms[{uniform_idx}].f32 = *(float *)&payload[{wire_offset}]; /* {u["name"]} */')
            else:
                uniform_update.append(f'    rs->uniforms[{uniform_idx}].u32 = payload[{wire_offset}]; /* {u["name"]} */')
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

    # ------------------------------------------------------------------
    # Storage-buffer setup (template_rs / per-instance RS) -- only for
    # buffers NOT diverted into textures. For yplot this is a single
    # buffer; the current generator named it "buffer" generically so we
    # keep the literal name for backwards compatibility.
    # ------------------------------------------------------------------
    def storage_rs_name(storage_idx, buf):
        if storage_idx == 0:
            return buf.get('rs_name') or 'buffer'
        return buf.get('rs_name') or f"{buf['name']}_buffer"

    merged = merged_storage(buffers)
    if merged and textures:
        raise ValueError('merged storage (array buffer) + textures is unsupported')
    if merged:
        # One RS buffer spans the whole post-uniform payload; the shader
        # walks the self-describing layout, so no per-buffer wiring exists.
        buffer_setup_str = '''    // Single merged storage buffer — the shader walks the
    // self-describing [len][data]... layout at runtime.
    rs->buffer_count = 1;
    strncpy(rs->buffers[0].name, "buffer", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->buffers[0].readonly = 1;'''
    elif len(storage_buffers) == 0:
        buffer_setup_str = '    // No storage buffers (all buffers diverted to textures)\n    rs->buffer_count = 0;'
    else:
        setup_lines = ['    // Setup storage buffers for wire buffer data',
                       f'    rs->buffer_count = {len(storage_buffers)};']
        for storage_idx, buf in enumerate(storage_buffers):
            setup_lines.append(
                f'    strncpy(rs->buffers[{storage_idx}].name, "{storage_rs_name(storage_idx, buf)}", YETTY_YRENDER_NAME_MAX - 1);\n'
                f'    strncpy(rs->buffers[{storage_idx}].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);\n'
                f'    rs->buffers[{storage_idx}].readonly = 1;')
        buffer_setup_str = '\n'.join(setup_lines)

    # ------------------------------------------------------------------
    # Texture setup in the template -- placeholder dimensions (1x1, NULL
    # data) so pipeline_create includes atlas bindings. The binder skips
    # textures with width==0/height==0 in `collect`, so the placeholders
    # MUST be > 0. data=NULL is safe: the upload path skips on NULL.
    # Per-instance code below overwrites width/height/data with real
    # values BEFORE binder->submit.
    # ------------------------------------------------------------------
    texture_setup_lines = []
    for ti, t in enumerate(textures):
        texture_setup_lines.append(f'''    /* Texture: {t["name"]} (format={t["format"]}, sampler={'linear' if t["sampler_filter"] == 1 else 'nearest'}) */
    strncpy(rs->textures[{ti}].name, "{t["name"]}", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(rs->textures[{ti}].wgsl_type, "{t["wgsl_type"]}", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    rs->textures[{ti}].format = {t["format_const"]};
    rs->textures[{ti}].sampler_filter = {t["sampler_filter"]};
    rs->textures[{ti}].width = 1;   /* placeholder for pipeline compile; overwritten per-instance */
    rs->textures[{ti}].height = 1;
    rs->textures[{ti}].data = NULL;''')
    if textures:
        texture_setup_lines.append(f'    rs->texture_count = {len(textures)};')
    texture_setup_str = '\n'.join(texture_setup_lines)
    # Compose buffer + texture setup as a single block so that empty
    # halves don't introduce stray blank lines (preserves bit-identical
    # output for primitives without textures).
    if texture_setup_str:
        rs_resources_setup = buffer_setup_str + '\n\n' + texture_setup_str
    else:
        rs_resources_setup = buffer_setup_str

    # ------------------------------------------------------------------
    # Per-instance wire-format wiring for storage buffer + textures
    # ------------------------------------------------------------------
    # Storage-buffer wiring: existing single-buffer code path (yplot).
    if merged:
        instance_buffer_wiring = f'''        uint32_t payload_bytes = data[1];
        const uint32_t *storage = payload + {uniforms_word_count};
        size_t storage_size = (size_t)payload_bytes - {uniforms_word_count}u * sizeof(uint32_t);
        instance->resource_set->buffers[0].data = (uint8_t *)(uintptr_t)storage;
        instance->resource_set->buffers[0].size = storage_size;
        instance->resource_set->buffers[0].dirty = 1;'''
        render_buffer_wiring = f'''    /* Merged storage region: everything after the uniforms, handed
     * verbatim to the storage buffer (the shader walks its header).
     * Deliberately NOT marked dirty here — the initial upload happens at
     * create, and incremental sample updates write directly to GPU via
     * the binder's write_buffer_chunk op; a per-frame dirty would
     * re-upload the whole region every frame. */
    uint32_t payload_bytes = data[1];
    const uint32_t *storage = payload + {uniforms_word_count};
    size_t storage_size = (size_t)payload_bytes - {uniforms_word_count}u * sizeof(uint32_t);
    rs->buffers[0].data = (uint8_t *)(uintptr_t)storage;
    rs->buffers[0].size = storage_size;'''
    elif len(storage_buffers) == 0:
        instance_buffer_wiring = ''
        render_buffer_wiring = ''
    elif len(storage_buffers) == 1:
        # The first buffer in declaration order maps to rs->buffers[0].
        # If buffers[0] is diverted (texture-only), we'd need to skip it
        # — supported below by the wire-offset computation.
        first_storage_idx = next(i for i, b in enumerate(buffers) if not b.get('diverted'))
        # Wire offset of buffer N's data = buffer_data_offset + sum(payload[uwc..uwc+N-1])
        if first_storage_idx == 0:
            data_off_expr = f'payload + {buffer_data_offset}'
            len_expr = f'payload[{uniforms_word_count}]'
        else:
            sum_terms = ' + '.join(f'payload[{uniforms_word_count + k}]' for k in range(first_storage_idx))
            data_off_expr = f'payload + {buffer_data_offset} + ({sum_terms})'
            len_expr = f'payload[{uniforms_word_count + first_storage_idx}]'
        instance_buffer_wiring = f'''        size_t buffer_words = {len_expr};
        const uint32_t *buffer_payload = {data_off_expr};
        instance->resource_set->buffers[0].data = (uint8_t *)buffer_payload;
        instance->resource_set->buffers[0].size = buffer_words * sizeof(uint32_t);
        instance->resource_set->buffers[0].dirty = 1;'''
        render_buffer_wiring = f'''    // Get buffer data (after uniforms and length fields)
    const uint32_t *buffer_data = {data_off_expr};
    size_t buffer_words = {len_expr};  // first buffer length

    // Update storage buffer
    rs->buffers[0].data = (uint8_t *)buffer_data;
    rs->buffers[0].size = buffer_words * sizeof(uint32_t);
    rs->buffers[0].dirty = 1;'''
    else:
        # Several storage buffers: walk the wire once, cumulatively — each
        # buffer's payload starts where the previous one ends. Length words
        # sit at payload[uwc + wire_index] in declaration order (diverted
        # buffers included in the walk, excluded from rs->buffers[]).
        walk_lines = []
        for wire_idx, buf in enumerate(buffers):
            walk_lines.append(
                f"size_t {buf['name']}_words = payload[{uniforms_word_count + wire_idx}];")
        prev = None
        for buf in buffers:
            if prev is None:
                walk_lines.append(
                    f"const uint32_t *{buf['name']}_payload = payload + {buffer_data_offset};")
            else:
                walk_lines.append(
                    f"const uint32_t *{buf['name']}_payload = "
                    f"{prev['name']}_payload + {prev['name']}_words;")
            prev = buf
        wire_blocks_instance = []
        wire_blocks_render = []
        for storage_idx, buf in enumerate(storage_buffers):
            for target_prefix, sink in (('instance->resource_set', wire_blocks_instance),
                                        ('rs', wire_blocks_render)):
                sink.append(
                    f"{target_prefix}->buffers[{storage_idx}].data = (uint8_t *){buf['name']}_payload;\n"
                    f"{target_prefix}->buffers[{storage_idx}].size = {buf['name']}_words * sizeof(uint32_t);\n"
                    f"{target_prefix}->buffers[{storage_idx}].dirty = 1;"
                    .replace('rs->buffers', 'rs->buffers'))
        def indent_block(lines, pad):
            return '\n'.join(pad + line for chunk in lines for line in chunk.split('\n'))
        instance_buffer_wiring = indent_block(walk_lines + wire_blocks_instance, ' ' * 8)
        render_buffer_wiring = ('    // Wire each storage buffer to its slice of the payload\n'
                                + indent_block(walk_lines + wire_blocks_render, ' ' * 4))

    # Texture wiring: for each texture with pixels_buffer, compute wire
    # offsets at codegen time and emit a block setting data/width/height.
    def texture_wire_block(t, ti, indent):
        if t['pixels_buffer'] is None:
            return ''  # texture with no pixel source — caller manages it
        buf_idx = next(i for i, b in enumerate(buffers) if b['name'] == t['pixels_buffer'])
        if buf_idx == 0:
            data_expr = f'payload + {buffer_data_offset}'
        else:
            sum_terms = ' + '.join(f'payload[{uniforms_word_count + k}]' for k in range(buf_idx))
            data_expr = f'payload + {buffer_data_offset} + ({sum_terms})'
        w_off = t['width_wire_offset']
        h_off = t['height_wire_offset']
        ind = ' ' * indent
        return f'''{ind}/* Texture '{t["name"]}' — pixels diverted from buffer '{t["pixels_buffer"]}'. */
{ind}{{
{ind}    const uint32_t *pixels_data = {data_expr};
{ind}    uint32_t tex_w = payload[{w_off}];
{ind}    uint32_t tex_h = payload[{h_off}];
{ind}    instance->resource_set->textures[{ti}].data = (uint8_t *)pixels_data;
{ind}    instance->resource_set->textures[{ti}].width = tex_w;
{ind}    instance->resource_set->textures[{ti}].height = tex_h;
{ind}    instance->resource_set->textures[{ti}].dirty = 1;
{ind}}}'''

    instance_texture_wiring = '\n'.join(
        texture_wire_block(t, ti, 8) for ti, t in enumerate(textures))
    if not instance_texture_wiring:
        instance_texture_wiring = ''

    # Same texture wiring for the per-frame render path. Uses `rs` instead
    # of `instance->resource_set` (the variable name in _instance_render).
    def texture_render_block(t, ti, indent):
        if t['pixels_buffer'] is None:
            return ''
        buf_idx = next(i for i, b in enumerate(buffers) if b['name'] == t['pixels_buffer'])
        if buf_idx == 0:
            data_expr = f'payload + {buffer_data_offset}'
        else:
            sum_terms = ' + '.join(f'payload[{uniforms_word_count + k}]' for k in range(buf_idx))
            data_expr = f'payload + {buffer_data_offset} + ({sum_terms})'
        w_off = t['width_wire_offset']
        h_off = t['height_wire_offset']
        ind = ' ' * indent
        return f'''{ind}/* Texture '{t["name"]}' — keep dimensions/data in sync with wire. */
{ind}{{
{ind}    const uint32_t *pixels_data = {data_expr};
{ind}    uint32_t tex_w = payload[{w_off}];
{ind}    uint32_t tex_h = payload[{h_off}];
{ind}    rs->textures[{ti}].data = (uint8_t *)pixels_data;
{ind}    rs->textures[{ti}].width = tex_w;
{ind}    rs->textures[{ti}].height = tex_h;
{ind}    rs->textures[{ti}].dirty = 1;
{ind}}}'''

    render_texture_wiring = '\n'.join(
        texture_render_block(t, ti, 4) for ti, t in enumerate(textures))
    if not render_texture_wiring:
        render_texture_wiring = ''
    # Compose render-side resource wiring as one block to avoid stray
    # blank lines when textures are absent (yplot path).
    if render_texture_wiring:
        render_resources_wiring = render_buffer_wiring + '\n\n' + render_texture_wiring
    else:
        render_resources_wiring = render_buffer_wiring
    if instance_buffer_wiring and instance_texture_wiring:
        instance_inner = instance_buffer_wiring + '\n\n' + instance_texture_wiring
    else:
        instance_inner = instance_buffer_wiring or instance_texture_wiring
    if instance_inner:
        # Comment matches what's in the wiring: storage-buffer-only (yplot
        # path) keeps the original wording so existing yplot output stays
        # bit-identical after regen; texture-bearing primitives get the
        # broader description.
        if textures:
            wiring_comment = '''    /* Wire the per-instance RS to this instance's payload. Storage
     * buffers (if any) point into the wire bytes; textures whose
     * pixels_buffer was diverted have their data + dimensions populated
     * here BEFORE binder->submit so the first finalize sees real
     * dimensions and atlas-packs accordingly. */'''
        else:
            wiring_comment = '''    /* Point the storage buffer descriptor at this instance's bytecode now,
     * so the binder's first finalize allocates a GPU buffer of the right
     * size and queueWriteBuffers the data. */'''
        instance_resources_wiring = f'''{wiring_comment}
    {{
        const uint32_t *data = (const uint32_t *)instance->buffer_data;
        const uint32_t *payload = data + 2;
{instance_inner}
    }}'''
    else:
        instance_resources_wiring = ''

    # Record-bounds validation, emitted at the very top of create_instance
    # (before any allocation, so a rejection is a plain early return). Two
    # independent checks guard the binder against uploading heap past
    # buffer_data (out-of-bounds read / GPU infoleak):
    #
    #   1. Every declared buffer's length word (payload[uwc+k]) is
    #      attacker-controlled and drives a GPU upload of that many words. The
    #      record must actually carry the sum of all buffer payloads.
    #   2. Each texture whose pixels are diverted from a buffer reads
    #      tex_w*tex_h*bpp bytes (which need NOT equal the diverted buffer's
    #      declared length), so its pixel region is bounded separately.
    #
    # All arithmetic is 64-bit so a crafted length/dimension cannot wrap.
    min_header_words = 2 + buffer_data_offset

    def buffer_validate_block():
        if not buffers:
            return ''
        # Bound the record against its own declared payload_size rather than a
        # sum of per-buffer length words at fixed offsets. The fixed-offset sum
        # is wrong for composites whose buffers are NOT contiguous: yplot
        # interleaves a variable-length bytecode payload between its
        # buffer-length words, so word[uniforms+1] is a bytecode instruction,
        # not a length — the old check read garbage and falsely rejected every
        # valid yplot record ("buffers exceed record"). payload_size (header
        # word[1]) is the authoritative byte count following the 8-byte header
        # and is written by every composite serializer, so this is correct for
        # all layouts (fixed or variable).
        return f'''    /* Bounds-check the wire record against its declared payload_size. */
    {{
        if (size < 2u * sizeof(uint32_t))
            return YETTY_ERR(yetty_ydraw_composite_ptr,
                             "{name}: record too small for header");
        uint64_t declared_payload = (uint64_t)((const uint32_t *)buffer_data)[1];
        if (2u * sizeof(uint32_t) + declared_payload > (uint64_t)size)
            return YETTY_ERR(yetty_ydraw_composite_ptr,
                             "{name}: payload exceeds wire record");
    }}'''

    def texture_validate_block(t):
        if t['pixels_buffer'] is None:
            return ''
        buf_idx = next(i for i, b in enumerate(buffers) if b['name'] == t['pixels_buffer'])
        sum_expr = ''.join(
            f' + payload[{uniforms_word_count + k}]' for k in range(buf_idx))
        return f'''    /* Bounds-check texture '{t["name"]}' pixels against the wire record. */
    {{
        const uint32_t *payload = (const uint32_t *)buffer_data + 2;
        if (size < (size_t){min_header_words}u * sizeof(uint32_t))
            return YETTY_ERR(yetty_ydraw_composite_ptr,
                             "{name}: record too small for texture header");
        uint32_t tex_w = payload[{t["width_wire_offset"]}];
        uint32_t tex_h = payload[{t["height_wire_offset"]}];
        if (tex_w > {TEXTURE_MAX_DIM}u || tex_h > {TEXTURE_MAX_DIM}u)
            return YETTY_ERR(yetty_ydraw_composite_ptr,
                             "{name}: texture dimensions out of range");
        uint64_t pixels_word_off = (uint64_t){buffer_data_offset}u{sum_expr};
        uint64_t pixels_byte_off = (2ull + pixels_word_off) * sizeof(uint32_t);
        uint64_t tex_need = (uint64_t)tex_w * (uint64_t)tex_h * {t["format_bpp"]}u;
        if (pixels_byte_off > (uint64_t)size ||
            tex_need > (uint64_t)size - pixels_byte_off)
            return YETTY_ERR(yetty_ydraw_composite_ptr,
                             "{name}: texture pixels exceed record");
    }}'''

    record_validation = '\n'.join(
        block for block in
        ([buffer_validate_block()] + [texture_validate_block(t) for t in textures])
        if block)
    if record_validation:
        record_validation = '\n' + record_validation + '\n'

    # ------------------------------------------------------------------
    # Hook surface — see hooks_enabled() docstring. When opted in, the
    # generated factory delegates four lifecycle points to extern
    # functions a hand-written <name>-hooks.c implements. The CPU/GPU
    # wiring above stays in the generated file (single source of truth
    # for layout); the hooks see fully-initialised structures and can
    # add stateful logic without forking gen output.
    # ------------------------------------------------------------------
    use_hooks = hooks_enabled(schema)
    if use_hooks:
        hooks_externs = f'''
/* Hook surface — see hooks_enabled() in ydraw-gen/generate.py. Implemented
 * in {name}-hooks.c (hand-written). Missing symbols are a link error. */
extern struct yetty_ycore_void_result {name}_hook_instance_create(
    struct yetty_ydraw_composite *instance,
    const void *buffer_data, size_t size);
extern void {name}_hook_instance_destroy(
    struct yetty_ydraw_composite *instance);
extern struct yetty_ycore_void_result {name}_hook_instance_update(
    struct yetty_ydraw_composite *instance,
    const void *payload, size_t size);
extern struct yetty_ycore_void_result {name}_hook_instance_render_pre(
    struct yetty_ydraw_composite *instance,
    struct yetty_ydraw_target *target, float x, float y);

static struct yetty_ycore_void_result {name}_instance_update(struct yetty_ydraw_composite *instance,
                                                             uint32_t target_field,
                                                             const void *body, size_t body_size)
{{
    if (!instance) {{
        return YETTY_ERR(yetty_ycore_void, "{name} update: instance NULL");
    }}
    if (body_size > 0 && !body) {{
        return YETTY_ERR(yetty_ycore_void, "{name} update: NULL body with non-zero size");
    }}
    /* Reassemble the [op][reserved×3][body] payload the hook still
     * expects. Avoid a heap alloc for typical update sizes; fall back
     * to malloc only for unusually large bodies. */
    enum {{ STACK_PAYLOAD_MAX = 4096u }};
    uint8_t stack_buf[STACK_PAYLOAD_MAX];
    size_t total = 4u + body_size;
    uint8_t *payload;
    bool heap = false;
    if (total <= sizeof(stack_buf)) {{
        payload = stack_buf;
    }} else {{
        payload = (uint8_t *)malloc(total);
        if (!payload) {{
            return YETTY_ERR(yetty_ycore_void, "{name} update: payload alloc failed");
        }}
        heap = true;
    }}
    payload[0] = (uint8_t)(target_field & 0xFFu);
    payload[1] = (uint8_t)((target_field >> 8) & 0xFFu);
    payload[2] = (uint8_t)((target_field >> 16) & 0xFFu);
    payload[3] = (uint8_t)((target_field >> 24) & 0xFFu);
    if (body_size > 0) {{
        memcpy(payload + 4u, body, body_size);
    }}
    struct yetty_ycore_void_result r = {name}_hook_instance_update(instance, payload, total);
    if (heap) {{
        free(payload);
    }}
    return r;
}}

/* Legacy factory adapter — kept so the abstract factory's
 * update_instance slot still resolves. scene-canvas now routes through
 * fi->ops->update directly, so this is dead in the runtime path but
 * gets removed when the factory loses the slot. */
static struct yetty_ycore_void_result {name}_update_dispatch(
    struct yetty_ydraw_concrete_factory *self,
    struct yetty_ydraw_composite *instance,
    const void *payload, size_t size)
{{
    (void)self;
    if (!payload || size < 4u) {{
        return YETTY_ERR(yetty_ycore_void, "{name} update_dispatch: payload header truncated");
    }}
    uint32_t target_field = ((const uint32_t *)payload)[0];
    return {name}_instance_update(instance, target_field, (const uint8_t *)payload + 4u,
                                  size - 4u);
}}
'''
        hooks_create_call = f'''
    /* hook_instance_create runs after RS clone + wire wiring, before
     * binder->submit. Lets the prim populate instance_data and set
     * per-instance texture dimensions before atlas pack. */
    {{
        struct yetty_ycore_void_result hcr =
            {name}_hook_instance_create(instance, buffer_data, size);
        if (YETTY_IS_ERR(hcr)) {{
            free(instance->resource_set);
            free(instance->buffer_data);
            free(instance);
            return YETTY_ERR(yetty_ydraw_composite_ptr,
                             "{name}: hook_instance_create failed", hcr);
        }}
    }}
'''
        hooks_create_rollback = f'        {name}_hook_instance_destroy(instance);\n        '
        hooks_destroy_call = f'    {name}_hook_instance_destroy(instance);\n'
        hooks_render_call = f'''
    /* hook_instance_render_pre runs after the wire→RS uniform refresh
     * and before binder->update. The prim can write texture data,
     * set dirty flags, or otherwise mutate the RS using the freshly
     * decoded / state-derived inputs. */
    {{
        struct yetty_ycore_void_result hrr =
            {name}_hook_instance_render_pre(self, target, x, y);
        if (YETTY_IS_ERR(hrr))
            return YETTY_ERR(yetty_ycore_void, "{name}: hook_render_pre failed", hrr);
    }}
'''
        hooks_factory_wire = f'    factory->base.update_instance = {name}_update_dispatch;\n'
        ops_update_member = f'{name}_instance_update'
    else:
        hooks_externs = ''
        if update_mode(schema) == 'extern':
            ops_update_member = f'yetty_{name}_instance_update'
            hooks_externs = f'''
/* Legacy factory adapter — kept so the abstract factory's
 * update_instance slot still resolves. scene-canvas now routes through
 * fi->ops->update directly, so this is dead in the runtime path but
 * gets removed when the factory loses the slot. The instance_update
 * itself is hand-written in a companion TU (see the generated header). */
static struct yetty_ycore_void_result {name}_update_dispatch(
    struct yetty_ydraw_concrete_factory *self,
    struct yetty_ydraw_composite *instance,
    const void *payload, size_t size)
{{
    (void)self;
    if (!payload || size < 4u) {{
        return YETTY_ERR(yetty_ycore_void, "{name} update_dispatch: payload header truncated");
    }}
    uint32_t target_field = ((const uint32_t *)payload)[0];
    return yetty_{name}_instance_update(instance, target_field,
                                        (const uint8_t *)payload + 4u, size - 4u);
}}
'''
        else:
            ops_update_member = ('NULL /* the wire never carries CMD_UPDATE for this figure — '
                                 'producers swap the whole record instead */')
        hooks_create_call = ''
        hooks_create_rollback = '        '
        hooks_destroy_call = ''
        hooks_render_call = ''
        hooks_factory_wire = ''
        if update_mode(schema) == 'extern':
            hooks_factory_wire = f'    factory->base.update_instance = {name}_update_dispatch;\n'
    lifecycle_create_call = ''
    if schema.get('lifecycle_extern'):
        lifecycle_create_call = f'''
    /* Companion-TU lifecycle callout (e.g. hooking the instance into a
     * shared animation timer). Runs after the binder is fully finalized.
     * Failure is non-fatal — the figure still renders, it just skips
     * whatever the callout would have enabled. */
    {{
        struct yetty_ycore_void_result created_res = yetty_{name}_instance_created(instance);
        if (YETTY_IS_ERR(created_res)) {{
            ywarn("{name}: instance_created callout failed: %s", created_res.error.msg);
            yetty_ycore_error_destroy(created_res.error);
        }}
    }}
'''
        hooks_destroy_call = (f'    /* Companion-TU teardown FIRST — e.g. dropping a timer listener\n'
                              f'\t * whose list holds a pointer into this instance. */\n'
                              f'    yetty_{name}_instance_destroying(instance);\n') + hooks_destroy_call

    # webgpu.h is pulled in via the ydraw-factory header (server-only).
    # Wire format / type-id ranges come from ydraw-core/composite.h.

    return f'''// Auto-generated from {name}.yaml - DO NOT EDIT
//
// Two-tier composite model:
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
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>
{lib_includes}
{hooks_externs}
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

struct yetty_{name}_factory {{
    struct yetty_ydraw_concrete_factory base;
    /* Shared, compiled once. NULL until compile_pipeline. */
    struct yetty_yrender_pipeline *pipeline;
    /* Template RS: shape definition for both the pipeline and per-instance
     * RSes. Children point to the shared static library RSes. */
    struct yetty_yrender_gpu_resource_set template_rs;
    int template_initialized;

    WGPUDevice device;
    WGPUQueue queue;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* Zoom state — written by the canvas into the factory, read by each
     * instance render() and pushed into the instance's own RS uniforms. */
    float visual_zoom_scale;
    float visual_zoom_off_x;
    float visual_zoom_off_y;
    float cell_zoom_scale;
    float cell_zoom_off_x;
    float cell_zoom_off_y;
}};

static struct yetty_{name}_factory *yetty_{name}_factory_from_base(struct yetty_ydraw_concrete_factory *base)
{{
    return (struct yetty_{name}_factory *)base;
}}

// Wire-format serialize helpers live in {name}-gen-wire.c
// (yetty_{name}_core, GPU-less, riscv64-safe).

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

{rs_resources_setup}
}}

//=============================================================================
// Instance Rendering — uses self->resource_set + self->binder; the factory
// supplies only the shared pipeline + zoom state.
//=============================================================================

static struct yetty_ycore_void_result
{name}_instance_render(struct yetty_ydraw_composite *self,
                       struct yetty_ydraw_target *target, float x, float y)
{{
    if (!self || !self->buffer_data || !self->factory)
        return YETTY_ERR(yetty_ycore_void, "invalid instance");
    if (!self->resource_set || !self->binder)
        return YETTY_ERR(yetty_ycore_void, "instance not finalised");

    struct yetty_{name}_factory *factory = yetty_{name}_factory_from_base(self->factory);
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

    // Compose the caller-provided pane position with the record's own
    // ENVELOPE-LOCAL origin: a multi-figure envelope (a browser page's
    // images) lays its figures out internally, so each record's wire
    // bounds_x/y is its offset inside the block anchored at (x, y).
    // Single-figure producers (ycat) ship a 0,0 origin — for them this
    // reduces to the plain caller position. bounds_w/h are LOGICAL pixels;
    // the offset and body both scale by content_scale (1.0 in the
    // terminal's local compositor).
    {{
        float figure_content_scale = self->content_scale > 0.0f ? self->content_scale : 1.0f;
        rs->uniforms[0].f32 = x + rs->uniforms[0].f32 * figure_content_scale;
        rs->uniforms[1].f32 = y + rs->uniforms[1].f32 * figure_content_scale;
        rs->uniforms[2].f32 *= figure_content_scale;
        rs->uniforms[3].f32 *= figure_content_scale;
    }}

{render_resources_wiring}
{hooks_render_call}
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

    /* The pane's render target may sit at a non-zero offset inside the
     * big surface (e.g. yui pushes the terminal pane down by the titlebar
     * height). The layer's simple-prim pass already draws to
     * (vp.x, vp.y, vp.w, vp.h); the complex prim must use the same rect,
     * otherwise its fullscreen triangle covers a different region of the
     * framebuffer than the rest of the layer and the FS would compare
     * canvas-local bounds against the wrong coordinate system — see the
     * pane_pixel comment in the matching shader. */
    wgpuRenderPassEncoderSetViewport(pass, target->viewport.x, target->viewport.y,
        target->viewport.w, target->viewport.h, 0.0f, 1.0f);

    /* Scissor to the viewport, intersected with the compositor's clip rect
     * when one is set (e.g. a scrolling ygrid's scroll-area bounds) so the
     * figure is clipped to its container instead of bleeding over
     * surrounding chrome such as the tab bar. */
    float scissor_x0 = target->viewport.x;
    float scissor_y0 = target->viewport.y;
    float scissor_x1 = target->viewport.x + target->viewport.w;
    float scissor_y1 = target->viewport.y + target->viewport.h;
    if (target->clip.w > 0.0f && target->clip.h > 0.0f) {{
        if (target->clip.x > scissor_x0) {{
            scissor_x0 = target->clip.x;
        }}
        if (target->clip.y > scissor_y0) {{
            scissor_y0 = target->clip.y;
        }}
        if (target->clip.x + target->clip.w < scissor_x1) {{
            scissor_x1 = target->clip.x + target->clip.w;
        }}
        if (target->clip.y + target->clip.h < scissor_y1) {{
            scissor_y1 = target->clip.y + target->clip.h;
        }}
    }}
    if (scissor_x1 < scissor_x0) {{
        scissor_x1 = scissor_x0;
    }}
    if (scissor_y1 < scissor_y0) {{
        scissor_y1 = scissor_y0;
    }}
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)scissor_x0, (uint32_t)scissor_y0,
                                        (uint32_t)(scissor_x1 - scissor_x0),
                                        (uint32_t)(scissor_y1 - scissor_y0));

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
{name}_compile_pipeline(struct yetty_ydraw_concrete_factory *self,
                        WGPUDevice device, WGPUQueue queue,
                        WGPUTextureFormat target_format,
                        struct yetty_ydraw_gpu_allocator *allocator)
{{
    struct yetty_{name}_factory *factory = yetty_{name}_factory_from_base(self);

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

static WGPURenderPipeline {name}_get_pipeline(struct yetty_ydraw_concrete_factory *self)
{{
    struct yetty_{name}_factory *factory = yetty_{name}_factory_from_base(self);
    return factory->pipeline ? yetty_yrender_pipeline_get_pipeline(factory->pipeline) : NULL;
}}

/* Forward decl — vtable definition lives below; create_instance just
 * needs its address. */
static const struct yetty_ydraw_composite_ops {name}_figure_ops;

static struct yetty_ydraw_composite_ptr_result
{name}_create_instance(struct yetty_ydraw_concrete_factory *self,
                       const void *buffer_data, size_t size, uint32_t rolling_row)
{{
    if (!buffer_data || size < sizeof(struct yetty_ydraw_composite_record))
        return YETTY_ERR(yetty_ydraw_composite_ptr, "invalid buffer data");
{record_validation}
    struct yetty_{name}_factory *factory = yetty_{name}_factory_from_base(self);
    if (!factory->pipeline)
        return YETTY_ERR(yetty_ydraw_composite_ptr,
                         "{name} factory pipeline not compiled");

    struct yetty_ydraw_composite *instance =
        calloc(1, sizeof(struct yetty_ydraw_composite));
    if (!instance)
        return YETTY_ERR(yetty_ydraw_composite_ptr, "allocation failed");

    instance->buffer_data = malloc(size);
    if (!instance->buffer_data) {{
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "buffer alloc failed");
    }}
    memcpy(instance->buffer_data, buffer_data, size);
    instance->buffer_size = size;
    instance->type = YETTY_{NAME}_TYPE_ID;
    instance->factory = self;
    instance->rolling_row = rolling_row;
    instance->render = {name}_instance_render;
    instance->ops = &{name}_figure_ops;

    struct rectangle_result aabb_res = yetty_ydraw_composite_record_aabb(buffer_data);
    if (YETTY_IS_OK(aabb_res))
        instance->bounds = aabb_res.value;

    /* Per-instance RS. Same shape as the factory template (so the binder
     * flattens to the same layout the pipeline was compiled against), but
     * with per-instance buffer/uniform values (set in render). */
    instance->resource_set = malloc(sizeof(struct yetty_yrender_gpu_resource_set));
    if (!instance->resource_set) {{
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr, "rs alloc failed");
    }}
    memcpy(instance->resource_set, &factory->template_rs,
           sizeof(struct yetty_yrender_gpu_resource_set));

{instance_resources_wiring}
{hooks_create_call}
    /* Per-instance binder bound to the factory's shared pipeline. Owns
     * its OWN uniform_buffer / storage_buffer / bind_group. */
    struct yetty_yrender_gpu_resource_binder_result br =
        yetty_yrender_gpu_resource_binder_create_with_pipeline(
            factory->device, factory->queue, factory->allocator, factory->pipeline);
    if (YETTY_IS_ERR(br)) {{
        ydebug("{name}_create_instance: binder_create FAILED: %s", br.error.msg);
{hooks_create_rollback}free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr,
                         "instance binder create failed", br);
    }}
    instance->binder = br.value;

    struct yetty_ycore_void_result sr =
        instance->binder->ops->submit(instance->binder, instance->resource_set);
    if (YETTY_IS_ERR(sr)) {{
        instance->binder->ops->destroy(instance->binder);
{hooks_create_rollback}free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr,
                         "binder submit failed", sr);
    }}

    struct yetty_ycore_void_result fr = instance->binder->ops->finalize(instance->binder);
    if (YETTY_IS_ERR(fr)) {{
        instance->binder->ops->destroy(instance->binder);
{hooks_create_rollback}free(instance->resource_set);
        free(instance->buffer_data);
        free(instance);
        return YETTY_ERR(yetty_ydraw_composite_ptr,
                         "binder finalize failed", fr);
    }}

{lifecycle_create_call}    ydebug("{name}_create_instance: OK bounds=(%.0f,%.0f,%.0f,%.0f)", instance->bounds.min.x,
           instance->bounds.min.y, instance->bounds.max.x, instance->bounds.max.y);
    return YETTY_OK(yetty_ydraw_composite_ptr, instance);
}}

static void {name}_instance_destroy(struct yetty_ydraw_composite *instance)
{{
    if (!instance)
        return;
{hooks_destroy_call}    if (instance->binder)
        instance->binder->ops->destroy(instance->binder);
    free(instance->resource_set);
    free(instance->buffer_data);
    free(instance);
}}

/* Per-instance vtable installed on every {name} figure_instance. */
static const struct yetty_ydraw_composite_ops {name}_figure_ops = {{
    .destroy = {name}_instance_destroy,
    .update = {ops_update_member},
}};

/* Legacy factory adapter — kept for the factory->destroy_instance
 * fallback path. */
static void {name}_destroy_instance(struct yetty_ydraw_concrete_factory *self,
                                    struct yetty_ydraw_composite *instance)
{{
    (void)self;
    {name}_instance_destroy(instance);
}}

static struct yetty_yrender_gpu_resource_set *{name}_get_shared_rs(
    struct yetty_ydraw_concrete_factory *self)
{{
    /* Returns the structural template, NOT a mutable per-instance RS. */
    struct yetty_{name}_factory *factory = yetty_{name}_factory_from_base(self);
    return factory->template_initialized ? &factory->template_rs : NULL;
}}

static struct yetty_ycore_void_result
{name}_set_visual_zoom(struct yetty_ydraw_concrete_factory *self,
                       float scale, float off_x, float off_y)
{{
    struct yetty_{name}_factory *factory = yetty_{name}_factory_from_base(self);
    factory->visual_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->visual_zoom_off_x = off_x;
    factory->visual_zoom_off_y = off_y;
    return YETTY_OK_VOID();
}}

static struct yetty_ycore_void_result
{name}_set_cell_zoom(struct yetty_ydraw_concrete_factory *self,
                     float scale, float off_x, float off_y)
{{
    struct yetty_{name}_factory *factory = yetty_{name}_factory_from_base(self);
    factory->cell_zoom_scale = (scale > 0.0f) ? scale : 1.0f;
    factory->cell_zoom_off_x = off_x;
    factory->cell_zoom_off_y = off_y;
    ydebug("{name}_set_cell_zoom: scale=%.3f off=(%.1f,%.1f)", scale, off_x, off_y);
    return YETTY_OK_VOID();
}}

struct yetty_ydraw_concrete_factory *yetty_{name}_factory_create(void)
{{
    struct yetty_{name}_factory *factory = calloc(1, sizeof(struct yetty_{name}_factory));
    if (!factory)
        return NULL;

    factory->base.type_id = YETTY_{NAME}_TYPE_ID;
    factory->base.destroy = yetty_{name}_factory_destroy;
    factory->base.compile_pipeline = {name}_compile_pipeline;
    factory->base.get_pipeline = {name}_get_pipeline;
    factory->base.create_instance = {name}_create_instance;
    factory->base.destroy_instance = {name}_destroy_instance;
{hooks_factory_wire}    factory->base.get_shared_rs = {name}_get_shared_rs;
    factory->base.set_visual_zoom = {name}_set_visual_zoom;
    factory->base.set_cell_zoom = {name}_set_cell_zoom;

    factory->visual_zoom_scale = 1.0f;
    factory->cell_zoom_scale = 1.0f;

    return &factory->base;
}}

void yetty_{name}_factory_destroy(struct yetty_ydraw_concrete_factory *self)
{{
    if (!self)
        return;

    struct yetty_{name}_factory *factory = yetty_{name}_factory_from_base(self);

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

    for ext in external_uniforms(schema):
        out += f'''
// `{ext["name"]}` — server-side uniform, written CPU-side by a companion
// TU straight into the instance RS (never on the wire). Always present in
// the uniform buffer; reads its zero-default until something writes it.
fn {name}_get_{ext["name"]}() -> {ext["type"]} {{
    return uniforms.{name}_{ext["name"]};
}}
'''

    if merged_storage(buffers):
        # Storage-walk helpers for the merged layout: per fixed buffer a
        # len/offset pair (running offsets, resolved in-shader), then the
        # count accessor for the trailing array buffer.
        out += '\n// ---- storage layout helpers (merged storage region) ----\n'
        fixed = [b for b in buffers if not b.get('array')]
        arr = next(b for b in buffers if b.get('array'))
        offset_expr = '0u'
        for b in fixed:
            out += f'''
// Length of the {b["name"]} block (in u32 words); data starts right after
// its length word.
fn {name}_{b["name"]}_len() -> u32 {{
    return storage_buffer[{offset_expr}];
}}
fn {name}_{b["name"]}_offset() -> u32 {{
    return {offset_expr} + 1u;
}}
'''
            offset_expr = f'{name}_{b["name"]}_offset() + {name}_{b["name"]}_len()'
        out += f'''
// Word index of the {arr["name"]}_count u32 (right after the fixed blocks).
fn {name}_{arr["name"]}_count_offset() -> u32 {{
    return {offset_expr};
}}

// Number of {arr["name"]} buffers carried by this instance (0 is valid).
fn {name}_{arr["name"]}_count() -> u32 {{
    return storage_buffer[{name}_{arr["name"]}_count_offset()];
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
// YAML parser factory for {name} composite

#include <yetty/{name}/{name}-gen.h>
#include <yetty/ydraw-yaml/ydraw-yaml.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/ytrace/ytrace.h>
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
{name}_yaml_factory(struct yetty_ydraw_drawable_list *buffer,
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

    size_t required = yetty_{name}_uniforms_serialized_size(&uniforms, &bufs);
    uint8_t *drawable_buf = malloc(required);
    if (!drawable_buf)
        return YETTY_ERR(yetty_ycore_void, "malloc failed");

    struct yetty_ycore_size_result ser_res =
        yetty_{name}_uniforms_serialize(&uniforms, &bufs, drawable_buf, required);
    if (YETTY_IS_ERR(ser_res)) {{
        free(drawable_buf);
        return YETTY_ERR(yetty_ycore_void, "{name} uniforms serialize failed", ser_res);
    }}

    struct yetty_ydraw_id_result id_res =
        yetty_ydraw_drawable_list_add_prim(buffer, drawable_buf, required);
    free(drawable_buf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "{name} yaml: add_prim failed");
    return YETTY_OK_VOID();
}}

struct yetty_ycore_void_result
yetty_{name}_register_yaml_factory(struct yetty_ydraw_yaml_parser *parser)
{{
    return yetty_ydraw_yaml_parser_register(parser, "{name}", {name}_yaml_factory);
}}
'''


def main():
    if len(sys.argv) != 2:
        print(f'Usage: {sys.argv[0]} <schema.yaml>', file=sys.stderr)
        sys.exit(1)

    schema_path = Path(sys.argv[1])
    schema = load_schema(schema_path)
    uniforms, buffers, textures = calculate_layout(schema)
    yaml_mode = yaml_factory_mode(schema)

    name = schema['name']
    src_dir = schema_path.parent
    # Header goes to include/yetty/<module>/, source files stay in src/yetty/<module>/
    # Schema is at src/yetty/<module>/<module>.yaml
    # Include dir is at include/yetty/<module>/
    include_dir = src_dir.parent.parent.parent / 'include' / 'yetty' / name
    include_dir.mkdir(parents=True, exist_ok=True)

    header = generate_c_header(schema, uniforms, buffers, textures, yaml_mode)
    source = generate_c_source(schema, uniforms, buffers, textures)
    wire_source = generate_c_wire_source(schema, uniforms, buffers)
    wgsl = generate_wgsl_bindings(schema, uniforms, buffers)

    (include_dir / f'{name}-gen.h').write_text(header + '\n')
    (src_dir / f'{name}-gen.c').write_text(source + '\n')
    (src_dir / f'{name}-gen-wire.c').write_text(wire_source + '\n')
    (src_dir / f'{name}-gen.wgsl').write_text(wgsl + '\n')

    print(f'Generated:')
    print(f'  {include_dir / f"{name}-gen.h"}')
    print(f'  {src_dir / f"{name}-gen.c"}')
    print(f'  {src_dir / f"{name}-gen-wire.c"}')
    print(f'  {src_dir / f"{name}-gen.wgsl"}')

    yaml_path = src_dir / f'{name}-gen-yaml.c'
    if yaml_mode == 'none':
        # Schema explicitly opts out — drop any stale generated file.
        if yaml_path.exists():
            yaml_path.unlink()
        print(f'  (no yaml factory generated; yaml_factory: none)')
    elif yaml_mode == 'manual':
        # A hand-written {name}-yaml.c implements the registration entry
        # point; only the decl is generated (in the header). Drop any
        # stale generated parser so it cannot shadow the hand file.
        if yaml_path.exists():
            yaml_path.unlink()
        print(f'  (yaml factory is hand-written: {name}-yaml.c)')
    else:
        yaml_parser = generate_yaml_parser(schema, uniforms, buffers)
        yaml_path.write_text(yaml_parser + '\n')
        print(f'  {yaml_path}')


if __name__ == '__main__':
    main()
