#!/usr/bin/env python3
"""
yrdawn-gen — emits the per-method shims for the WebGPU-over-OSC bridge.

Run by hand whenever the METHODS table below changes:

    python3 src/yetty/yrdawn/generate.py

Output is written in place into the repo (committed source, not a
build artifact):

    include/yetty/yrdawn/methods.gen.h
    src/yetty/yrdawn/client-stubs.gen.c
    src/yetty/yrdawn/server-dispatch.gen.c

The four paths can be overridden via the corresponding --out-* flags
for one-off experiments. The method spec is hand-curated as a Python
list for now; when dawn.json becomes accessible we'll feed it in
instead and the output shape stays unchanged.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


# ---------------------------------------------------------------------------
# Method table (hand-curated starter set).
#
# Each entry:
#   name      — exact WebGPU C entrypoint name (used verbatim in generated
#               wrappers, dispatcher case labels, etc.)
#   id        — stable method_id sent on the wire; never renumber
#   args      — wire fields, in order. Each {name, ctype, wgpu_kind}.
#               ctype: "u64" (handle), "u32", "i32", "f32", "void"
#               wgpu_kind (server-side): "in_handle", "out_handle",
#                                        "scalar" or omitted
#   returns   — "void" or "handle" (sync — the new handle was allocated
#               client-side and is in args)
# ---------------------------------------------------------------------------

METHODS = [
    {
        "name": "wgpuCreateInstance",
        "id": 1,
        "args": [
            {"name": "result_handle", "ctype": "u64", "wgpu_kind": "out_handle"},
        ],
        "returns": "handle",
        "server_body": """\
        WGPUInstance inst = (WGPUInstance)yrdawn_server_get_shared_instance(ctx);
        if (!inst)
            return YETTY_YRDAWN_REPLY_INTERNAL;
        wgpuInstanceAddRef(inst);
        struct yetty_ycore_void_result r =
            yrdawn_server_handle_set(ctx, a->result_handle, inst);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
            wgpuInstanceRelease(inst);
            return YETTY_YRDAWN_REPLY_INTERNAL;
        }
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuInstanceRelease",
        "id": 2,
        "args": [
            {"name": "instance", "ctype": "u64", "wgpu_kind": "in_handle"},
        ],
        "returns": "void",
        "server_body": """\
        WGPUInstance inst = (WGPUInstance)yrdawn_server_handle_get(ctx, a->instance);
        if (!inst)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        wgpuInstanceRelease(inst);
        yrdawn_server_handle_release(ctx, a->instance);
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuDeviceTick",
        "id": 3,
        "args": [
            {"name": "device", "ctype": "u64", "wgpu_kind": "in_handle"},
        ],
        "returns": "void",
        "server_body": """\
        WGPUDevice device = (WGPUDevice)yrdawn_server_handle_get(ctx, a->device);
        if (!device)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        wgpuDeviceTick(device);
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuDeviceGetQueue",
        "id": 4,
        "args": [
            {"name": "device", "ctype": "u64", "wgpu_kind": "in_handle"},
            {"name": "result_handle", "ctype": "u64", "wgpu_kind": "out_handle"},
        ],
        "returns": "handle",
        "server_body": """\
        WGPUDevice device = (WGPUDevice)yrdawn_server_handle_get(ctx, a->device);
        if (!device)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        /* Hand back the shared queue rather than the per-device default
         * (they're the same object for yetty's setup, but going through
         * the accessor keeps the bridge honest about which Dawn pointer
         * it's exposing). */
        WGPUQueue q = (WGPUQueue)yrdawn_server_get_shared_queue(ctx);
        if (!q) return YETTY_YRDAWN_REPLY_INTERNAL;
        wgpuQueueAddRef(q);
        struct yetty_ycore_void_result r =
            yrdawn_server_handle_set(ctx, a->result_handle, q);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
            wgpuQueueRelease(q);
            return YETTY_YRDAWN_REPLY_INTERNAL;
        }
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuQueueRelease",
        "id": 5,
        "args": [{"name": "queue", "ctype": "u64", "wgpu_kind": "in_handle"}],
        "returns": "void",
        "server_body": """\
        WGPUQueue q = (WGPUQueue)yrdawn_server_handle_get(ctx, a->queue);
        if (!q)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        wgpuQueueRelease(q);
        yrdawn_server_handle_release(ctx, a->queue);
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuDeviceRelease",
        "id": 6,
        "args": [{"name": "device", "ctype": "u64", "wgpu_kind": "in_handle"}],
        "returns": "void",
        "server_body": """\
        WGPUDevice d = (WGPUDevice)yrdawn_server_handle_get(ctx, a->device);
        if (!d)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        wgpuDeviceRelease(d);
        yrdawn_server_handle_release(ctx, a->device);
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuAdapterRelease",
        "id": 7,
        "args": [{"name": "adapter", "ctype": "u64", "wgpu_kind": "in_handle"}],
        "returns": "void",
        "server_body": """\
        WGPUAdapter ad = (WGPUAdapter)yrdawn_server_handle_get(ctx, a->adapter);
        if (!ad)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        wgpuAdapterRelease(ad);
        yrdawn_server_handle_release(ctx, a->adapter);
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuDeviceCreateCommandEncoder",
        "id": 8,
        "args": [
            {"name": "device", "ctype": "u64", "wgpu_kind": "in_handle"},
            {"name": "result_handle", "ctype": "u64", "wgpu_kind": "out_handle"},
        ],
        "returns": "handle",
        "server_body": """\
        WGPUDevice device = (WGPUDevice)yrdawn_server_handle_get(ctx, a->device);
        if (!device)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        WGPUCommandEncoderDescriptor desc = {0};
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, &desc);
        if (!enc)
            return YETTY_YRDAWN_REPLY_INTERNAL;
        struct yetty_ycore_void_result r =
            yrdawn_server_handle_set(ctx, a->result_handle, enc);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
            wgpuCommandEncoderRelease(enc);
            return YETTY_YRDAWN_REPLY_INTERNAL;
        }
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuCommandEncoderRelease",
        "id": 9,
        "args": [{"name": "encoder", "ctype": "u64", "wgpu_kind": "in_handle"}],
        "returns": "void",
        "server_body": """\
        WGPUCommandEncoder e = (WGPUCommandEncoder)yrdawn_server_handle_get(ctx, a->encoder);
        if (!e)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        wgpuCommandEncoderRelease(e);
        yrdawn_server_handle_release(ctx, a->encoder);
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuCommandEncoderFinish",
        "id": 10,
        "args": [
            {"name": "encoder", "ctype": "u64", "wgpu_kind": "in_handle"},
            {"name": "result_handle", "ctype": "u64", "wgpu_kind": "out_handle"},
        ],
        "returns": "handle",
        "server_body": """\
        WGPUCommandEncoder enc = (WGPUCommandEncoder)yrdawn_server_handle_get(ctx, a->encoder);
        if (!enc)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        WGPUCommandBufferDescriptor desc = {0};
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, &desc);
        if (!cb)
            return YETTY_YRDAWN_REPLY_INTERNAL;
        struct yetty_ycore_void_result r =
            yrdawn_server_handle_set(ctx, a->result_handle, cb);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
            wgpuCommandBufferRelease(cb);
            return YETTY_YRDAWN_REPLY_INTERNAL;
        }
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    {
        "name": "wgpuCommandBufferRelease",
        "id": 11,
        "args": [{"name": "buffer", "ctype": "u64", "wgpu_kind": "in_handle"}],
        "returns": "void",
        "server_body": """\
        WGPUCommandBuffer cb = (WGPUCommandBuffer)yrdawn_server_handle_get(ctx, a->buffer);
        if (!cb)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        wgpuCommandBufferRelease(cb);
        yrdawn_server_handle_release(ctx, a->buffer);
        return YETTY_YRDAWN_REPLY_OK;""",
    },
    # ----- Async gateway methods (req_id required) ----------------------
    {
        "name": "wgpuInstanceRequestAdapter",
        "id": 12,
        "args": [
            {"name": "instance", "ctype": "u64", "wgpu_kind": "in_handle"},
            {"name": "result_handle", "ctype": "u64", "wgpu_kind": "out_handle"},
        ],
        "returns": "handle",
        "async": True,
        "server_body": """\
        if (req_id == 0)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        (void)a;
        /* Hand back yetty's already-selected adapter — no Dawn call. We
         * still emit the REPLY through the async path so the client's
         * trampoline-driven wait completes the same way it would for a
         * real adapter request. */
        WGPUAdapter shared = (WGPUAdapter)yrdawn_server_get_shared_adapter(ctx);
        if (!shared) return YETTY_YRDAWN_REPLY_INTERNAL;
        wgpuAdapterAddRef(shared);
        struct yetty_ycore_void_result r =
            yrdawn_server_handle_set(ctx, a->result_handle, shared);
        uint32_t status = YETTY_YRDAWN_REPLY_OK;
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
            wgpuAdapterRelease(shared);
            status = YETTY_YRDAWN_REPLY_INTERNAL;
        }
        struct yetty_ycore_void_result e =
            yrdawn_server_emit_reply(ctx, req_id, method_id, status, NULL, 0);
        if (YETTY_IS_ERR(e)) yetty_ycore_error_destroy(e.error);
        return YRDAWN_DISPATCH_DEFERRED;""",
    },
    {
        "name": "wgpuAdapterRequestDevice",
        "id": 13,
        "args": [
            {"name": "adapter", "ctype": "u64", "wgpu_kind": "in_handle"},
            {"name": "result_handle", "ctype": "u64", "wgpu_kind": "out_handle"},
        ],
        "returns": "handle",
        "async": True,
        "server_body": """\
        if (req_id == 0)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        (void)a;
        /* Hand back yetty's already-active device for the same reason
         * as RequestAdapter — running a second Dawn device in-process
         * deadlocks Vulkan on the first pipeline-creation call. */
        WGPUDevice shared = (WGPUDevice)yrdawn_server_get_shared_device(ctx);
        if (!shared) return YETTY_YRDAWN_REPLY_INTERNAL;
        wgpuDeviceAddRef(shared);
        struct yetty_ycore_void_result r =
            yrdawn_server_handle_set(ctx, a->result_handle, shared);
        uint32_t status = YETTY_YRDAWN_REPLY_OK;
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
            wgpuDeviceRelease(shared);
            status = YETTY_YRDAWN_REPLY_INTERNAL;
        }
        struct yetty_ycore_void_result e =
            yrdawn_server_emit_reply(ctx, req_id, method_id, status, NULL, 0);
        if (YETTY_IS_ERR(e)) yetty_ycore_error_destroy(e.error);
        return YRDAWN_DISPATCH_DEFERRED;""",
    },
    # Yetty-defined presentation primitive: the client uploads a frame
    # (pixel bytes shipped via a BULK ref) and the server swaps it into
    # the layer's display texture. Method id 100+ leaves room for future
    # webgpu.h methods below. skip_client_stub: the user-facing helper
    # (yetty_yrdawn_client_present_frame) lives in client.c because it
    # orchestrates BULK emit + CMD emit; the codegen stub would be
    # incomplete.
    {
        "name": "yetty_yrdawn_present_frame",
        "id": 100,
        "args": [
            {"name": "width", "ctype": "u32"},
            {"name": "height", "ctype": "u32"},
            {"name": "format", "ctype": "u32"},
            {"name": "payload_ref", "ctype": "u32"},
        ],
        "returns": "void",
        "skip_client_stub": True,
        "server_body": """\
        if (a->format != 0)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        if (a->payload_ref == 0)
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        struct yetty_ycore_buffer pixels = {0};
        if (!yrdawn_server_bulk_take(ctx, a->payload_ref, &pixels))
            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;
        struct yetty_ycore_void_result rr =
            yrdawn_server_set_frame(ctx, a->width, a->height, pixels.data, pixels.size);
        yetty_ycore_buffer_destroy(&pixels);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
            return YETTY_YRDAWN_REPLY_INTERNAL;
        }
        return YETTY_YRDAWN_REPLY_OK;""",
    },
]


CTYPE_C = {
    "u64": "uint64_t",
    "u32": "uint32_t",
    "i32": "int32_t",
    "i64": "int64_t",
    "f32": "float",
    "f64": "double",
}


def to_c_type(ctype: str) -> str:
    """Resolve a wire-side ctype tag to a C declaration. Accepts both
    the short tags ('u32', 'u64', ...) used by the hand-curated METHODS
    table and raw C type names ('uint32_t', 'WGPUFlags', ...) used by
    the auto-discovered entries."""
    return CTYPE_C.get(ctype, ctype)


# ---------------------------------------------------------------------------
# webgpu.h parser
#
# Reads the Dawn-flavoured webgpu.h that's downloaded into the build
# tree by CMake and emits a method spec for every WGPU_EXPORT wgpu*
# entrypoint. Entries already in the hand-curated METHODS table above
# keep their server_body / trampoline as-is — auto-discovery only adds
# the long tail.
#
# Methods that fit a recognised pattern get a real server_body:
#     wgpu<Type>Release(WGPUFoo)   → handle lookup + wgpu*Release + table drop
#     wgpu<Type>AddRef(WGPUFoo)    → handle lookup + wgpu*AddRef
# Everything else lands as a stub that returns UNKNOWN_METHOD so the
# client sees a clear "not implemented yet" signal rather than a silent
# OK. Wire-side, every entrypoint still has an args struct, a method_id,
# and a client stub — adding a real body later is a one-line change in
# this generator, no wire reshuffle.
# ---------------------------------------------------------------------------

_RE_EXPORT = re.compile(
    r"^WGPU_EXPORT\s+(?P<ret>.+?)\s+(?P<name>wgpu\w+)\s*\((?P<args>[^)]*)\)\s*(?:WGPU_FUNCTION_ATTRIBUTE\s*)?;",
    re.MULTILINE,
)

_RE_HANDLE_TYPEDEF = re.compile(
    # `typedef struct WGPUFooImpl* WGPUFoo WGPU_OBJECT_ATTRIBUTE;` — the
    # attribute suffix is Dawn-specific so match up to the type name's
    # word boundary instead of requiring a trailing `;`.
    r"typedef\s+struct\s+(WGPU\w+)Impl\s*\*\s*(WGPU\w+)\b"
)

_RE_ENUM_TYPEDEF = re.compile(r"typedef\s+enum\s+(WGPU\w+)\s*\{")
_RE_FLAG_TYPEDEF = re.compile(r"typedef\s+WGPU(Flags|Flags64)\s+(WGPU\w+)\s*;")
_RE_PRIM_TYPEDEF = re.compile(
    r"typedef\s+(uint32_t|uint64_t|int32_t|int64_t|float|double|size_t)\s+(WGPU\w+)\s*;"
)
_RE_STRUCT_DEF = re.compile(r"typedef\s+struct\s+(WGPU\w+)\s*\{")
_RE_CALLBACK_TYPEDEF = re.compile(
    r"typedef\s+(\w+)\s+\(\s*\*\s*(WGPU\w+Callback)\s*\)\s*\(([^)]*)\)"
)
_RE_STYPE_ENUM_VALUE = re.compile(r"WGPUSType_(\w+)\s*=\s*0x[0-9A-Fa-f]+")


# ---------------------------------------------------------------------------
# Field overrides — info that lives in dawn.json (the canonical IDL) but
# not in webgpu.h. Tiny, explicit, and per-field so a future webgpu.h
# update that renames or moves a field will surface a build failure rather
# than silently mis-encoding bytes.

# Pointer fields whose array length isn't carried on the wire — it's a
# documented constant. Key: (struct_name, field_name) → element count.
FIXED_LEN_PTR_FIELDS: dict[tuple[str, str], int] = {
    ("WGPUExternalTextureDescriptor", "yuvToRgbConversionMatrix"): 12,
    ("WGPUExternalTextureDescriptor", "srcTransferFunctionParameters"): 7,
    ("WGPUExternalTextureDescriptor", "dstTransferFunctionParameters"): 7,
    ("WGPUExternalTextureDescriptor", "gamutConversionMatrix"): 9,
    ("WGPUCopyTextureForBrowserOptions", "srcTransferFunctionParameters"): 7,
    ("WGPUCopyTextureForBrowserOptions", "conversionMatrix"): 9,
    ("WGPUCopyTextureForBrowserOptions", "dstTransferFunctionParameters"): 7,
}

# Pointer fields that share their length with another count field in
# the same struct (`length: "fooCount"` in dawn.json — multiple arrays
# bound to the same count). Key: (struct_name, field_name) → count
# field name. The encoder/decoder treats these as array_items keyed off
# the named count.
SHARED_COUNT_PTR_FIELDS: dict[tuple[str, str], str] = {
    ("WGPUSharedBufferMemoryBeginAccessDescriptor", "signaledValues"): "fenceCount",
    ("WGPUSharedBufferMemoryEndAccessState", "signaledValues"): "fenceCount",
    ("WGPUSharedTextureMemoryBeginAccessDescriptor", "signaledValues"): "fenceCount",
    ("WGPUSharedTextureMemoryEndAccessState", "signaledValues"): "fenceCount",
}


def parse_stype_to_struct(src: str,
                          struct_fields: dict[str, list[tuple[str, str]]]
                          ) -> dict[str, str]:
    """Return {sType_name: struct_name} for every WGPUSType_X enum value
    whose matching WGPUX struct actually starts with `WGPUChainedStruct
    chain;`. The codegen uses this to dispatch on `nextInChain->sType`
    and pick the right encoder/decoder. Structs that share the sType
    naming convention but aren't chain links (e.g. plain "Properties"
    outputs that have no chain header) are excluded."""
    out: dict[str, str] = {}
    for m in _RE_STYPE_ENUM_VALUE.finditer(src):
        base = m.group(1)
        struct_name = "WGPU" + base
        fields = struct_fields.get(struct_name)
        if not fields:
            continue
        ft, fn = fields[0]
        first_type = " ".join(ft.replace("const", "").split())
        if first_type == "WGPUChainedStruct" and fn == "chain":
            out["WGPUSType_" + base] = struct_name
    return out


def parse_callbacks(src: str) -> dict[str, tuple[str, list[tuple[str, str]]]]:
    """Return callback typedef name → (return_type, [(arg_type, arg_name), …]).
    `typedef void (*WGPUFooCallback)(WGPUFooStatus status, void *u1, void *u2)`."""
    out: dict[str, tuple[str, list[tuple[str, str]]]] = {}
    for m in _RE_CALLBACK_TYPEDEF.finditer(src):
        rt = m.group(1).strip()
        name = m.group(2)
        raw = m.group(3).strip()
        args: list[tuple[str, str]] = []
        if raw and raw != "void":
            for chunk in raw.split(","):
                chunk = " ".join(chunk.split())
                parts = chunk.rsplit(maxsplit=1)
                if len(parts) == 2:
                    at = parts[0].strip()
                    an = parts[1].lstrip("*")
                    if parts[1].startswith("*"):
                        at = at + (" *" * (len(parts[1]) - len(an)))
                    args.append((at.strip(), an))
        out[name] = (rt, args)
    return out


def callback_for_info_struct(info_name: str, struct_fields, callbacks) -> str | None:
    """Given `WGPUFooCallbackInfo`, find its `callback` field's type
    (e.g. `WGPUFooCallback`). Returns None if not a recognised CallbackInfo."""
    if info_name not in struct_fields:
        return None
    for ft, fn in struct_fields[info_name]:
        ft_clean = ft.strip()
        if ft_clean in callbacks and fn == "callback":
            return ft_clean
    return None


def parse_type_aliases(src: str) -> dict[str, str]:
    """Map every WGPU* enum / flag / primitive-typedef name to its
    underlying C type. Handle types are excluded — those live in
    handle_types from parse_handle_types()."""
    out: dict[str, str] = {}
    for m in _RE_ENUM_TYPEDEF.finditer(src):
        out[m.group(1)] = "uint32_t"  # Dawn enums are always uint32_t-sized
    for m in _RE_FLAG_TYPEDEF.finditer(src):
        out[m.group(2)] = "uint64_t" if m.group(1) == "Flags64" else "uint32_t"
    for m in _RE_PRIM_TYPEDEF.finditer(src):
        out[m.group(2)] = m.group(1)
    return out


def parse_struct_names(src: str) -> set[str]:
    """Names of `typedef struct WGPUFoo { ... } WGPUFoo;` definitions —
    structs passed by value or by pointer. Used by the arg classifier
    to recognise that a bare WGPU* type is a struct, not a scalar."""
    return {m.group(1) for m in _RE_STRUCT_DEF.finditer(src)}


def parse_struct_fields(src: str) -> dict[str, list[tuple[str, str]]]:
    """Returns struct_name → [(field_type, field_name), ...] for every
    `typedef struct WGPUFoo { ... } WGPUFoo;` body. Used to classify
    whether a struct is POD (memcpy-able) for by-value wire serialise."""
    out: dict[str, list[tuple[str, str]]] = {}
    for m in _RE_STRUCT_DEF.finditer(src):
        name = m.group(1)
        # Find balanced closing brace.
        depth = 1
        i = m.end()
        while i < len(src) and depth > 0:
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
            i += 1
        body = src[m.end():i - 1]
        fields: list[tuple[str, str]] = []
        # Strip block comments before tokenising.
        body = re.sub(r"/\*.*?\*/", "", body, flags=re.DOTALL)
        for line in body.split("\n"):
            line = line.split("//")[0].strip()
            if not line.endswith(";"):
                continue
            decl = line[:-1].strip()
            if not decl:
                continue
            parts = decl.rsplit(maxsplit=1)
            if len(parts) != 2:
                continue
            ftype, fname = parts
            if fname.startswith("*"):
                ptrs = len(fname) - len(fname.lstrip("*"))
                fname = fname.lstrip("*")
                ftype = ftype + (" *" * ptrs)
            fields.append((ftype.strip(), fname))
        out[name] = fields
    return out


_POD_SCALARS = {
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t",  "int16_t",  "int32_t",  "int64_t",
    "float", "double", "size_t", "char",
    "WGPUBool", "WGPUFlags", "WGPUFlags64",
}


def classify_struct_fields(name: str, struct_fields: dict[str, list[tuple[str, str]]]
                           ) -> list[dict]:
    """Walk a struct's fields and classify each, including array-with-count
    detection. Returns a list of dicts:
        {'kind': str, 'ctype': str, 'fname': str, 'extra': {...}}

    Recognised kinds (within structs):
        'scalar'           plain C scalar / Dawn enum / flag
        'handle'           WGPUFoo opaque handle
        'struct_pod'       embedded by-value POD struct
        'struct_nonpod'    embedded by-value non-POD struct (recurse)
        'stringview'       WGPUStringView by value
        'next_in_chain'    `WGPUChainedStruct const *nextInChain` — skipped on wire
        'ptr_nullable'     `WGPU_NULLABLE WGPUFoo const *` (single struct)
        'array_count'      `size_t <X>Count` paired with the next field
        'array_items'      `WGPUFoo const *<X>` paired with the previous count
        'unsupported'      pointer / unknown type — struct can't be encoded

    Caller uses the field list to decide encodability and to drive the
    encoder/decoder emitters. classify_field() (separate function) is the
    arg-level classifier; this one knows the surrounding context (the
    full field list) and can detect array pairs."""
    fields = struct_fields.get(name, [])
    out: list[dict] = []
    skip_next = False
    for i, (ftype, fname) in enumerate(fields):
        if skip_next:
            skip_next = False
            continue
        traw = ftype.replace("WGPU_NULLABLE", "").strip()
        traw = " ".join(traw.split())
        # nextInChain is the canonical chained-extension slot. We always
        # skip it (write a u8 flag = 0). Recognising it by name keeps the
        # heuristic robust even if Dawn renames the type.
        if fname == "nextInChain":
            out.append({"kind": "next_in_chain", "ctype": traw, "fname": fname})
            continue
        # Array pair: this field is a `size_t/uint32_t <X>Count` and the
        # next field is a pointer with name closely matching X.
        if "*" not in traw and (traw.endswith("size_t") or
                                traw.endswith("uint32_t")) and fname.endswith("Count"):
            base = fname[:-len("Count")]
            if i + 1 < len(fields):
                nft, nfn = fields[i + 1]
                if "*" in nft and (nfn == base or nfn == base + "s" or
                                   nfn.lower() == base.lower() + "s" or
                                   nfn.lower() == base.lower()):
                    pointee = nft.replace("*", " ").replace("const", " ")
                    pointee = " ".join(pointee.split())
                    # `const char *const *` (array of C strings, e.g.
                    # WGPUDawnTogglesDescriptor::enabledToggles): the element
                    # is itself a pointer, so stripping all `*` collapses it to
                    # `char`. Detect the double indirection and mark it so the
                    # codec serialises each string's bytes rather than the
                    # (process-local, meaningless on the wire) pointer value.
                    items = {"kind": "array_items", "ctype": pointee, "fname": nfn,
                             "count_name": fname, "items_type": pointee}
                    if pointee == "char" and nft.count("*") >= 2:
                        items["string"] = True
                    out.append({"kind": "array_count", "ctype": traw, "fname": fname,
                                "items_name": nfn, "items_type": pointee})
                    out.append(items)
                    skip_next = True
                    continue
        # WGPUStringView by value.
        cleaned = traw.replace("const", "").strip()
        cleaned = " ".join(cleaned.split())
        if cleaned == "WGPUStringView":
            out.append({"kind": "stringview", "ctype": "WGPUStringView", "fname": fname})
            continue
        # WGPUChainedStruct by value — the chain header that lets a
        # struct sit on someone else's `nextInChain`. The outer chain
        # walker handles `.next` and `.sType`; the inner encoder/decoder
        # skips this field entirely.
        if cleaned == "WGPUChainedStruct":
            out.append({"kind": "embedded_chain", "ctype": cleaned, "fname": fname})
            continue
        # By-value CallbackInfo — function pointer + opaque userdata
        # don't survive a process hop. We skip on the wire and zero on
        # decode; if the surrounding method actually needs callbacks,
        # it goes through the (cb, user) pair the codegen injects on
        # methods carrying a CallbackInfo *arg*.
        if cleaned.endswith("CallbackInfo") and cleaned in struct_fields:
            out.append({"kind": "embedded_callback_info", "ctype": cleaned,
                        "fname": fname})
            continue
        if "*" in cleaned:
            pointee = cleaned.replace("*", " ").strip()
            pointee = " ".join(pointee.split())
            # Pointer field with a known constant length (dawn.json says
            # 9, 12, etc.). We emit a present flag + N elements on the
            # wire and keep encode/decode shape independent of the
            # implicit count.
            n = FIXED_LEN_PTR_FIELDS.get((name, fname))
            if n is not None:
                out.append({"kind": "fixed_array", "ctype": pointee,
                            "fname": fname, "length": n})
                continue
            # Pointer field that piggy-backs on another count field in
            # the same struct (multi-array sharing one length).
            shared = SHARED_COUNT_PTR_FIELDS.get((name, fname))
            if shared is not None:
                out.append({"kind": "array_items", "ctype": pointee,
                            "fname": fname, "count_name": shared,
                            "items_type": pointee})
                continue
            out.append({"kind": "ptr_nullable", "ctype": pointee, "fname": fname})
            continue
        # Defer scalar/handle/struct classification — done in
        # is_encodable_struct/emit_struct_encoder which know
        # handle_types, aliases, pod_structs, encodable.
        out.append({"kind": "raw", "ctype": cleaned, "fname": fname})
    return out


def field_kind_in_ctx(field: dict, handle_types: set[str], aliases: dict[str, str],
                      pod_structs: set[str], struct_names: set[str],
                      encodable: set[str]) -> str:
    """Refine the field's preliminary kind once we know which structs are
    encodable. Returns the same kinds as classify_struct_fields with
    'raw' resolved into 'scalar' / 'handle' / 'struct_pod' /
    'struct_nonpod' / 'unsupported'."""
    if field["kind"] != "raw":
        # Pre-classified by classify_struct_fields. ptr_nullable refines
        # against encodability of pointee.
        if field["kind"] == "ptr_nullable":
            t = field["ctype"]
            if t in pod_structs:
                return "ptr_pod"
            if t in encodable:
                return "ptr_nonpod"
            if t in handle_types:
                return "unsupported"  # rare; WGPU_NULLABLE handle is taken as scalar=0 above
            return "unsupported"
        if field["kind"] == "fixed_array":
            t = field["ctype"]
            if t in handle_types:
                return "fixed_array_handle"
            if t in pod_structs:
                return "fixed_array_pod"
            if t in _POD_SCALARS or t in aliases:
                return "fixed_array_scalar"
            return "unsupported"
        if field["kind"] == "array_items":
            if field.get("string"):
                return "array_string"
            t = field["items_type"]
            if t in handle_types:
                return "array_handle"
            if t in pod_structs:
                return "array_pod"
            if t in encodable:
                return "array_nonpod"
            if t in aliases or t in _POD_SCALARS:
                return "array_scalar"
            return "unsupported"
        return field["kind"]
    t = field["ctype"]
    if t in handle_types:
        return "handle"
    if t in _POD_SCALARS or t in aliases:
        return "scalar"
    if t in pod_structs:
        return "struct_pod"
    if t in encodable:
        return "struct_nonpod"
    if t in struct_names:
        return "unsupported"  # would-be encodable but a transitive field is bad
    return "unsupported"


def is_encodable_struct(name: str, struct_fields: dict[str, list[tuple[str, str]]],
                        handle_types: set[str], aliases: dict[str, str],
                        pod_structs: set[str], struct_names: set[str],
                        callback_infos: set[str],
                        cache: dict[str, bool], depth: int = 0) -> bool:
    """Whether we can emit encode/decode for this struct. Tentative-true
    during recursion to break cycles."""
    if name in cache:
        return cache[name]
    if depth > 8 or name not in struct_fields:
        cache[name] = False
        return False
    cache[name] = True  # tentative
    ok = True
    field_dicts = classify_struct_fields(name, struct_fields)
    for f in field_dicts:
        # Resolve transitively.
        if f["kind"] == "raw":
            t = f["ctype"]
            if t in handle_types or t in _POD_SCALARS or t in aliases or t in pod_structs:
                continue
            if t in struct_names:
                if not is_encodable_struct(t, struct_fields, handle_types, aliases,
                                           pod_structs, struct_names, callback_infos,
                                           cache, depth + 1):
                    ok = False
                    break
                continue
            ok = False
            break
        if f["kind"] == "ptr_nullable":
            t = f["ctype"]
            if t in pod_structs:
                continue
            if t in struct_names:
                if not is_encodable_struct(t, struct_fields, handle_types, aliases,
                                           pod_structs, struct_names, callback_infos,
                                           cache, depth + 1):
                    ok = False
                    break
                continue
            ok = False
            break
        if f["kind"] == "array_items":
            if f.get("string"):
                continue
            t = f["items_type"]
            if t in handle_types or t in pod_structs or t in _POD_SCALARS or t in aliases:
                continue
            if t in struct_names:
                if not is_encodable_struct(t, struct_fields, handle_types, aliases,
                                           pod_structs, struct_names, callback_infos,
                                           cache, depth + 1):
                    ok = False
                    break
                continue
            ok = False
            break
        if f["kind"] == "fixed_array":
            t = f["ctype"]
            if t in _POD_SCALARS or t in aliases or t in pod_structs or t in handle_types:
                continue
            ok = False
            break
        if f["kind"] in ("array_count", "stringview", "next_in_chain",
                         "embedded_chain", "embedded_callback_info"):
            continue
        # any other unknown
        ok = False
        break
    cache[name] = ok
    return ok


def emit_struct_encoder_decoder(name: str, struct_fields: dict[str, list[tuple[str, str]]],
                                handle_types: set[str], aliases: dict[str, str],
                                pod_structs: set[str], struct_names: set[str],
                                encodable: set[str]) -> tuple[list[str], list[str]]:
    """Return (encoder_lines, decoder_lines) for a single struct."""
    fields = classify_struct_fields(name, struct_fields)

    enc: list[str] = []
    enc.append(f"struct yetty_ycore_void_result yrdawn_encode_{name}(const {name} *src, "
               f"struct yetty_ycore_buffer *out);")

    dec: list[str] = []
    dec.append(f"int yrdawn_decode_{name}(void *ctx, const uint8_t **src, size_t *rem, "
               f"{name} *out, struct yrdawn_arena *arena);")
    return enc, dec  # just declarations; bodies emitted by emit_struct_codec_body


def emit_struct_codec_body(name: str, struct_fields: dict[str, list[tuple[str, str]]],
                           handle_types: set[str], aliases: dict[str, str],
                           pod_structs: set[str], struct_names: set[str],
                           encodable: set[str],
                           chainable: dict[str, str] | None = None) -> list[str]:
    chainable = chainable or {}
    fields = classify_struct_fields(name, struct_fields)
    body: list[str] = []

    # ----- encoder ----------------------------------------------------
    # Each serialised write returns a Result; we bail on the first failed
    # append rather than absorbing it, so a buffer-grow failure surfaces to
    # the caller instead of silently truncating the wire frame. `wresult` /
    # `eresult` are block-scoped so the same name can repeat per field.
    def write_prop(val_expr: str, size_expr: str, indent: str = "    ") -> None:
        body.append(f"{indent}{{ struct yetty_ycore_void_result wresult = "
                    f"yetty_ycore_buffer_write(out, {val_expr}, {size_expr}); "
                    f"YETTY_RETURN_IF_ERR(yetty_ycore_void, wresult, "
                    f"\"yrdawn_encode_{name}\"); }}")

    def encode_prop(call_expr: str, indent: str = "    ") -> None:
        body.append(f"{indent}{{ struct yetty_ycore_void_result eresult = {call_expr}; "
                    f"YETTY_RETURN_IF_ERR(yetty_ycore_void, eresult, "
                    f"\"yrdawn_encode_{name}\"); }}")

    body.append(f"struct yetty_ycore_void_result yrdawn_encode_{name}(const {name} *src,")
    body.append("                                  struct yetty_ycore_buffer *out)")
    body.append("{")
    for f in fields:
        k = field_kind_in_ctx(f, handle_types, aliases, pod_structs, struct_names, encodable)
        fn = f["fname"]
        if k in ("scalar", "handle", "struct_pod"):
            write_prop(f"&src->{fn}", f"sizeof(src->{fn})")
        elif k == "struct_nonpod":
            encode_prop(f"yrdawn_encode_{f['ctype']}(&src->{fn}, out)")
        elif k == "stringview":
            body.append("    {")
            body.append(f"        uint64_t _len = (src->{fn}.length == WGPU_STRLEN || "
                        f"!src->{fn}.data)")
            body.append(f"            ? (src->{fn}.data ? (uint64_t)strlen(src->{fn}.data) : 0u)")
            body.append(f"            : (uint64_t)src->{fn}.length;")
            write_prop("&_len", "sizeof(_len)", indent="        ")
            body.append(f"        if (_len > 0 && src->{fn}.data) {{")
            write_prop(f"src->{fn}.data", "(size_t)_len", indent="            ")
            body.append("        }")
            body.append("    }")
        elif k == "next_in_chain":
            # Walk the chain. Each link: tag=1 (1 byte) + sType (4 bytes)
            # + payload length (4 bytes, set after encode) + payload bytes.
            # Terminate with tag=0. Each known sType encodes via the
            # matching struct's full encoder; the encoder skips the
            # struct's `WGPUChainedStruct chain` header (classified as
            # embedded_chain) so we don't write sType twice.
            body.append("    {")
            body.append(f"        const WGPUChainedStruct *_ch = src->{fn};")
            body.append("        while (_ch) {")
            body.append("            uint8_t _tag = 1;")
            write_prop("&_tag", "1", indent="            ")
            body.append("            uint32_t _st = (uint32_t)_ch->sType;")
            write_prop("&_st", "sizeof(_st)", indent="            ")
            body.append("            size_t _len_off = out->size;")
            body.append("            uint32_t _len_placeholder = 0;")
            write_prop("&_len_placeholder", "sizeof(_len_placeholder)", indent="            ")
            body.append("            size_t _body_off = out->size;")
            body.append("            switch (_st) {")
            for stype, struct in sorted(chainable.items()):
                body.append(f"            case (uint32_t){stype}:")
                encode_prop(f"yrdawn_encode_{struct}((const {struct} *)_ch, out)",
                            indent="                ")
                body.append("                break;")
            body.append("            default: break;")
            body.append("            }")
            body.append("            uint32_t _len = (uint32_t)(out->size - _body_off);")
            body.append("            if (out->data) memcpy((uint8_t *)out->data + _len_off, &_len, sizeof(_len));")
            body.append("            _ch = _ch->next;")
            body.append("        }")
            body.append("        uint8_t _term = 0;")
            write_prop("&_term", "1", indent="        ")
            body.append("    }")
        elif k == "embedded_chain":
            # Header for an extension struct sitting in someone else's
            # nextInChain. The outer walker wrote (tag, sType, length);
            # the body skips this field. Nothing on the wire.
            pass
        elif k == "embedded_callback_info":
            # Function pointer + opaque userdata can't survive a process
            # hop. Skip on the wire; the matching decoder zeroes the
            # field so server-side Dawn sees an empty CallbackInfo.
            pass
        elif k in ("ptr_pod", "ptr_nonpod"):
            body.append("    {")
            body.append(f"        uint8_t _present = src->{fn} ? 1u : 0u;")
            write_prop("&_present", "1", indent="        ")
            body.append(f"        if (src->{fn}) {{")
            if k == "ptr_pod":
                write_prop(f"src->{fn}", f"sizeof(*src->{fn})", indent="            ")
            else:
                encode_prop(f"yrdawn_encode_{f['ctype']}(src->{fn}, out)", indent="            ")
            body.append("        }")
            body.append("    }")
        elif k == "array_count":
            body.append(f"    {{ uint64_t _c = (uint64_t)src->{fn};")
            write_prop("&_c", "sizeof(_c)", indent="      ")
            body.append("    }")
        elif k in ("array_handle", "array_pod", "array_scalar"):
            cnt = f["count_name"]
            body.append(f"    for (size_t _i = 0; _i < (size_t)src->{cnt}; ++_i) {{")
            write_prop(f"&src->{fn}[_i]", f"sizeof(src->{fn}[_i])", indent="        ")
            body.append("    }")
        elif k == "array_nonpod":
            cnt = f["count_name"]
            body.append(f"    for (size_t _i = 0; _i < (size_t)src->{cnt}; ++_i) {{")
            encode_prop(f"yrdawn_encode_{f['items_type']}(&src->{fn}[_i], out)", indent="        ")
            body.append("    }")
        elif k == "array_string":
            # Array of C strings: each element is a NUL-terminated pointer.
            # Serialise length + bytes per string (the pointer value itself
            # is process-local and useless across the wire).
            cnt = f["count_name"]
            body.append(f"    for (size_t _i = 0; _i < (size_t)src->{cnt}; ++_i) {{")
            body.append(f"        const char *_s = src->{fn}[_i];")
            body.append("        uint64_t _slen = _s ? (uint64_t)strlen(_s) : 0u;")
            write_prop("&_slen", "sizeof(_slen)", indent="        ")
            body.append("        if (_slen > 0) {")
            write_prop("_s", "(size_t)_slen", indent="            ")
            body.append("        }")
            body.append("    }")
        elif k in ("fixed_array_scalar", "fixed_array_pod", "fixed_array_handle"):
            # 1-byte present flag + N elements (count from the IDL,
            # baked into the codec). Handles NULL pointers cleanly so
            # nullable Dawn fields round-trip.
            n = f["length"]
            elem_t = f["ctype"]
            body.append(f"    {{ uint8_t _p = src->{fn} ? 1u : 0u;")
            write_prop("&_p", "1", indent="      ")
            body.append("    }")
            body.append(f"    if (src->{fn}) {{")
            write_prop(f"src->{fn}", f"sizeof({elem_t}) * {n}", indent="        ")
            body.append("    }")
        else:
            body.append(f"    /* unsupported field {fn}: kind={k} */")
    body.append("    return YETTY_OK_VOID();")
    body.append("}")
    body.append("")

    # ----- decoder ----------------------------------------------------
    # The server decoder takes `ctx` so it can resolve handle fields
    # through the layer's handle table — without that, fields like
    # WGPURenderPipelineDescriptor.vertex.module arrive as raw u64
    # tokens and Dawn dereferences garbage when it sees them as
    # WGPUShaderModule pointers.
    body.append(f"int yrdawn_decode_{name}(void *ctx, const uint8_t **src, size_t *rem,")
    body.append(f"                                {name} *out, struct yrdawn_arena *arena)")
    body.append("{")
    body.append("    (void)arena; (void)ctx;")
    for f in fields:
        k = field_kind_in_ctx(f, handle_types, aliases, pod_structs, struct_names, encodable)
        fn = f["fname"]
        if k == "handle":
            # WGPU* pointer field — wire carries the u64 token; resolve
            # through the handle table so Dawn sees its own pointer.
            ctype = f["ctype"]
            body.append("    {")
            body.append("        uint64_t _h = 0;")
            body.append("        if (*rem < sizeof(_h)) return 0;")
            body.append("        memcpy(&_h, *src, sizeof(_h));")
            body.append("        *src += sizeof(_h); *rem -= sizeof(_h);")
            body.append(f"        out->{fn} = ({ctype})yrdawn_server_handle_get(ctx, _h);")
            body.append("    }")
        elif k in ("scalar", "struct_pod"):
            body.append(f"    if (*rem < sizeof(out->{fn})) return 0;")
            body.append(f"    memcpy(&out->{fn}, *src, sizeof(out->{fn}));")
            body.append(f"    *src += sizeof(out->{fn}); *rem -= sizeof(out->{fn});")
        elif k == "struct_nonpod":
            body.append(f"    if (!yrdawn_decode_{f['ctype']}(ctx, src, rem, &out->{fn}, arena)) return 0;")
        elif k == "stringview":
            body.append("    {")
            body.append("        uint64_t _len = 0;")
            body.append("        if (*rem < sizeof(_len)) return 0;")
            body.append("        memcpy(&_len, *src, sizeof(_len));")
            body.append("        *src += sizeof(_len); *rem -= sizeof(_len);")
            body.append("        if (_len > *rem) return 0;")
            body.append(f"        if (_len == 0) {{")
            body.append(f"            out->{fn}.data = NULL; out->{fn}.length = 0;")
            body.append("        } else {")
            body.append("            char *_buf = (char *)yrdawn_arena_alloc(arena, (size_t)_len + 1u);")
            body.append("            if (!_buf) return 0;")
            body.append("            memcpy(_buf, *src, (size_t)_len);")
            body.append("            _buf[_len] = '\\0';")
            body.append(f"            out->{fn}.data = _buf;")
            body.append(f"            out->{fn}.length = (size_t)_len;")
            body.append("            *src += _len; *rem -= (size_t)_len;")
            body.append("        }")
            body.append("    }")
        elif k == "next_in_chain":
            # Read chain links: tag (1) + sType (4) + length (4) + body.
            # Known sTypes get arena-allocated and decoded via the
            # matching struct's full decoder (which skips its embedded
            # `chain` field). Unknown sTypes skip `length` bytes.
            body.append("    {")
            body.append(f"        out->{fn} = NULL;")
            body.append(f"        WGPUChainedStruct **_pp = (WGPUChainedStruct **)&out->{fn};")
            body.append("        while (1) {")
            body.append("            if (*rem < 1) return 0;")
            body.append("            uint8_t _tag = **src; *src += 1; *rem -= 1;")
            body.append("            if (_tag == 0) break;")
            body.append("            if (*rem < sizeof(uint32_t) + sizeof(uint32_t)) return 0;")
            body.append("            uint32_t _st; memcpy(&_st, *src, sizeof(_st)); *src += sizeof(_st); *rem -= sizeof(_st);")
            body.append("            uint32_t _bl; memcpy(&_bl, *src, sizeof(_bl)); *src += sizeof(_bl); *rem -= sizeof(_bl);")
            body.append("            if (_bl > *rem) return 0;")
            body.append("            const uint8_t *_body_p = *src; size_t _body_rem = _bl;")
            body.append("            switch (_st) {")
            for stype, struct in sorted(chainable.items()):
                body.append(f"            case (uint32_t){stype}: {{")
                body.append(f"                {struct} *_link = ({struct} *)yrdawn_arena_alloc(arena, sizeof(*_link));")
                body.append( "                if (!_link) return 0;")
                body.append(f"                memset(_link, 0, sizeof(*_link));")
                body.append(f"                _link->chain.sType = {stype};")
                body.append(f"                if (!yrdawn_decode_{struct}(ctx, &_body_p, &_body_rem, _link, arena)) return 0;")
                body.append( "                *_pp = (WGPUChainedStruct *)_link; _pp = &(*_pp)->next;")
                body.append( "                break;")
                body.append( "            }")
            body.append("            default: /* unknown sType: skip body */ break;")
            body.append("            }")
            body.append("            *src += _bl; *rem -= _bl;")
            body.append("        }")
            body.append("    }")
        elif k == "embedded_chain":
            # Outer walker already wrote (tag, sType, length); skip here.
            body.append(f"    out->{fn}.next = NULL;")
        elif k == "embedded_callback_info":
            # Wire carries nothing; zero on decode so Dawn sees an
            # empty CallbackInfo. Surrounding method handles real
            # callbacks via the (cb, user) pair if needed.
            body.append(f"    memset(&out->{fn}, 0, sizeof(out->{fn}));")
        elif k in ("ptr_pod", "ptr_nonpod"):
            body.append("    {")
            body.append("        uint8_t _present = 0;")
            body.append("        if (*rem < 1) return 0;")
            body.append("        _present = **src; *src += 1; *rem -= 1;")
            body.append("        if (_present) {")
            body.append(f"            {f['ctype']} *_p = "
                        f"({f['ctype']} *)yrdawn_arena_alloc(arena, sizeof({f['ctype']}));")
            body.append("            if (!_p) return 0;")
            if k == "ptr_pod":
                body.append("            if (*rem < sizeof(*_p)) return 0;")
                body.append("            memcpy(_p, *src, sizeof(*_p));")
                body.append("            *src += sizeof(*_p); *rem -= sizeof(*_p);")
            else:
                body.append(f"            if (!yrdawn_decode_{f['ctype']}(ctx, src, rem, _p, arena)) return 0;")
            body.append(f"            out->{fn} = _p;")
            body.append("        } else {")
            body.append(f"            out->{fn} = NULL;")
            body.append("        }")
            body.append("    }")
        elif k == "array_count":
            body.append("    {")
            body.append("        uint64_t _c = 0;")
            body.append("        if (*rem < sizeof(_c)) return 0;")
            body.append("        memcpy(&_c, *src, sizeof(_c));")
            body.append("        *src += sizeof(_c); *rem -= sizeof(_c);")
            cn_ctype = f["ctype"].split()[-1]
            body.append(f"        out->{fn} = ({cn_ctype})_c;")
            body.append("    }")
        elif k in ("array_handle", "array_pod", "array_scalar", "array_nonpod"):
            cnt = f["count_name"]
            it_t = f["items_type"]
            body.append("    {")
            body.append(f"        size_t _n = (size_t)out->{cnt};")
            body.append(f"        {it_t} *_arr = NULL;")
            body.append("        if (_n > 0) {")
            body.append(f"            _arr = ({it_t} *)yrdawn_arena_alloc(arena, _n * sizeof({it_t}));")
            body.append("            if (!_arr) return 0;")
            body.append("        }")
            body.append("        for (size_t _i = 0; _i < _n; ++_i) {")
            if k == "array_nonpod":
                body.append(f"            if (!yrdawn_decode_{it_t}(ctx, src, rem, &_arr[_i], arena)) return 0;")
            elif k == "array_handle":
                # Resolve every handle through the table — same reason as
                # the scalar-handle field above.
                body.append("            if (*rem < sizeof(uint64_t)) return 0;")
                body.append("            uint64_t _h; memcpy(&_h, *src, sizeof(_h));")
                body.append("            *src += sizeof(_h); *rem -= sizeof(_h);")
                body.append(f"            _arr[_i] = ({it_t})yrdawn_server_handle_get(ctx, _h);")
            else:
                body.append("            if (*rem < sizeof(_arr[_i])) return 0;")
                body.append("            memcpy(&_arr[_i], *src, sizeof(_arr[_i]));")
                body.append("            *src += sizeof(_arr[_i]); *rem -= sizeof(_arr[_i]);")
            body.append("        }")
            body.append(f"        out->{fn} = _arr;")
            body.append("    }")
        elif k == "array_string":
            # Array of C strings, arena-allocated (the whole arena is freed
            # wholesale after dispatch, so no per-string free here).
            cnt = f["count_name"]
            body.append("    {")
            body.append(f"        size_t _n = (size_t)out->{cnt};")
            body.append("        const char **_arr = NULL;")
            body.append("        if (_n > 0) {")
            body.append("            _arr = (const char **)yrdawn_arena_alloc("
                        "arena, _n * sizeof(const char *));")
            body.append("            if (!_arr) return 0;")
            body.append("        }")
            body.append("        for (size_t _i = 0; _i < _n; ++_i) {")
            body.append("            uint64_t _slen = 0;")
            body.append("            if (*rem < sizeof(_slen)) return 0;")
            body.append("            memcpy(&_slen, *src, sizeof(_slen)); "
                        "*src += sizeof(_slen); *rem -= sizeof(_slen);")
            body.append("            if (_slen > *rem) return 0;")
            body.append("            char *_s = (char *)yrdawn_arena_alloc(arena, (size_t)_slen + 1u);")
            body.append("            if (!_s) return 0;")
            body.append("            memcpy(_s, *src, (size_t)_slen); _s[_slen] = '\\0';")
            body.append("            *src += _slen; *rem -= (size_t)_slen;")
            body.append("            _arr[_i] = _s;")
            body.append("        }")
            body.append(f"        out->{fn} = (const char *const *)_arr;")
            body.append("    }")
        elif k == "fixed_array_handle":
            n = f["length"]
            elem_t = f["ctype"]
            body.append("    {")
            body.append("        if (*rem < 1) return 0;")
            body.append("        uint8_t _p = **src; *src += 1; *rem -= 1;")
            body.append("        if (_p) {")
            body.append(f"            size_t _nb = sizeof({elem_t}) * {n};")
            body.append(f"            if (*rem < _nb) return 0;")
            body.append(f"            {elem_t} *_arr = ({elem_t} *)yrdawn_arena_alloc(arena, _nb);")
            body.append("            if (!_arr) return 0;")
            body.append(f"            for (size_t _i = 0; _i < {n}; ++_i) {{")
            body.append("                uint64_t _h; memcpy(&_h, (const uint8_t *)(*src) + _i * sizeof(uint64_t), sizeof(_h));")
            body.append(f"                _arr[_i] = ({elem_t})yrdawn_server_handle_get(ctx, _h);")
            body.append("            }")
            body.append("            *src += _nb; *rem -= _nb;")
            body.append(f"            out->{fn} = _arr;")
            body.append(f"        }} else {{ out->{fn} = NULL; }}")
            body.append("    }")
        elif k in ("fixed_array_scalar", "fixed_array_pod"):
            n = f["length"]
            elem_t = f["ctype"]
            body.append("    {")
            body.append("        if (*rem < 1) return 0;")
            body.append("        uint8_t _p = **src; *src += 1; *rem -= 1;")
            body.append(f"        if (_p) {{")
            body.append(f"            size_t _nb = sizeof({elem_t}) * {n};")
            body.append(f"            if (*rem < _nb) return 0;")
            body.append(f"            {elem_t} *_arr = ({elem_t} *)yrdawn_arena_alloc(arena, _nb);")
            body.append(f"            if (!_arr) return 0;")
            body.append(f"            memcpy(_arr, *src, _nb); *src += _nb; *rem -= _nb;")
            body.append(f"            out->{fn} = _arr;")
            body.append(f"        }} else {{ out->{fn} = NULL; }}")
            body.append("    }")
        else:
            body.append(f"    /* unsupported field {fn}: kind={k} */ return 0;")
    body.append("    return 1;")
    body.append("}")
    body.append("")
    return body


def emit_client_struct_decoder_body(name: str, struct_fields, handle_types, aliases,
                                    pod_structs, struct_names, encodable,
                                    chainable: dict[str, str] | None = None) -> list[str]:
    chainable = chainable or {}
    """Mirror of the server arena-based decoder, but uses malloc for
    inner pointers. The companion yrdawn_client_free_<Name> walks the
    same shape and free()s every allocation."""
    fields = classify_struct_fields(name, struct_fields)
    body: list[str] = []
    body.append(f"int yrdawn_client_decode_{name}(const uint8_t **src, size_t *rem,")
    body.append(f"                                {name} *out)")
    body.append("{")
    for f in fields:
        k = field_kind_in_ctx(f, handle_types, aliases, pod_structs, struct_names, encodable)
        fn = f["fname"]
        if k in ("scalar", "handle", "struct_pod"):
            body.append(f"    if (*rem < sizeof(out->{fn})) return 0;")
            body.append(f"    memcpy(&out->{fn}, *src, sizeof(out->{fn}));")
            body.append(f"    *src += sizeof(out->{fn}); *rem -= sizeof(out->{fn});")
        elif k == "struct_nonpod":
            body.append(f"    if (!yrdawn_client_decode_{f['ctype']}(src, rem, &out->{fn})) return 0;")
        elif k == "stringview":
            body.append("    {")
            body.append("        uint64_t _len = 0;")
            body.append("        if (*rem < sizeof(_len)) return 0;")
            body.append("        memcpy(&_len, *src, sizeof(_len));")
            body.append("        *src += sizeof(_len); *rem -= sizeof(_len);")
            body.append("        if (_len > *rem) return 0;")
            body.append(f"        if (_len == 0) {{ out->{fn}.data = NULL; out->{fn}.length = 0; }}")
            body.append("        else {")
            body.append("            char *_buf = (char *)malloc((size_t)_len + 1u);")
            body.append("            if (!_buf) return 0;")
            body.append("            memcpy(_buf, *src, (size_t)_len);")
            body.append("            _buf[_len] = '\\0';")
            body.append(f"            out->{fn}.data = _buf; out->{fn}.length = (size_t)_len;")
            body.append("            *src += _len; *rem -= (size_t)_len;")
            body.append("        }")
            body.append("    }")
        elif k == "next_in_chain":
            # Mirror of the server decoder, but uses malloc instead of
            # arena. Each known sType decodes via the matching client
            # decoder (which skips its embedded `chain` field). Unknown
            # sTypes skip their (length-prefixed) body.
            body.append("    {")
            body.append(f"        out->{fn} = NULL;")
            body.append(f"        WGPUChainedStruct **_pp = (WGPUChainedStruct **)&out->{fn};")
            body.append("        while (1) {")
            body.append("            if (*rem < 1) return 0;")
            body.append("            uint8_t _tag = **src; *src += 1; *rem -= 1;")
            body.append("            if (_tag == 0) break;")
            body.append("            if (*rem < sizeof(uint32_t) + sizeof(uint32_t)) return 0;")
            body.append("            uint32_t _st; memcpy(&_st, *src, sizeof(_st)); *src += sizeof(_st); *rem -= sizeof(_st);")
            body.append("            uint32_t _bl; memcpy(&_bl, *src, sizeof(_bl)); *src += sizeof(_bl); *rem -= sizeof(_bl);")
            body.append("            if (_bl > *rem) return 0;")
            body.append("            const uint8_t *_body_p = *src; size_t _body_rem = _bl;")
            body.append("            switch (_st) {")
            for stype, struct in sorted(chainable.items()):
                body.append(f"            case (uint32_t){stype}: {{")
                body.append(f"                {struct} *_link = ({struct} *)calloc(1, sizeof(*_link));")
                body.append( "                if (!_link) return 0;")
                body.append(f"                _link->chain.sType = {stype};")
                body.append(f"                if (!yrdawn_client_decode_{struct}(&_body_p, &_body_rem, _link)) {{ free(_link); return 0; }}")
                body.append( "                *_pp = (WGPUChainedStruct *)_link; _pp = &(*_pp)->next;")
                body.append( "                break;")
                body.append( "            }")
            body.append("            default: /* unknown sType: skip body */ break;")
            body.append("            }")
            body.append("            *src += _bl; *rem -= _bl;")
            body.append("        }")
            body.append("    }")
        elif k == "embedded_chain":
            body.append(f"    out->{fn}.next = NULL;")
        elif k == "embedded_callback_info":
            body.append(f"    memset(&out->{fn}, 0, sizeof(out->{fn}));")
        elif k in ("ptr_pod", "ptr_nonpod"):
            body.append("    {")
            body.append("        uint8_t _present = 0;")
            body.append("        if (*rem < 1) return 0;")
            body.append("        _present = **src; *src += 1; *rem -= 1;")
            body.append("        if (_present) {")
            body.append(f"            {f['ctype']} *_p = ({f['ctype']} *)calloc(1, sizeof({f['ctype']}));")
            body.append("            if (!_p) return 0;")
            if k == "ptr_pod":
                body.append("            if (*rem < sizeof(*_p)) { free(_p); return 0; }")
                body.append("            memcpy(_p, *src, sizeof(*_p));")
                body.append("            *src += sizeof(*_p); *rem -= sizeof(*_p);")
            else:
                body.append(f"            if (!yrdawn_client_decode_{f['ctype']}(src, rem, _p)) "
                            "{ free(_p); return 0; }")
            body.append(f"            out->{fn} = _p;")
            body.append(f"        }} else {{ out->{fn} = NULL; }}")
            body.append("    }")
        elif k == "array_count":
            body.append("    {")
            body.append("        uint64_t _c = 0;")
            body.append("        if (*rem < sizeof(_c)) return 0;")
            body.append("        memcpy(&_c, *src, sizeof(_c));")
            body.append("        *src += sizeof(_c); *rem -= sizeof(_c);")
            cn_ctype = f["ctype"].split()[-1]
            body.append(f"        out->{fn} = ({cn_ctype})_c;")
            body.append("    }")
        elif k in ("array_handle", "array_pod", "array_scalar", "array_nonpod"):
            cnt = f["count_name"]
            it_t = f["items_type"]
            body.append("    {")
            body.append(f"        size_t _n = (size_t)out->{cnt};")
            body.append(f"        {it_t} *_arr = NULL;")
            body.append("        if (_n > 0) {")
            body.append(f"            _arr = ({it_t} *)calloc(_n, sizeof({it_t}));")
            body.append("            if (!_arr) return 0;")
            body.append("        }")
            body.append("        for (size_t _i = 0; _i < _n; ++_i) {")
            if k == "array_nonpod":
                body.append(f"            if (!yrdawn_client_decode_{it_t}(src, rem, &_arr[_i])) "
                            "{ free(_arr); return 0; }")
            else:
                body.append("            if (*rem < sizeof(_arr[_i])) { free(_arr); return 0; }")
                body.append("            memcpy(&_arr[_i], *src, sizeof(_arr[_i]));")
                body.append("            *src += sizeof(_arr[_i]); *rem -= sizeof(_arr[_i]);")
            body.append("        }")
            body.append(f"        out->{fn} = _arr;")
            body.append("    }")
        elif k == "array_string":
            # Array of C strings, malloc'd (freed element-by-element in the
            # matching freer). Mirrors the server arena decoder.
            cnt = f["count_name"]
            body.append("    {")
            body.append(f"        size_t _n = (size_t)out->{cnt};")
            body.append("        const char **_arr = NULL;")
            body.append("        if (_n > 0) {")
            body.append("            _arr = (const char **)calloc(_n, sizeof(const char *));")
            body.append("            if (!_arr) return 0;")
            body.append("        }")
            body.append("        for (size_t _i = 0; _i < _n; ++_i) {")
            body.append("            uint64_t _slen = 0;")
            body.append("            if (*rem < sizeof(_slen)) { free(_arr); return 0; }")
            body.append("            memcpy(&_slen, *src, sizeof(_slen)); "
                        "*src += sizeof(_slen); *rem -= sizeof(_slen);")
            body.append("            if (_slen > *rem) { free(_arr); return 0; }")
            body.append("            char *_s = (char *)malloc((size_t)_slen + 1u);")
            body.append("            if (!_s) { free(_arr); return 0; }")
            body.append("            memcpy(_s, *src, (size_t)_slen); _s[_slen] = '\\0';")
            body.append("            *src += _slen; *rem -= (size_t)_slen;")
            body.append("            _arr[_i] = _s;")
            body.append("        }")
            body.append(f"        out->{fn} = (const char *const *)_arr;")
            body.append("    }")
        elif k in ("fixed_array_scalar", "fixed_array_pod", "fixed_array_handle"):
            n = f["length"]
            elem_t = f["ctype"]
            body.append("    {")
            body.append("        if (*rem < 1) return 0;")
            body.append("        uint8_t _p = **src; *src += 1; *rem -= 1;")
            body.append(f"        if (_p) {{")
            body.append(f"            size_t _nb = sizeof({elem_t}) * {n};")
            body.append(f"            if (*rem < _nb) return 0;")
            body.append(f"            {elem_t} *_arr = ({elem_t} *)malloc(_nb);")
            body.append(f"            if (!_arr) return 0;")
            body.append(f"            memcpy(_arr, *src, _nb); *src += _nb; *rem -= _nb;")
            body.append(f"            out->{fn} = _arr;")
            body.append(f"        }} else {{ out->{fn} = NULL; }}")
            body.append("    }")
        else:
            body.append("    return 0;  /* unsupported field */")
    body.append("    return 1;")
    body.append("}")
    body.append("")
    return body


def emit_client_struct_freer_body(name: str, struct_fields, handle_types, aliases,
                                  pod_structs, struct_names, encodable,
                                  chainable: dict[str, str] | None = None) -> list[str]:
    chainable = chainable or {}
    """Walk the struct and free every malloc'd inner pointer that the
    client decoder produced. Mirrors the decoder's allocation pattern."""
    fields = classify_struct_fields(name, struct_fields)
    body: list[str] = []
    body.append(f"void yrdawn_client_free_{name}({name} *s)")
    body.append("{")
    body.append("    if (!s) return;")
    for f in fields:
        k = field_kind_in_ctx(f, handle_types, aliases, pod_structs, struct_names, encodable)
        fn = f["fname"]
        if k == "stringview":
            body.append(f"    if (s->{fn}.data) {{ free((void *)s->{fn}.data); "
                        f"s->{fn}.data = NULL; s->{fn}.length = 0; }}")
        elif k == "struct_nonpod":
            body.append(f"    yrdawn_client_free_{f['ctype']}(&s->{fn});")
        elif k in ("ptr_pod", "ptr_nonpod"):
            # The field may be a `const WGPUFoo *` in webgpu.h, but the
            # client decoder malloc'd the storage, so freeing it (and
            # recursing into the nested freer that writes through it) is
            # correct — cast the const away for both.
            if k == "ptr_nonpod":
                body.append(f"    if (s->{fn}) yrdawn_client_free_{f['ctype']}(({f['ctype']} *)s->{fn});")
            body.append(f"    free((void *)s->{fn}); s->{fn} = NULL;")
        elif k in ("array_handle", "array_pod", "array_scalar"):
            cnt = f["count_name"]
            body.append(f"    if (s->{fn}) {{ "
                        f"(void)s->{cnt}; "
                        f"free((void *)s->{fn}); s->{fn} = NULL; }}")
        elif k == "array_nonpod":
            cnt = f["count_name"]
            it_t = f["items_type"]
            body.append(f"    if (s->{fn}) {{")
            body.append(f"        for (size_t _i = 0; _i < (size_t)s->{cnt}; ++_i)")
            body.append(f"            yrdawn_client_free_{it_t}((({it_t} *)s->{fn}) + _i);")
            body.append(f"        free((void *)s->{fn}); s->{fn} = NULL;")
            body.append("    }")
        elif k == "array_string":
            # Each string was malloc'd individually by the decoder, then the
            # pointer array itself — free both.
            cnt = f["count_name"]
            body.append(f"    if (s->{fn}) {{")
            body.append(f"        for (size_t _i = 0; _i < (size_t)s->{cnt}; ++_i)")
            body.append(f"            free((void *)s->{fn}[_i]);")
            body.append(f"        free((void *)s->{fn}); s->{fn} = NULL;")
            body.append("    }")
        elif k in ("fixed_array_scalar", "fixed_array_pod", "fixed_array_handle"):
            body.append(f"    if (s->{fn}) {{ free((void *)s->{fn}); s->{fn} = NULL; }}")
        elif k == "next_in_chain":
            # Walk chain links the client decoder malloc'd. Dispatch on
            # sType, recurse into the matching struct's freer to release
            # its inner pointers, then free the link itself.
            body.append("    {")
            body.append(f"        WGPUChainedStruct *_ch = (WGPUChainedStruct *)s->{fn};")
            body.append("        while (_ch) {")
            body.append("            WGPUChainedStruct *_next = _ch->next;")
            body.append("            switch ((uint32_t)_ch->sType) {")
            for stype, struct in sorted(chainable.items()):
                body.append(f"            case (uint32_t){stype}: yrdawn_client_free_{struct}(({struct} *)_ch); break;")
            body.append("            default: break;")
            body.append("            }")
            body.append("            free(_ch);")
            body.append("            _ch = _next;")
            body.append("        }")
            body.append(f"        s->{fn} = NULL;")
            body.append("    }")
        # scalar / handle / struct_pod / array_count / embedded_chain
        # / embedded_callback_info — nothing to free
    body.append("}")
    body.append("")
    return body


def is_pod_struct(name: str, struct_fields: dict[str, list[tuple[str, str]]],
                  handle_types: set[str], aliases: dict[str, str],
                  cache: dict[str, bool], depth: int = 0) -> bool:
    """POD = no pointer fields, no non-POD nested struct fields.
    Handles count as POD (they're scalars on the wire). Memoised."""
    if name in cache:
        return cache[name]
    if depth > 8 or name not in struct_fields:
        cache[name] = False
        return False
    cache[name] = True  # tentative — avoids infinite recursion on cycles
    pod = True
    for ftype, _fname in struct_fields[name]:
        t = ftype.replace("const", "").strip()
        t = " ".join(t.split())
        if "*" in t:
            pod = False
            break
        if t in handle_types:
            # Handle fields make the struct non-POD even though the
            # type is pointer-sized — the wire still carries a u64
            # token that the server must resolve through the handle
            # table before Dawn sees it. Memcpy'ing the struct as a
            # blob would hand Dawn a raw token cast to a pointer.
            pod = False
            break
        if t in aliases or t in _POD_SCALARS:
            continue
        if t in struct_fields:
            if not is_pod_struct(t, struct_fields, handle_types, aliases, cache, depth + 1):
                pod = False
                break
            continue
        # Unknown / forward-declared — treat as non-POD to be safe.
        pod = False
        break
    cache[name] = pod
    return pod


def classify_arg(arg_type: str, handle_types: set[str], aliases: dict[str, str],
                 struct_names: set[str], pod_structs: set[str],
                 encodable: set[str] = frozenset(),
                 callback_infos: set[str] = frozenset()) -> tuple[str, str]:
    """Returns (kind, resolved_c_type). kind ∈
       {'handle', 'scalar', 'struct_pod', 'pointer_to_pod',
        'pointer_to_encodable', 'stringview',
        'struct_nonpod', 'pointer', 'unknown'}.

    `resolved_c_type` is the pointee type (without const/asterisk) for
    pointer kinds, and the bare type otherwise. `encodable` is the set
    of struct names we have an encoder/decoder for — pointer-to-encodable
    is the new "real body via deep serialisation" path."""
    raw = arg_type.replace("WGPU_NULLABLE", "").strip()
    raw = " ".join(raw.split())
    if "*" in raw:
        is_const = "const" in raw
        pointee = raw.replace("*", " ").replace("const", " ").strip()
        pointee = " ".join(pointee.split())
        if pointee in pod_structs:
            return "pointer_to_pod", pointee
        if pointee in encodable:
            # Non-const pointer to encodable = server-fills-output struct.
            # Const pointer = read-only input descriptor (already encoded).
            return "pointer_to_encodable" if is_const else "out_struct", pointee
        return "pointer", pointee
    t = raw.replace("const", "").strip()
    t = " ".join(t.split())
    if t == "WGPUStringView":
        return "stringview", t
    if t in callback_infos:
        return "callback_info", t
    if t in handle_types:
        return "handle", t
    if t in _POD_SCALARS:
        return "scalar", t
    if t in aliases:
        return "scalar", t
    if t in pod_structs:
        return "struct_pod", t
    if t in struct_names:
        return "struct_nonpod", t
    if t.startswith("WGPU"):
        return "unknown", t
    return "unknown", t


def classify_return(ret_type: str, handle_types: set[str]) -> str:
    """One of: 'void', 'handle', 'opaque_handle', 'value'.

       'handle'         — typed WGPUFoo handle return (already in the
                          handle table on Dawn's side; we mirror the
                          token to the client).
       'opaque_handle'  — `void *`, `void const *`, or `WGPUProc`. Dawn
                          owns the bytes (mapped-buffer ranges, proc
                          pointers, …); we register the pointer in the
                          server-side handle table and ship a u64 token
                          to the client just like a typed handle.
       'value'          — POD scalar / status enum / WGPUFuture.
       'void'           — nothing returned."""
    t = ret_type.replace("WGPU_NULLABLE", "").strip()
    t = " ".join(t.split())
    if t == "void":
        return "void"
    if t in handle_types:
        return "handle"
    # `void *`, `void const *`, function-pointer typedef (WGPUProc).
    pointee_compact = " ".join(t.replace("*", " ").replace("const", " ").split())
    if "*" in t and pointee_compact == "void":
        return "opaque_handle"
    if t == "WGPUProc":
        return "opaque_handle"
    return "value"


def wire_ctype(c_type: str, aliases: dict[str, str]) -> str:
    """Resolve a C type to its underlying primitive for the args-struct
    field. Handles get u64 separately; this is only for scalars."""
    if c_type in aliases:
        return aliases[c_type]
    return c_type


def parse_handle_types(src: str) -> set[str]:
    """Names like WGPUDevice, WGPUBuffer — typedef struct WGPUFooImpl* WGPUFoo."""
    out: set[str] = set()
    for m in _RE_HANDLE_TYPEDEF.finditer(src):
        out.add(m.group(2))
    return out


def parse_methods_from_header(src: str) -> list[dict]:
    """Return a list of dicts: {name, ret_type, args=[(type, name), ...]} for
    every WGPU_EXPORT wgpu* entrypoint. Args are normalised (whitespace
    collapsed). Non-wgpu* exports (e.g. emscripten_*) are skipped."""
    out: list[dict] = []
    for m in _RE_EXPORT.finditer(src):
        name = m.group("name")
        if not name.startswith("wgpu"):
            continue
        ret = " ".join(m.group("ret").split())
        raw_args = m.group("args").strip()
        args: list[tuple[str, str]] = []
        if raw_args and raw_args != "void":
            for chunk in raw_args.split(","):
                chunk = " ".join(chunk.split())
                # Strip WGPU_NULLABLE decorator and split off the trailing identifier.
                chunk = chunk.replace("WGPU_NULLABLE", "").strip()
                # Last token is the parameter name unless it's a pure type
                # (rare). Handle pointer asterisks attached to either side.
                parts = chunk.rsplit(maxsplit=1)
                if len(parts) == 2 and parts[1].startswith("*"):
                    # e.g. "WGPUFoo const *descriptor" — descriptor stays.
                    arg_type = parts[0] + " " + parts[1][:len(parts[1]) - len(parts[1].lstrip("*"))]
                    arg_name = parts[1].lstrip("*")
                    arg_type = " ".join(arg_type.split())
                elif len(parts) == 2:
                    arg_type, arg_name = parts
                else:
                    arg_type, arg_name = chunk, ""
                args.append((arg_type, arg_name))
        out.append({"name": name, "ret_type": ret, "args": args})
    return out


def classify_pattern(name: str, ret_type: str, args: list[tuple[str, str]],
                     handle_types: set[str]) -> str:
    """One of: 'release', 'addref', 'stub'. Real-body templates fire only
    for shapes we can faithfully emit; everything else is a stub."""
    if ret_type != "void" or len(args) != 1:
        return "stub"
    if args[0][0] not in handle_types:
        return "stub"
    if name.endswith("Release"):
        return "release"
    if name.endswith("AddRef"):
        return "addref"
    return "stub"


def auto_spec_for(name: str, ret_type: str, args: list[tuple[str, str]],
                  handle_types: set[str], aliases: dict[str, str],
                  struct_names: set[str], pod_structs: set[str],
                  encodable: set[str] = frozenset(),
                  free_members_available: set[str] = frozenset(),
                  callback_infos: set[str] = frozenset(),
                  cb_info_to_cb: dict[str, str] = None,
                  callbacks: dict[str, tuple[str, list[tuple[str, str]]]] = None,
                  struct_fields: dict[str, list[tuple[str, str]]] = None) -> dict:
    cb_info_to_cb = cb_info_to_cb or {}
    callbacks = callbacks or {}
    struct_fields = struct_fields or {}
    """Build a method-spec dict for an auto-discovered entrypoint.

    Generates a real Dawn-calling body when the method fits a shape we
    can faithfully serialise:
      - all args are handles or scalars (no pointers / descriptors), AND
      - return type is void OR a handle type (where the client pre-
        allocates and the server stores via handle_set).

    Methods that take pointers, structs-by-value, or return non-handle
    scalars fall through to a stub returning UNKNOWN_METHOD — the wire
    contract is still there (method_id, args struct, client wrapper) so
    upgrading to a real body later is a one-line change in this file."""
    # Pre-pass: detect paired args.
    #   handle_array: `size_t XCount` + `WGPUFoo const *X` (handle type)
    #   scalar_array: `size_t XCount` + `uint32_t const *X` (or similar
    #                 fixed-size scalar element)
    #   byte_array:   `void const *data` + `size_t size`  OR
    #                 `size_t size` + `void const *data`  (input bytes)
    # The pair is variable-length on the wire and neither field shows up
    # in the args struct; encoded in the trailing blob.
    skip_indices: set[int] = set()
    handle_array_at: dict[int, tuple[str, str, str]] = {}
    scalar_array_at: dict[int, tuple[str, str, str]] = {}    # idx → (count_name, items_name, elem_type)
    byte_array_at:   dict[int, tuple[str, str]] = {}         # idx → (size_name, data_name) — data at idx
    out_byte_array_at: dict[int, tuple[str, str]] = {}       # idx → (size_name, data_name) — server-fills bytes
    byte_size_at:    set[int] = set()                        # indices used as the paired size

    SCALAR_ARRAY_ELEMS = {"uint32_t", "uint64_t", "int32_t", "int64_t", "float"}
    BYTES_PTR_PREFIXES = ("void", "uint8_t", "int8_t", "char")

    def _is_size_like(at: str, an: str) -> bool:
        atc = " ".join(at.replace("const", "").split())
        return (atc.endswith("size_t") or atc.endswith("uint64_t") or atc.endswith("uint32_t"))

    def _is_void_ptr(at: str) -> bool:
        ac = " ".join(at.replace("const", "").replace("*", " ").split())
        return ac in ("void", "uint8_t", "int8_t", "char")

    for i in range(len(args) - 1):
        at1, an1 = args[i]
        at2, an2 = args[i + 1]
        at1c = at1.replace("const", "").strip()
        at1c = " ".join(at1c.split())
        # count + array (handle / scalar). The count-arg's name must
        # indicate it's a count (ends in `Count` or contains `count`),
        # otherwise we'd misread an unrelated `size_t offset` as a
        # count for the next pointer.
        if (at1c.endswith("size_t") or at1c.endswith("uint32_t")) and "*" in at2 \
                and (an1.endswith("Count") or "count" in an1.lower()):
            pointee = at2.replace("*", " ").replace("const", " ").strip()
            pointee = " ".join(pointee.split())
            if pointee in handle_types:
                handle_array_at[i + 1] = (an1, an2, pointee)
                skip_indices.add(i)
                continue
            if pointee in SCALAR_ARRAY_ELEMS:
                scalar_array_at[i + 1] = (an1, an2, pointee)
                skip_indices.add(i)
                continue
        # size + byte_ptr (only when the size-arg's name actually
        # suggests "size" — not e.g. `offset`)
        if (at1c.endswith("size_t") or at1c.endswith("uint64_t") or at1c.endswith("uint32_t")) \
                and "*" in at2 and "size" in an1.lower():
            pointee = at2.replace("*", " ").replace("const", " ").strip()
            pointee = " ".join(pointee.split())
            if pointee in ("void", "uint8_t", "int8_t", "char") and "const" in at2:
                byte_array_at[i + 1] = (an1, an2)
                skip_indices.add(i)
                continue
        # void const *X + size_t Y (input bytes, reverse order)
        if _is_void_ptr(at1) and ("const" in at1) and _is_size_like(at2, an2) \
                and "size" in an2.lower():
            byte_array_at[i] = (an2, an1)
            byte_size_at.add(i + 1)
            continue
        # void *X + size_t Y (server-fills output bytes, reverse order).
        # E.g. wgpuBufferReadMappedRange(buf, off, void *data, size_t size).
        if _is_void_ptr(at1) and ("const" not in at1) and _is_size_like(at2, an2) \
                and "size" in an2.lower():
            out_byte_array_at[i] = (an2, an1)
            byte_size_at.add(i + 1)
            continue

    classified: list[tuple[str, str, str]] = []  # (kind, ctype, arg_name)
    byte_size_for_data: dict[str, str] = {}
    ok_for_real_body = True
    # In reverse-order byte_array pairs (data at i, size at i+1), the
    # wire writes the size BEFORE the data so the server can sanity-
    # check the read. Detect and inject the size as a synthesised
    # array_count entry ahead of the data entry.
    for idx, (at, an) in enumerate(args):
        if idx in skip_indices:
            classified.append(("array_count", "", an))
            continue
        if idx in byte_size_at:
            # Size was already emitted before the data; skip here.
            continue
        if idx in handle_array_at:
            cn, items_n, pointee = handle_array_at[idx]
            classified.append(("handle_array", pointee, items_n))
            continue
        if idx in scalar_array_at:
            cn, items_n, elem = scalar_array_at[idx]
            classified.append(("scalar_array", elem, items_n))
            continue
        if idx in byte_array_at:
            size_n, data_n = byte_array_at[idx]
            byte_size_for_data[data_n] = size_n
            # Synthesize the size as a leading array_count entry.
            classified.append(("array_count", "", size_n))
            classified.append(("byte_array", "uint8_t", data_n))
            continue
        if idx in out_byte_array_at:
            size_n, data_n = out_byte_array_at[idx]
            # Server allocates a scratch buffer of `size` bytes, hands it
            # to Dawn, and ships the bytes back inside the REPLY payload
            # (alongside any value-return / out_struct fields). Client
            # writes them into the caller's void* buffer.
            classified.append(("array_count", "", size_n))
            classified.append(("out_byte_array", "uint8_t", data_n))
            continue
        kind, resolved = classify_arg(at, handle_types, aliases, struct_names,
                                      pod_structs, encodable, callback_infos)
        # Bare `void *` / `void const *` / WGPUProc argument — treat as
        # an opaque handle token. The server-side lookup goes through the
        # same handle table Dawn uses for typed WGPU* pointers.
        if kind == "pointer" and resolved == "void":
            kind = "opaque_handle"
        elif kind in ("unknown", "scalar") and resolved == "WGPUProc":
            kind = "opaque_handle"
        if kind not in ("handle", "scalar", "struct_pod", "pointer_to_pod",
                        "pointer_to_encodable", "stringview", "out_struct",
                        "callback_info", "scalar_array", "byte_array",
                        "out_byte_array", "handle_array", "array_count",
                        "opaque_handle") or not an:
            ok_for_real_body = False
        classified.append((kind, resolved, an))

    ret_kind = classify_return(ret_type, handle_types)
    # Value-returning methods are supported by the sync-reply-with-payload
    # path: the server emits the return value inline in the REPLY frame;
    # the client's blocking wrapper waits for the reply and copies it
    # back. The wire is only safe for fixed-size POD return types — we
    # bail to stub if the return type is something we can't memcpy.
    value_return_supported = False
    value_return_ctype = ret_type
    if ret_kind == "value":
        ct = ret_type.strip()
        if (ct in handle_types or ct in aliases or ct in _POD_SCALARS
                or ct in pod_structs):
            value_return_supported = True
        if not value_return_supported:
            ok_for_real_body = False

    # FreeMembers methods are pure-client-side: they free the malloc'd
    # inner pointers of a struct the client decoded from a previous
    # REPLY. The server never sees these calls.
    is_free_members = name.endswith("FreeMembers") and len(args) == 1
    fm_struct = None
    if is_free_members:
        at, _ = args[0]
        t = at.replace("const", "").strip()
        t = " ".join(t.split())
        if t in encodable:
            fm_struct = t
            ok_for_real_body = True
        else:
            is_free_members = False  # struct isn't encodable; can't free

    # Build args struct entries.
    wire_args: list[dict] = []
    for kind, ctype, an in classified:
        if kind == "handle":
            wire_args.append({
                "name": an, "ctype": "u64", "wgpu_kind": "in_handle",
                "wgpu_type": ctype,
            })
        elif kind == "opaque_handle":
            # Same wire shape as a typed handle: u64 token, server-side
            # resolved through the shared handle table.
            wire_args.append({
                "name": an, "ctype": "u64", "wgpu_kind": "opaque_in_handle",
                "wgpu_type": ctype,
            })
        elif kind == "scalar":
            wire_args.append({
                "name": an, "ctype": wire_ctype(ctype, aliases),
                "wgpu_kind": "scalar", "wgpu_type": ctype,
            })
        elif kind == "struct_pod":
            # POD struct included by value in the args struct. The C
            # struct layout is identical client-side and server-side
            # because webgpu.h is included transitively in
            # methods.gen.h.
            wire_args.append({
                "name": an, "ctype": ctype, "wgpu_kind": "struct_pod",
                "wgpu_type": ctype,
            })
        elif kind == "pointer_to_pod":
            # `WGPUFooPod const *foo` — the wire carries the POD value
            # inline plus a 1-byte present flag so null pointers can
            # round-trip. The user-facing wrapper takes the pointer
            # (matches webgpu.h); the encode helper dereferences it.
            wire_args.append({
                "name": an + "_present", "ctype": "u32",
                "wgpu_kind": "present_flag", "wgpu_type": "uint32_t",
            })
            wire_args.append({
                "name": an, "ctype": ctype, "wgpu_kind": "pointer_to_pod",
                "wgpu_type": ctype,
            })
        elif kind == "pointer_to_encodable":
            # Variable-length: serialised after the args struct via the
            # generated yrdawn_encode_<Struct>. No slot in args struct.
            wire_args.append({
                "name": an, "ctype": ctype, "wgpu_kind": "pointer_to_encodable",
                "wgpu_type": ctype, "in_blob": True,
            })
        elif kind == "stringview":
            # WGPUStringView by value (typically a label or name). Same
            # variable-length blob treatment as pointer_to_encodable.
            wire_args.append({
                "name": an, "ctype": "WGPUStringView",
                "wgpu_kind": "stringview", "wgpu_type": "WGPUStringView",
                "in_blob": True,
            })
        elif kind == "array_count":
            wire_args.append({
                "name": an, "ctype": "size_t",
                "wgpu_kind": "array_count", "wgpu_type": "size_t",
                "in_blob": True,
            })
        elif kind == "handle_array":
            wire_args.append({
                "name": an, "ctype": ctype,
                "wgpu_kind": "handle_array", "wgpu_type": ctype,
                "in_blob": True,
            })
        elif kind == "byte_array":
            wire_args.append({
                "name": an, "ctype": "uint8_t",
                "wgpu_kind": "byte_array", "wgpu_type": ctype,
                "in_blob": True,
            })
        elif kind == "scalar_array":
            wire_args.append({
                "name": an, "ctype": ctype,
                "wgpu_kind": "scalar_array", "wgpu_type": ctype,
                "in_blob": True,
            })
        elif kind == "out_struct":
            # Server fills this struct, encodes it into the REPLY
            # payload; client decodes back into the user's pointer.
            # No slot in the args struct.
            wire_args.append({
                "name": an, "ctype": ctype,
                "wgpu_kind": "out_struct", "wgpu_type": ctype,
                "in_blob": True,
            })
        elif kind == "out_byte_array":
            # Server fills `count` bytes (count from the paired
            # array_count, sent on input). The reply payload carries
            # the bytes back; the client wrapper copies them into the
            # caller's void* buffer.
            wire_args.append({
                "name": an, "ctype": "uint8_t",
                "wgpu_kind": "out_byte_array", "wgpu_type": ctype,
                "in_blob": True,
            })
        elif kind == "callback_info":
            # CallbackInfo struct passed by value. The user-facing
            # signature exposes (user_cb, user_userdata); the server
            # builds a real CallbackInfo around a generated trampoline
            # and passes that to wgpu*. No slot in the args struct.
            wire_args.append({
                "name": an, "ctype": ctype,
                "wgpu_kind": "callback_info", "wgpu_type": ctype,
            })
        else:
            # Pointer / non-POD struct / unknown — placeholder slot.
            wire_args.append({
                "name": an or "_arg",
                "ctype": "u64", "wgpu_kind": "opaque", "wgpu_type": ctype,
            })

    if ret_kind in ("handle", "opaque_handle"):
        wire_args.append({
            "name": "result_handle", "ctype": "u64", "wgpu_kind": "out_handle",
            "wgpu_type": ret_type,
        })

    # spec.returns for value-return methods stays as "value"; emit_client_c
    # detects this and uses the blocking-reply path.
    if ret_kind == "value" and value_return_supported:
        spec_returns_value = True
    else:
        spec_returns_value = False

    if not wire_args:
        wire_args.append({"name": "_reserved", "ctype": "u32"})

    if ret_kind in ("handle", "opaque_handle"):
        spec_returns = "handle"
    elif ret_kind == "value" and value_return_supported:
        spec_returns = "value"
    else:
        spec_returns = "void"

    if not ok_for_real_body:
        # Stub: log every miss so unsupported entry points are visible
        # at runtime instead of a silent UNKNOWN_METHOD reply.
        reasons = []
        for kind, _ctype, _an in classified:
            if kind not in ("handle", "scalar", "struct_pod", "pointer_to_pod"):
                reasons.append(kind)
        if ret_kind == "value":
            reasons.append("value_return")
        reason_str = ",".join(reasons) if reasons else "shape"
        body = (
            "        (void)a;\n"
            f"        yerror(\"yrdawn: stub for {name} (needs: {reason_str})\");\n"
            "        return YETTY_YRDAWN_REPLY_UNKNOWN_METHOD;"
        )
        return {
            "name": name,
            "args": wire_args,
            "returns": spec_returns,
            "auto": True,
            "server_body": body,
        }

    # ---- Real-body emission ------------------------------------------
    # Resolve every in_handle (lookup + null check) and assemble the
    # wgpu* call argument list. We collect by-name then re-order at the
    # end in args-declaration order — necessary because the classified
    # list may swap entries (e.g. byte_array reverse-order injects the
    # synthesised array_count BEFORE the data, but the wgpu* function
    # still expects (data, size)).
    body_lines: list[str] = []
    call_args: list[str] = []
    call_arg_by_name: dict[str, str] = {}
    has_var = any(k in ("pointer_to_encodable", "stringview",
                         "array_count", "handle_array", "out_struct",
                         "scalar_array", "byte_array", "out_byte_array")
                  for k, _, _ in classified)
    has_cb = any(k == "callback_info" for k, _, _ in classified)
    if has_var:
        body_lines.append("        struct yrdawn_arena _arena = {0};")
        body_lines.append("        const uint8_t *_p = (const uint8_t *)body + sizeof(*a);")
        body_lines.append("        size_t _rem = body_len - sizeof(*a);")
    for kind, ctype, an in classified:
        if kind == "handle":
            local = f"_h_{an}"
            body_lines.append(
                f"        {ctype} {local} = ({ctype})yrdawn_server_handle_get(ctx, a->{an});")
            body_lines.append(
                f"        if (!{local})" + (" { yrdawn_arena_free(&_arena); " if has_var else " ") +
                f"return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;" + (" }" if has_var else ""))
            call_args.append(local); call_arg_by_name[an] = local
        elif kind == "opaque_handle":
            # Bare `void *` arg — token in the same handle table as the
            # typed WGPU* handles. NULL token = NULL pointer (legitimate
            # for nullable args), no validation error.
            local = f"_h_{an}"
            body_lines.append(
                f"        void *{local} = a->{an} ? yrdawn_server_handle_get(ctx, a->{an}) : NULL;")
            call_args.append(f"({ctype} *){local}")
            call_arg_by_name[an] = f"({ctype} *){local}"
        elif kind == "struct_pod":
            call_args.append(f"a->{an}"); call_arg_by_name[an] = f"a->{an}"
        elif kind == "pointer_to_pod":
            # `a` is the const decoded-args struct, so `&a->{an}` is a
            # `const ctype *`. Most WGPU params are `const ctype *` and take it
            # as-is, but a few (e.g. wgpuInstanceWaitAny's WGPUFutureWaitInfo*,
            # which writes `.completed` back) are non-const. Cast away const —
            # the args buffer is server-owned scratch the call may write into.
            arg_expr = f"(a->{an}_present ? ({ctype} *)&a->{an} : NULL)"
            call_args.append(arg_expr); call_arg_by_name[an] = arg_expr
        elif kind == "pointer_to_encodable":
            body_lines.append(f"        uint8_t _f_{an} = 0;")
            body_lines.append(f"        if (_rem < 1) {{ yrdawn_arena_free(&_arena); "
                              f"return YETTY_YRDAWN_REPLY_VALIDATION_ERROR; }}")
            body_lines.append(f"        _f_{an} = *_p; _p += 1; _rem -= 1;")
            body_lines.append(f"        {ctype} _v_{an} = (({ctype}){{0}});")
            body_lines.append(f"        {ctype} *_ptr_{an} = NULL;")
            body_lines.append(f"        if (_f_{an}) {{")
            body_lines.append(f"            if (!yrdawn_decode_{ctype}(ctx, &_p, &_rem, &_v_{an}, &_arena)) {{")
            body_lines.append(f"                yrdawn_arena_free(&_arena);")
            body_lines.append(f"                return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;")
            body_lines.append(f"            }}")
            body_lines.append(f"            _ptr_{an} = &_v_{an};")
            body_lines.append(f"        }}")
            call_args.append(f"_ptr_{an}"); call_arg_by_name[an] = f"_ptr_{an}"
        elif kind == "stringview":
            body_lines.append("        WGPUStringView _sv_" + an + " = {0};")
            body_lines.append("        {")
            body_lines.append("            uint64_t _len = 0;")
            body_lines.append("            if (_rem < sizeof(_len)) { yrdawn_arena_free(&_arena); "
                              "return YETTY_YRDAWN_REPLY_VALIDATION_ERROR; }")
            body_lines.append("            memcpy(&_len, _p, sizeof(_len));")
            body_lines.append("            _p += sizeof(_len); _rem -= sizeof(_len);")
            body_lines.append("            if (_len > _rem) { yrdawn_arena_free(&_arena); "
                              "return YETTY_YRDAWN_REPLY_VALIDATION_ERROR; }")
            body_lines.append("            if (_len > 0) {")
            body_lines.append("                char *_buf = (char *)yrdawn_arena_alloc(&_arena, "
                              "(size_t)_len + 1u);")
            body_lines.append("                if (!_buf) { yrdawn_arena_free(&_arena); "
                              "return YETTY_YRDAWN_REPLY_INTERNAL; }")
            body_lines.append("                memcpy(_buf, _p, (size_t)_len);")
            body_lines.append("                _buf[_len] = '\\0';")
            body_lines.append(f"                _sv_{an}.data = _buf; _sv_{an}.length = (size_t)_len;")
            body_lines.append("                _p += _len; _rem -= (size_t)_len;")
            body_lines.append("            }")
            body_lines.append("        }")
            call_args.append(f"_sv_{an}"); call_arg_by_name[an] = f"_sv_{an}"
        elif kind == "array_count":
            # Count: read u64 from blob, will be passed alongside the
            # paired handle_array arg. We don't emit the call arg here —
            # the array arg handler does (count + array).
            body_lines.append(f"        uint64_t _ac_{an} = 0;")
            body_lines.append(f"        if (_rem < sizeof(_ac_{an})) {{ yrdawn_arena_free(&_arena); "
                              f"return YETTY_YRDAWN_REPLY_VALIDATION_ERROR; }}")
            body_lines.append(f"        memcpy(&_ac_{an}, _p, sizeof(_ac_{an})); "
                              f"_p += sizeof(_ac_{an}); _rem -= sizeof(_ac_{an});")
            call_args.append(f"(size_t)_ac_{an}"); call_arg_by_name[an] = f"(size_t)_ac_{an}"
        elif kind == "out_struct":
            # Server-fills output: allocate locally, will be encoded
            # into the reply payload after the call.
            body_lines.append(f"        {ctype} _out_{an} = (({ctype}){{0}});")
            call_args.append(f"&_out_{an}"); call_arg_by_name[an] = f"&_out_{an}"
        elif kind == "callback_info":
            # Build a real CallbackInfo with our trampoline + heap-allocated
            # closure. Trampoline emits the REPLY when Dawn fires the
            # callback. Some CallbackInfo variants (e.g. logging) don't
            # have a `mode` field — emit it conditionally.
            has_mode = any(fn == "mode" for _ft, fn in struct_fields.get(ctype, []))
            body_lines.append("        if (req_id == 0)")
            body_lines.append("            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;")
            body_lines.append("        struct yrdawn_async_cb *_cb =")
            body_lines.append("            (struct yrdawn_async_cb *)malloc(sizeof(*_cb));")
            body_lines.append("        if (!_cb) {")
            if has_var:
                body_lines.append("            yrdawn_arena_free(&_arena);")
            body_lines.append("            return YETTY_YRDAWN_REPLY_OUT_OF_MEMORY;")
            body_lines.append("        }")
            body_lines.append("        _cb->ctx = ctx; _cb->req_id = req_id;")
            body_lines.append("        _cb->method_id = method_id; _cb->result_handle = 0;")
            body_lines.append(f"        {ctype} _cbi_{an} = (({ctype}){{0}});")
            if has_mode:
                # WaitAnyOnly + wgpuInstanceWaitAny is the only callback
                # mode this Dawn build actually drives in our setup —
                # ProcessEvents-driven callbacks never fire and Spontaneous
                # races the single-threaded OSC writer. WaitAny blocks the
                # wire-layer coro until the trampoline emits the REPLY,
                # which is fine: a Map/CreatePipeline/Submit-Done finishes
                # in well under a render frame.
                body_lines.append(f"        _cbi_{an}.mode = WGPUCallbackMode_WaitAnyOnly;")
            body_lines.append(f"        _cbi_{an}.callback = yrdawn_trampoline_{name};")
            body_lines.append(f"        _cbi_{an}.userdata1 = _cb;")
            call_args.append(f"_cbi_{an}"); call_arg_by_name[an] = f"_cbi_{an}"
        elif kind == "handle_array":
            # Read N u64 handles (N = the just-decoded count from the
            # paired handle_array_count arg). Allocate the WGPUFoo array
            # in the arena, look up each handle.
            # We need the count variable name — it's whatever the
            # previous classified entry decoded into `_ac_<name>`.
            prev = next((c for c in reversed(classified[:-1])
                         if c[0] == "array_count"), None)
            count_var = f"_ac_{prev[2]}" if prev else "0"
            body_lines.append(f"        {ctype} *_arr_{an} = NULL;")
            body_lines.append(f"        if ((size_t){count_var} > 0) {{")
            body_lines.append(f"            _arr_{an} = ({ctype} *)yrdawn_arena_alloc(&_arena, "
                              f"(size_t){count_var} * sizeof({ctype}));")
            body_lines.append(f"            if (!_arr_{an}) {{ yrdawn_arena_free(&_arena); "
                              f"return YETTY_YRDAWN_REPLY_INTERNAL; }}")
            body_lines.append(f"            for (size_t _i = 0; _i < (size_t){count_var}; ++_i) {{")
            body_lines.append("                uint64_t _hv = 0;")
            body_lines.append("                if (_rem < sizeof(_hv)) { yrdawn_arena_free(&_arena); "
                              "return YETTY_YRDAWN_REPLY_VALIDATION_ERROR; }")
            body_lines.append("                memcpy(&_hv, _p, sizeof(_hv));")
            body_lines.append("                _p += sizeof(_hv); _rem -= sizeof(_hv);")
            body_lines.append(f"                _arr_{an}[_i] = "
                              f"({ctype})yrdawn_server_handle_get(ctx, _hv);")
            body_lines.append("            }")
            body_lines.append("        }")
            call_args.append(f"_arr_{an}"); call_arg_by_name[an] = f"_arr_{an}"
        elif kind == "scalar_array":
            prev = next((c for c in reversed(classified[:-1])
                         if c[0] == "array_count"), None)
            count_var = f"_ac_{prev[2]}" if prev else "0"
            body_lines.append(f"        {ctype} *_arr_{an} = NULL;")
            body_lines.append(f"        if ((size_t){count_var} > 0) {{")
            body_lines.append(f"            size_t _nb = (size_t){count_var} * sizeof({ctype});")
            body_lines.append(f"            if (_rem < _nb) {{ yrdawn_arena_free(&_arena); "
                              f"return YETTY_YRDAWN_REPLY_VALIDATION_ERROR; }}")
            body_lines.append(f"            _arr_{an} = ({ctype} *)yrdawn_arena_alloc(&_arena, _nb);")
            body_lines.append(f"            if (!_arr_{an}) {{ yrdawn_arena_free(&_arena); "
                              f"return YETTY_YRDAWN_REPLY_INTERNAL; }}")
            body_lines.append(f"            memcpy(_arr_{an}, _p, _nb);")
            body_lines.append(f"            _p += _nb; _rem -= _nb;")
            body_lines.append("        }")
            call_args.append(f"_arr_{an}"); call_arg_by_name[an] = f"_arr_{an}"
        elif kind == "byte_array":
            # Look up the paired size: byte_array's data-name maps to
            # the size-name via byte_size_for_data, OR fall back to the
            # most recent array_count.
            size_name = byte_size_for_data.get(an)
            if size_name:
                count_var = f"_ac_{size_name}"
            else:
                prev = next((c for c in reversed(classified[:-1])
                             if c[0] == "array_count"), None)
                count_var = f"_ac_{prev[2]}" if prev else "0"
            body_lines.append(f"        const void *_bytes_{an} = NULL;")
            body_lines.append(f"        if ((size_t){count_var} > 0) {{")
            body_lines.append(f"            if (_rem < (size_t){count_var}) {{ "
                              f"yrdawn_arena_free(&_arena); "
                              f"return YETTY_YRDAWN_REPLY_VALIDATION_ERROR; }}")
            body_lines.append(f"            void *_buf = yrdawn_arena_alloc(&_arena, "
                              f"(size_t){count_var});")
            body_lines.append(f"            if (!_buf) {{ yrdawn_arena_free(&_arena); "
                              f"return YETTY_YRDAWN_REPLY_INTERNAL; }}")
            body_lines.append(f"            memcpy(_buf, _p, (size_t){count_var});")
            body_lines.append(f"            _p += (size_t){count_var}; "
                              f"_rem -= (size_t){count_var};")
            body_lines.append(f"            _bytes_{an} = _buf;")
            body_lines.append("        }")
            call_args.append(f"_bytes_{an}"); call_arg_by_name[an] = f"_bytes_{an}"
        elif kind == "out_byte_array":
            # Server-fills bytes (e.g. wgpuBufferReadMappedRange). Allocate
            # a scratch buffer of `count` bytes from the arena; Dawn fills
            # it; the post-call reply path ships the bytes back inline.
            prev = next((c for c in reversed(classified[:-1])
                         if c[0] == "array_count"), None)
            count_var = f"_ac_{prev[2]}" if prev else "0"
            body_lines.append(f"        void *_obytes_{an} = NULL;")
            body_lines.append(f"        size_t _obytes_{an}_n = (size_t){count_var};")
            body_lines.append(f"        if (_obytes_{an}_n > 0) {{")
            body_lines.append(f"            _obytes_{an} = yrdawn_arena_alloc(&_arena, _obytes_{an}_n);")
            body_lines.append(f"            if (!_obytes_{an}) {{ yrdawn_arena_free(&_arena); "
                              f"return YETTY_YRDAWN_REPLY_INTERNAL; }}")
            body_lines.append("        }")
            call_args.append(f"_obytes_{an}"); call_arg_by_name[an] = f"_obytes_{an}"
        else:  # scalar
            call_args.append(f"({ctype})a->{an}"); call_arg_by_name[an] = f"({ctype})a->{an}"

    # Re-order call args to match the original C signature (in case
    # classified rearranged them — e.g. byte_array reverse-order pair).
    ordered: list[str] = []
    for at, an in args:
        if an in call_arg_by_name:
            ordered.append(call_arg_by_name[an])
        else:
            # Some classified entries (synthesised array_count for
            # reverse-order byte_array) don't have a corresponding C
            # arg name — drop them from call_args silently. Their value
            # is reachable via the paired data-arg's call_arg.
            pass
    call_expr = f"{name}({', '.join(ordered)})"
    cleanup = "yrdawn_arena_free(&_arena); " if has_var else ""

    # Collect server-fills args for reply payload encoding. Order in
    # the reply payload: value-return (if any) → out_byte_arrays →
    # out_structs. Client decoder reads in the same order.
    out_structs = [(c, an) for k, c, an in classified if k == "out_struct"]
    out_byte_arrays = [an for k, _c, an in classified if k == "out_byte_array"]

    if has_cb:
        # If the wgpu* call returns a WGPUFuture, drive it to completion
        # synchronously with wgpuInstanceWaitAny. The callback (mode=
        # WaitAnyOnly) fires inside WaitAny on the same thread, the
        # trampoline emits the REPLY, and we return DEFERRED so
        # handle_cmd doesn't double-reply.
        #
        # Methods that return void from the cb-bearing entry point
        # (only wgpuDeviceSetLoggingCallback today — a free-form
        # listener, no Future) can't be waited on; fall back to the
        # old fire-and-pray path. Its trampoline ignores the closure
        # and Dawn will fire it whenever it likes.
        ret_clean = " ".join(ret_type.replace("WGPU_NULLABLE", "").split())
        if ret_clean == "WGPUFuture":
            body_lines.append(f"        WGPUFuture _f = {call_expr};")
            body_lines.append("        WGPUFutureWaitInfo _wi = { _f, 0 };")
            body_lines.append("        WGPUInstance _inst = (WGPUInstance)yrdawn_server_get_shared_instance(ctx);")
            body_lines.append("        if (_inst) (void)wgpuInstanceWaitAny(_inst, 1, &_wi, UINT64_MAX);")
        else:
            body_lines.append(f"        (void){call_expr};")
        if has_var:
            body_lines.append("        yrdawn_arena_free(&_arena);")
        body_lines.append("        return YRDAWN_DISPATCH_DEFERRED;")
    elif out_structs or out_byte_arrays:
        # Call the method, then encode (return value if any) + each
        # server-fills slot into a single reply payload. Return
        # DEFERRED so the dispatcher doesn't emit a second REPLY.
        if ret_kind == "void":
            body_lines.append(f"        {call_expr};")
        else:  # value return
            body_lines.append(f"        {ret_type} _result = {call_expr};")
        body_lines.append("        struct yetty_ycore_buffer _reply_buf = {0};")
        if ret_kind != "void":
            body_lines.append("        { struct yetty_ycore_void_result _r = "
                              "yetty_ycore_buffer_write(&_reply_buf, &_result, sizeof(_result));"
                              " if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }")
        for an in out_byte_arrays:
            body_lines.append(f"        if (_obytes_{an}_n > 0 && _obytes_{an}) {{ "
                              f"struct yetty_ycore_void_result _r = "
                              f"yetty_ycore_buffer_write(&_reply_buf, _obytes_{an}, _obytes_{an}_n); "
                              f"if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }}")
        for c, an in out_structs:
            body_lines.append(f"        {{ struct yetty_ycore_void_result eresult = "
                              f"yrdawn_encode_{c}(&_out_{an}, &_reply_buf); "
                              f"if (YETTY_IS_ERR(eresult)) yetty_ycore_error_destroy(eresult.error); }}")
        # Server-side cleanup: Dawn may have allocated inner pointers
        # into the output struct (e.g. strings in WGPUAdapterInfo). Find
        # the corresponding wgpu<Type>FreeMembers function — if it
        # exists in webgpu.h, call it. The codegen only marks methods
        # as out_struct-bearing when an encoder exists for the struct,
        # so the FreeMembers companion is highly likely to exist.
        for c, an in out_structs:
            fm = f"wgpu{c[4:]}FreeMembers"
            if fm in free_members_available:
                body_lines.append(f"        {fm}(_out_{an});")
        body_lines.append(f"        {cleanup}")
        body_lines.append("        struct yetty_ycore_void_result _e =")
        body_lines.append("            yrdawn_server_emit_reply(ctx, req_id, method_id,")
        body_lines.append("                                    YETTY_YRDAWN_REPLY_OK,")
        body_lines.append("                                    _reply_buf.data, _reply_buf.size);")
        body_lines.append("        if (YETTY_IS_ERR(_e)) yetty_ycore_error_destroy(_e.error);")
        body_lines.append("        yetty_ycore_buffer_destroy(&_reply_buf);")
        body_lines.append("        return YRDAWN_DISPATCH_DEFERRED;")
    elif ret_kind == "void":
        body_lines.append(f"        {call_expr};")
        body_lines.append(f"        {cleanup}return YETTY_YRDAWN_REPLY_OK;")
    elif ret_kind == "value" and value_return_supported:
        # Server-side value-return: call, emit REPLY with the value as
        # inline payload, return DEFERRED so the dispatcher does NOT
        # emit another REPLY on top.
        body_lines.append(f"        {ret_type} _result = {call_expr};")
        body_lines.append(f"        {cleanup}")
        body_lines.append("        struct yetty_ycore_void_result _e =")
        body_lines.append("            yrdawn_server_emit_reply(ctx, req_id, method_id,")
        body_lines.append("                                    YETTY_YRDAWN_REPLY_OK,")
        body_lines.append("                                    &_result, sizeof(_result));")
        body_lines.append("        if (YETTY_IS_ERR(_e)) yetty_ycore_error_destroy(_e.error);")
        body_lines.append("        return YRDAWN_DISPATCH_DEFERRED;")
    elif ret_kind == "opaque_handle":
        # `void *` / `void const *` / WGPUProc — Dawn owns the bytes.
        # Register the bare pointer in the handle table and ship the
        # token. Failure → NULL ptr is *not* an error: many entry points
        # (mapped-range queries, GetProcAddress) legitimately return
        # NULL to signal "not available". Client gets 0 in that case.
        bare_ret = ret_type.replace("WGPU_NULLABLE", "").strip()
        bare_ret = " ".join(bare_ret.split())
        body_lines.append(f"        {bare_ret} _result = {call_expr};")
        body_lines.append(f"        {cleanup}")
        body_lines.append("        if (!_result) return YETTY_YRDAWN_REPLY_OK;")
        body_lines.append("        struct yetty_ycore_void_result _r =")
        body_lines.append("            yrdawn_server_handle_set(ctx, a->result_handle, (void *)_result);")
        body_lines.append("        if (YETTY_IS_ERR(_r)) { yetty_ycore_error_destroy(_r.error); return YETTY_YRDAWN_REPLY_INTERNAL; }")
        body_lines.append("        return YETTY_YRDAWN_REPLY_OK;")
    else:  # handle
        bare_ret = ret_type.replace("WGPU_NULLABLE", "").strip()
        bare_ret = " ".join(bare_ret.split())
        body_lines.append(f"        {bare_ret} _result = {call_expr};")
        body_lines.append(f"        {cleanup}")
        body_lines.append("        if (!_result)")
        body_lines.append("            return YETTY_YRDAWN_REPLY_INTERNAL;")
        body_lines.append("        struct yetty_ycore_void_result _r =")
        body_lines.append(
            "            yrdawn_server_handle_set(ctx, a->result_handle, _result);")
        body_lines.append("        if (YETTY_IS_ERR(_r)) {")
        body_lines.append("            yetty_ycore_error_destroy(_r.error);")
        body_lines.append(f"            wgpu{bare_ret[4:]}Release(_result);")
        body_lines.append("            return YETTY_YRDAWN_REPLY_INTERNAL;")
        body_lines.append("        }")
        body_lines.append("        return YETTY_YRDAWN_REPLY_OK;")

    # If this method has an output struct, mark it for the dyn-reply
    # path — the REPLY payload is variable-length (status + encoded
    # struct). The client wrapper uses blocking_dyn.
    # Set when the reply payload is variable-length — either an out_struct
    # encoded into the reply, OR a server-fills byte array shipped back.
    has_out_struct = any(k in ("out_struct", "out_byte_array")
                         for k, _, _ in classified)
    # User-facing wrapper takes args in original C declaration order
    # (matching webgpu.h), not wire order. Wire order may differ when
    # byte_array detection inserts a size-count before the data ptr.
    # callback_info args are replaced by the cb/user pair.
    kind_by_name = {an_: kind_ for kind_, _, an_ in classified}
    client_param_order = [an for at, an in args
                          if kind_by_name.get(an) != "callback_info"]
    spec: dict = {
        "name": name,
        "args": wire_args,
        "returns": spec_returns,
        "auto": True,
        "server_body": "\n".join(body_lines),
        "client_param_order": client_param_order,
    }
    if spec_returns == "value":
        spec["return_ctype"] = value_return_ctype
    if has_out_struct:
        spec["has_out_struct"] = True
    if has_cb:
        spec["async"] = True
        # Find the CallbackInfo arg's struct type and its callback type.
        cb_arg = next((c for k, c, _ in classified if k == "callback_info"), None)
        if cb_arg and cb_arg in cb_info_to_cb:
            spec["callback_info_type"] = cb_arg
            spec["callback_type"] = cb_info_to_cb[cb_arg]
            spec["callback_sig"] = callbacks.get(spec["callback_type"])
            spec["trampoline"] = _emit_auto_trampoline(name, spec["callback_type"],
                                                       spec["callback_sig"])
    if is_free_members:
        spec["free_members"] = fm_struct
        spec["free_members_arg"] = args[0][1]
        # Server stub never reached — client doesn't send.
        spec["server_body"] = (
            "        (void)a;\n"
            f"        /* {name} is client-only; server never receives this CMD */\n"
            "        return YETTY_YRDAWN_REPLY_OK;"
        )
    return spec


def _emit_auto_trampoline(method_name: str, cb_type: str,
                          cb_sig: tuple[str, list[tuple[str, str]]] | None) -> str:
    """Emit the C source for a trampoline matching the callback's exact
    signature. Body extracts the closure from userdata1, posts a REPLY
    whose payload is the first arg (typically a status enum) memcpy'd,
    then frees the closure."""
    if not cb_sig:
        return ""
    rt, cb_args = cb_sig
    # Build the function arg list.
    params: list[str] = []
    for at, an in cb_args:
        params.append(f"{at} {an}")
    sig = f"static {rt} yrdawn_trampoline_{method_name}({', '.join(params)})"
    # Signature is dictated by Dawn's async callback typedef, so it cannot
    # return a Result; emit_reply failures are absorbed at this boundary.
    lines = ["YETTY_EXTERNAL_CALLBACK", sig, "{"]
    # First arg is conventionally a status enum we forward in the REPLY.
    first_arg = cb_args[0] if cb_args else None
    # Find userdata1 (used to carry our closure pointer).
    u1 = next((an for at, an in cb_args if an == "userdata1"), None)
    if u1 is None:
        # Some callbacks use a single `void *userdata` or none — bail out.
        # We mark such methods as stubs by emitting nothing here; the
        # dispatch case will compile-fail unless we caught it elsewhere.
        return ""
    for at, an in cb_args:
        if an in ("userdata2",):
            lines.append(f"    (void){an};")
    lines.append(f"    struct yrdawn_async_cb *d = (struct yrdawn_async_cb *){u1};")
    if first_arg:
        fat, fan = first_arg
        # Forward the status as REPLY payload (memcpy'd). The client's
        # generated wrapper reads it back the same way.
        lines.append(f"    {fat} _s = {fan};")
        lines.append("    uint32_t _payload = (uint32_t)_s;")
        lines.append("    struct yetty_ycore_void_result e =")
        lines.append("        yrdawn_server_emit_reply(d->ctx, d->req_id, d->method_id,")
        lines.append("                                YETTY_YRDAWN_REPLY_OK,")
        lines.append("                                &_payload, sizeof(_payload));")
        lines.append("    if (YETTY_IS_ERR(e)) yetty_ycore_error_destroy(e.error);")
    else:
        lines.append("    struct yetty_ycore_void_result e =")
        lines.append("        yrdawn_server_emit_reply(d->ctx, d->req_id, d->method_id,")
        lines.append("                                YETTY_YRDAWN_REPLY_OK, NULL, 0);")
        lines.append("    if (YETTY_IS_ERR(e)) yetty_ycore_error_destroy(e.error);")
    # Forward any other args we haven't already silenced.
    for at, an in cb_args:
        if an not in ("userdata1", "userdata2") and an != (first_arg[1] if first_arg else None):
            lines.append(f"    (void){an};")
    lines.append("    free(d);")
    if rt != "void":
        lines.append(f"    return ({rt})0;")
    lines.append("}")
    return "\n".join(lines)


def merge_with_auto(curated: list[dict], header_path: pathlib.Path) -> list[dict]:
    """Pull every wgpu* entrypoint from webgpu.h, drop the names already
    pinned in `curated`, allocate sequential method_ids starting at 200
    in deterministic (sorted) order for the rest. Curated entries keep
    their explicit ids untouched."""
    if not header_path.exists():
        sys.stderr.write(
            f"yrdawn-gen: webgpu.h not found at {header_path}; emitting curated table only.\n"
        )
        return curated
    src = header_path.read_text()
    handle_types = parse_handle_types(src)
    aliases = parse_type_aliases(src)
    struct_names = parse_struct_names(src)
    struct_fields = parse_struct_fields(src)
    # Set of wgpu*FreeMembers names that actually exist in webgpu.h —
    # used to gate server-side cleanup calls on output structs.
    free_members_available = set(re.findall(r"\bwgpu\w+FreeMembers\b", src))
    callbacks = parse_callbacks(src)
    # CallbackInfo struct → its `callback` field's type.
    cb_info_to_cb: dict[str, str] = {}
    for sn_name in struct_names:
        cb = callback_for_info_struct(sn_name, struct_fields, callbacks)
        if cb:
            cb_info_to_cb[sn_name] = cb
    callback_infos = set(cb_info_to_cb.keys())
    # sType -> struct: every chain-able extension struct discovered in
    # the WGPUSType enum that has a matching WGPUFoo with a leading
    # `WGPUChainedStruct chain;` field. The nextInChain encoder/decoder/
    # freer dispatches off this map.
    stype_to_struct = parse_stype_to_struct(src, struct_fields)
    pod_cache: dict[str, bool] = {}
    pod_structs = {
        n for n in struct_names
        if is_pod_struct(n, struct_fields, handle_types, aliases, pod_cache)
    }
    enc_cache: dict[str, bool] = {}
    encodable = {
        n for n in struct_names
        if is_encodable_struct(n, struct_fields, handle_types, aliases,
                               pod_structs, struct_names, callback_infos,
                               enc_cache)
    }
    # Restrict the chain dispatch to sTypes whose struct we can actually
    # encode/decode. Unknown sTypes still survive on the wire — the
    # encoder writes a tag+sType+length=0 and the decoder skips by
    # length — but they don't get a real codec body emitted.
    chainable = {st: s for st, s in stype_to_struct.items() if s in encodable}
    discovered = parse_methods_from_header(src)
    curated_names = {m["name"] for m in curated}
    auto = []
    for d in sorted(discovered, key=lambda x: x["name"]):
        if d["name"] in curated_names:
            continue
        auto.append(auto_spec_for(d["name"], d["ret_type"], d["args"],
                                  handle_types, aliases, struct_names,
                                  pod_structs, encodable, free_members_available,
                                  callback_infos, cb_info_to_cb, callbacks,
                                  struct_fields))
    # Stash codec metadata on the methods list so emit_server_c /
    # emit_client_c can emit the per-struct encoder/decoder helpers
    # alongside the dispatch table. We attach to the list itself via a
    # sentinel dict at index -1; emit_* code pops it.
    auto.append({"_codec_meta": {
        "encodable": sorted(encodable),
        "struct_fields": struct_fields,
        "handle_types": handle_types,
        "aliases": aliases,
        "pod_structs": pod_structs,
        "struct_names": struct_names,
        "callback_infos": callback_infos,
        "chainable": chainable,
    }})
    # Assign sequential ids starting at 200 to leave room for hand-pinned
    # curated entries 1-199 and yetty meta-methods at 100+.
    next_id = 200
    used_ids = {m["id"] for m in curated}
    for a in auto:
        while next_id in used_ids:
            next_id += 1
        a["id"] = next_id
        used_ids.add(next_id)
        next_id += 1
    return curated + auto


def emit_methods_h(methods: list[dict]) -> str:
    lines: list[str] = []
    lines.append("/* Generated by src/yetty/yrdawn/generate.py — DO NOT EDIT. */")
    lines.append("#ifndef YETTY_YRDAWN_METHODS_GEN_H")
    lines.append("#define YETTY_YRDAWN_METHODS_GEN_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("/* Brings in WGPU* by-value struct types (WGPUColor, WGPUExtent3D, …)")
    lines.append(" * referenced by auto-generated args structs. The wire layout reuses")
    lines.append(" * the C layout because both client and server are C and use the")
    lines.append(" * same webgpu.h. */")
    lines.append("#include <webgpu/webgpu.h>")
    lines.append("")
    lines.append("#include <yetty/ycore/result.h>")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append("extern \"C\" {")
    lines.append("#endif")
    lines.append("")
    lines.append("enum yetty_yrdawn_method_id {")
    lines.append("    YETTY_YRDAWN_METHOD_INVALID = 0,")
    for m in methods:
        lines.append(f"    YETTY_YRDAWN_METHOD_{m['name']} = {m['id']},")
    lines.append("};")
    lines.append("")
    for m in methods:
        lines.append(f"struct yrdawn_args_{m['name']} {{")
        for a in m["args"]:
            lines.append(f"    {to_c_type(a['ctype'])} {a['name']};")
        lines.append("};")
        lines.append("")
    # Client-stub declarations. Skip methods with skip_client_stub
    # (their user-facing API lives in client.c). Mirror the calling
    # convention from emit_client_c so the .c definitions match.
    lines.append("struct yetty_yrdawn_client;")
    lines.append("typedef void (*yetty_yrdawn_reply_cb)(void *user, uint32_t status, uint32_t method_id,")
    lines.append("                                     const uint8_t *body, size_t body_len);")
    lines.append("")
    for m in methods:
        if m.get("skip_client_stub"):
            continue
        is_async = bool(m.get("async"))
        if m.get("free_members"):
            struct = m["free_members"]
            an = m["free_members_arg"]
            lines.append(f"void yrdawn_client_{m['name']}(struct yetty_yrdawn_client *c, "
                         f"{struct} {an});")
            continue
        # Inject async cb/user pair if the method has a callback_info
        # arg (we mark is_async=True for those). Don't inject twice.
        cb_already = is_async
        params: list[str] = ["struct yetty_yrdawn_client *c"]
        args_by_name = {a["name"]: a for a in m["args"]}
        order = m.get("client_param_order")
        iter_args = ([args_by_name[n] for n in order if n in args_by_name]
                     if order else m["args"])
        for a in iter_args:
            kind = a.get("wgpu_kind")
            if kind in ("out_handle", "present_flag", "callback_info"):
                continue
            if kind == "pointer_to_pod" or kind == "pointer_to_encodable":
                params.append(f"{to_c_type(a['ctype'])} const *{a['name']}")
            elif kind == "stringview":
                params.append(f"WGPUStringView {a['name']}")
            elif kind == "array_count":
                params.append(f"size_t {a['name']}")
            elif kind == "handle_array":
                params.append(f"{a['ctype']} const *{a['name']}")
            elif kind == "scalar_array":
                params.append(f"{a['ctype']} const *{a['name']}")
            elif kind == "byte_array":
                params.append(f"void const *{a['name']}")
            elif kind == "out_byte_array":
                params.append(f"void *{a['name']}")
            elif kind == "out_struct":
                params.append(f"{to_c_type(a['ctype'])} *{a['name']}")
            elif kind == "opaque_in_handle":
                params.append(f"uint64_t {a['name']}")
            else:
                params.append(f"{to_c_type(a['ctype'])} {a['name']}")
        if cb_already:
            params.append("yetty_yrdawn_reply_cb cb")
            params.append("void *user")
        if m["returns"] == "handle":
            ret_t = "uint64_t"
        elif m["returns"] == "value":
            ret_t = m.get("return_ctype", "uint32_t")
        else:
            ret_t = "struct yetty_ycore_void_result"
        lines.append(f"{ret_t} yrdawn_client_{m['name']}({', '.join(params)});")
    lines.append("")
    lines.append("#ifdef __cplusplus")
    lines.append("}")
    lines.append("#endif")
    lines.append("")
    lines.append("#endif /* YETTY_YRDAWN_METHODS_GEN_H */")
    lines.append("")
    return "\n".join(lines)


def _emit_codec_runtime_decls(lines: list[str], codec_meta: dict | None, side: str) -> None:
    """Forward-declarations for yrdawn_encode_<Struct> / yrdawn_decode_<Struct>.
    Client also gets yrdawn_client_decode_<Struct> (malloc-based) and
    yrdawn_client_free_<Struct> (frees malloc'd inner pointers, used by
    the wgpu*FreeMembers wrappers)."""
    if not codec_meta:
        return
    for n in codec_meta["encodable"]:
        if side == "client":
            lines.append(f"struct yetty_ycore_void_result yrdawn_encode_{n}(const {n} *src, "
                         f"struct yetty_ycore_buffer *out);")
            lines.append(f"int  yrdawn_client_decode_{n}(const uint8_t **src, size_t *rem, {n} *out);")
            lines.append(f"void yrdawn_client_free_{n}({n} *s);")
        else:
            # Server emits encoders too (for output structs) plus the
            # decoders (for descriptor args).
            lines.append(f"struct yetty_ycore_void_result yrdawn_encode_{n}(const {n} *src, "
                         f"struct yetty_ycore_buffer *out);")
            lines.append(f"int  yrdawn_decode_{n}(void *ctx, const uint8_t **src, size_t *rem, "
                         f"{n} *out, struct yrdawn_arena *arena);")


def _emit_codec_bodies(lines: list[str], codec_meta: dict | None, side: str) -> None:
    if not codec_meta:
        return
    chainable = codec_meta.get("chainable", {})
    for n in codec_meta["encodable"]:
        body = emit_struct_codec_body(
            n, codec_meta["struct_fields"], codec_meta["handle_types"],
            codec_meta["aliases"], codec_meta["pod_structs"],
            codec_meta["struct_names"], set(codec_meta["encodable"]),
            chainable)
        joined = "\n".join(body)
        encoder_src, decoder_src = joined.split(f"int yrdawn_decode_{n}(", 1)
        decoder_src = "int yrdawn_decode_" + n + "(" + decoder_src
        if side == "client":
            lines.append(encoder_src)
            cd_lines = emit_client_struct_decoder_body(
                n, codec_meta["struct_fields"], codec_meta["handle_types"],
                codec_meta["aliases"], codec_meta["pod_structs"],
                codec_meta["struct_names"], set(codec_meta["encodable"]),
                chainable)
            lines.append("\n".join(cd_lines))
            cf_lines = emit_client_struct_freer_body(
                n, codec_meta["struct_fields"], codec_meta["handle_types"],
                codec_meta["aliases"], codec_meta["pod_structs"],
                codec_meta["struct_names"], set(codec_meta["encodable"]),
                chainable)
            lines.append("\n".join(cf_lines))
        else:
            # Server needs both: encoder (for output struct → reply) and
            # decoder (for descriptor input args).
            lines.append(encoder_src)
            lines.append(decoder_src)


def _pair_out_byte_arrays(args: list[dict]) -> list[tuple[str, str]]:
    """Walk the wire_args in order, pair each out_byte_array with the
    most-recent preceding array_count. Returns [(data_name, count_name), ...]."""
    out: list[tuple[str, str]] = []
    last_count: str | None = None
    for a in args:
        k = a.get("wgpu_kind")
        if k == "array_count":
            last_count = a["name"]
        elif k == "out_byte_array" and last_count is not None:
            out.append((a["name"], last_count))
    return out


def emit_client_c(methods: list[dict], codec_meta: dict | None = None) -> str:
    lines: list[str] = []
    lines.append("/* Generated by src/yetty/yrdawn/generate.py — DO NOT EDIT. */")
    lines.append("#include <stdlib.h>")
    lines.append("#include <string.h>")
    lines.append("#include <yetty/ycore/types.h>")
    lines.append("#include <yetty/yrdawn/client.h>")
    lines.append("#include <yetty/yrdawn/methods.gen.h>")
    lines.append("")
    _emit_codec_runtime_decls(lines, codec_meta, "client")
    lines.append("")
    _emit_codec_bodies(lines, codec_meta, "client")
    lines.append("")
    for m in methods:
        if m.get("skip_client_stub"):
            continue
        is_async = bool(m.get("async"))

        # FreeMembers: client-only walk over the struct's malloc'd
        # inner pointers. No CMD is sent.
        if m.get("free_members"):
            struct = m["free_members"]
            an = m["free_members_arg"]
            sig = f"void yrdawn_client_{m['name']}(struct yetty_yrdawn_client *c, {struct} {an})"
            lines.append(sig)
            lines.append("{")
            lines.append("    (void)c;")
            lines.append(f"    yrdawn_client_free_{struct}(&{an});")
            lines.append("}")
            lines.append("")
            continue

        # Build the user-facing parameter list.
        # Skip wire-only fields (out_handle/present_flag) — derived
        # client-side. pointer_to_pod shows up as a pointer (matches
        # webgpu.h). pointer_to_encodable and stringview are in the
        # variable-blob, also pointers in the user signature.
        # handle_array exposes (size_t count, T const *items) — both
        # appear in the user signature but neither in the args struct.
        params: list[str] = ["struct yetty_yrdawn_client *c"]
        var_args: list[dict] = []  # in wire order (matches server decode)
        # Wire-order pass: collect var_args (encoded into _body in this
        # order, matches server-side decode order in classified).
        for a in m["args"]:
            kind = a.get("wgpu_kind")
            if kind in ("pointer_to_encodable", "stringview", "array_count",
                        "handle_array", "scalar_array", "byte_array",
                        "out_byte_array", "out_struct"):
                var_args.append(a)
        # User-facing param order: original C arg declaration order.
        # callback_info args are replaced by the (cb, user) pair injected
        # below for async methods.
        args_by_name = {a["name"]: a for a in m["args"]}
        order = m.get("client_param_order")
        iter_args = ([args_by_name[n] for n in order if n in args_by_name]
                     if order else m["args"])
        for a in iter_args:
            kind = a.get("wgpu_kind")
            if kind in ("out_handle", "present_flag", "callback_info"):
                continue
            if kind == "pointer_to_pod":
                params.append(f"{to_c_type(a['ctype'])} const *{a['name']}")
            elif kind == "pointer_to_encodable":
                params.append(f"{to_c_type(a['ctype'])} const *{a['name']}")
            elif kind == "stringview":
                params.append(f"WGPUStringView {a['name']}")
            elif kind == "array_count":
                params.append(f"size_t {a['name']}")
            elif kind == "handle_array":
                params.append(f"{a['ctype']} const *{a['name']}")
            elif kind == "scalar_array":
                params.append(f"{a['ctype']} const *{a['name']}")
            elif kind == "byte_array":
                params.append(f"void const *{a['name']}")
            elif kind == "out_byte_array":
                params.append(f"void *{a['name']}")
            elif kind == "out_struct":
                params.append(f"{to_c_type(a['ctype'])} *{a['name']}")
            elif kind == "opaque_in_handle":
                params.append(f"uint64_t {a['name']}")
            else:
                params.append(f"{to_c_type(a['ctype'])} {a['name']}")
        if is_async:
            params.append("yetty_yrdawn_reply_cb cb")
            params.append("void *user")
        if m["returns"] == "handle":
            ret_t = "uint64_t"
        elif m["returns"] == "value":
            ret_t = m.get("return_ctype", "uint32_t")
        else:
            ret_t = "struct yetty_ycore_void_result"
        sig = f"{ret_t} yrdawn_client_{m['name']}({', '.join(params)})"
        lines.append(sig)
        lines.append("{")
        lines.append(f"    struct yrdawn_args_{m['name']} args = {{0}};")
        if m["returns"] == "handle":
            out = next(a for a in m["args"] if a.get("wgpu_kind") == "out_handle")
            lines.append(f"    uint64_t h = yetty_yrdawn_client_alloc_handle(c);")
            lines.append(f"    args.{out['name']} = h;")
        # Fixed-size args fill the args struct directly. Variable args
        # (pointer_to_encodable, stringview, array_count, handle_array,
        # scalar_array, byte_array, out_struct, callback_info) land in
        # the trailing blob or are handled separately.
        for a in m["args"]:
            kind = a.get("wgpu_kind")
            if kind in ("out_handle", "present_flag", "pointer_to_encodable",
                        "stringview", "array_count", "handle_array",
                        "scalar_array", "byte_array", "out_byte_array",
                        "out_struct", "callback_info"):
                continue
            if kind == "pointer_to_pod":
                lines.append(f"    if ({a['name']}) {{")
                lines.append(f"        args.{a['name']}_present = 1u;")
                lines.append(f"        args.{a['name']} = *{a['name']};")
                lines.append(f"    }}")
            else:
                lines.append(f"    args.{a['name']} = {a['name']};")
        if var_args:
            # Variable-length args appended after the args struct in
            # declaration order. Each has a 1-byte present flag (for
            # pointer-to-encodable) then the encoded form.
            lines.append("    struct yetty_ycore_buffer _body = {0};")
            lines.append("    { struct yetty_ycore_void_result _r = "
                         "yetty_ycore_buffer_write(&_body, &args, sizeof(args)); "
                         "if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }")
            for a in var_args:
                k = a["wgpu_kind"]
                an = a["name"]
                if k == "array_count":
                    lines.append(f"    {{ uint64_t _c = (uint64_t){an}; "
                                 f"struct yetty_ycore_void_result _r = "
                                 f"yetty_ycore_buffer_write(&_body, &_c, sizeof(_c)); "
                                 f"if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }}")
                    continue
                if k == "handle_array":
                    # Look up the paired count: walk var_args backwards
                    # for the preceding handle_array_count entry.
                    cnt = next((x["name"] for x in reversed(var_args)
                                if x["wgpu_kind"] == "array_count"), None)
                    if cnt:
                        lines.append(f"    for (size_t _i = 0; _i < (size_t){cnt}; ++_i) {{")
                        lines.append(f"        uint64_t _hv = (uint64_t)({an}[_i]);")
                        lines.append(f"        struct yetty_ycore_void_result _r = "
                                     f"yetty_ycore_buffer_write(&_body, &_hv, sizeof(_hv));")
                        lines.append("        if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error);")
                        lines.append("    }")
                    continue
                if k == "scalar_array":
                    cnt = next((x["name"] for x in reversed(var_args)
                                if x["wgpu_kind"] == "array_count"), None)
                    elem_ct = a["ctype"]
                    if cnt:
                        lines.append(f"    if ((size_t){cnt} > 0 && {an}) {{")
                        lines.append(f"        struct yetty_ycore_void_result _r = "
                                     f"yetty_ycore_buffer_write(&_body, {an}, "
                                     f"(size_t){cnt} * sizeof({elem_ct}));")
                        lines.append("        if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error);")
                        lines.append("    }")
                    continue
                if k == "byte_array":
                    cnt = next((x["name"] for x in reversed(var_args)
                                if x["wgpu_kind"] == "array_count"), None)
                    if cnt:
                        lines.append(f"    if ((size_t){cnt} > 0 && {an}) {{")
                        lines.append(f"        struct yetty_ycore_void_result _r = "
                                     f"yetty_ycore_buffer_write(&_body, {an}, (size_t){cnt});")
                        lines.append("        if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error);")
                        lines.append("    }")
                    continue
                if k == "pointer_to_encodable":
                    lines.append(f"    {{ uint8_t _f = {an} ? 1u : 0u; "
                                 f"struct yetty_ycore_void_result _r = "
                                 f"yetty_ycore_buffer_write(&_body, &_f, 1); "
                                 f"if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }}")
                    lines.append(f"    if ({an}) {{ struct yetty_ycore_void_result eresult = "
                                 f"yrdawn_encode_{a['ctype']}({an}, &_body); "
                                 f"if (YETTY_IS_ERR(eresult)) yetty_ycore_error_destroy(eresult.error); }}")
                elif k == "stringview":
                    lines.append("    {")
                    lines.append(f"        uint64_t _len = ({an}.length == WGPU_STRLEN || "
                                 f"!{an}.data) ? ({an}.data ? "
                                 f"(uint64_t)strlen({an}.data) : 0u) : (uint64_t){an}.length;")
                    lines.append("        struct yetty_ycore_void_result _r1 = "
                                 "yetty_ycore_buffer_write(&_body, &_len, sizeof(_len));")
                    lines.append("        if (YETTY_IS_ERR(_r1)) yetty_ycore_error_destroy(_r1.error);")
                    lines.append(f"        if (_len > 0 && {an}.data) {{")
                    lines.append("            struct yetty_ycore_void_result _r2 = "
                                 f"yetty_ycore_buffer_write(&_body, {an}.data, (size_t)_len);")
                    lines.append("            if (YETTY_IS_ERR(_r2)) yetty_ycore_error_destroy(_r2.error);")
                    lines.append("        }")
                    lines.append("    }")
            has_out_struct = m.get("has_out_struct", False)
            out_struct_args = [a for a in m["args"] if a.get("wgpu_kind") == "out_struct"]
            # Pair each out_byte_array with the array_count that precedes
            # it in wire order — the client already has that count as a
            # function parameter.
            out_bytes = _pair_out_byte_arrays(m["args"])
            if has_out_struct:
                # Variable-size reply: status (if value-return) +
                # out_byte_arrays + out_structs, in that order.
                lines.append("    uint8_t *_reply_buf = NULL;")
                lines.append("    size_t _reply_len = 0;")
                lines.append("    { struct yetty_ycore_void_result _r = "
                             f"yetty_yrdawn_client_send_cmd_blocking_dyn(c, "
                             f"YETTY_YRDAWN_METHOD_{m['name']}, "
                             "_body.data, _body.size, "
                             "&_reply_buf, &_reply_len, NULL);")
                lines.append("      if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }")
                lines.append("    yetty_ycore_buffer_destroy(&_body);")
                lines.append("    const uint8_t *_rp = _reply_buf;")
                lines.append("    size_t _rrem = _reply_len;")
                if m["returns"] == "value":
                    ret_ct = m.get("return_ctype", "uint32_t")
                    lines.append(f"    {ret_ct} _result = ({ret_ct}){{0}};")
                    lines.append("    if (_rrem >= sizeof(_result)) { "
                                 "memcpy(&_result, _rp, sizeof(_result)); "
                                 "_rp += sizeof(_result); _rrem -= sizeof(_result); }")
                for data_n, count_n in out_bytes:
                    lines.append(f"    if ({data_n} && (size_t){count_n} > 0 && "
                                 f"_rrem >= (size_t){count_n}) {{")
                    lines.append(f"        memcpy({data_n}, _rp, (size_t){count_n});")
                    lines.append(f"        _rp += (size_t){count_n}; _rrem -= (size_t){count_n};")
                    lines.append("    }")
                for oa in out_struct_args:
                    lines.append(f"    if ({oa['name']}) (void)yrdawn_client_decode_"
                                 f"{oa['ctype']}(&_rp, &_rrem, {oa['name']});")
                lines.append("    free(_reply_buf);")
                if m["returns"] == "value":
                    lines.append("    return _result;")
                else:
                    lines.append("    return YETTY_OK_VOID();")
            elif is_async:
                # See the no-var_args branch: async takes priority over
                # value-return because the cb is what carries the result.
                send = (
                    f"yetty_yrdawn_client_send_cmd_async(c, YETTY_YRDAWN_METHOD_{m['name']},"
                    " _body.data, _body.size, cb, user)"
                )
                lines.append(f"    {{ struct yetty_ycore_void_result _r = {send};")
                lines.append("      if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }")
                lines.append("    yetty_ycore_buffer_destroy(&_body);")
                if m["returns"] == "handle":
                    lines.append("    return h;")
                elif m["returns"] == "value":
                    ret_ct = m.get("return_ctype", "uint32_t")
                    lines.append(f"    return ({ret_ct}){{0}};")
                else:
                    lines.append("    return YETTY_OK_VOID();")
            elif m["returns"] == "value":
                ret_ct = m.get("return_ctype", "uint32_t")
                lines.append(f"    {ret_ct} _result = ({ret_ct}){{0}};")
                lines.append("    { struct yetty_ycore_void_result _r = "
                             f"yetty_yrdawn_client_send_cmd_blocking(c, "
                             f"YETTY_YRDAWN_METHOD_{m['name']}, "
                             "_body.data, _body.size, "
                             "&_result, sizeof(_result), NULL);")
                lines.append("      if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }")
                lines.append("    yetty_ycore_buffer_destroy(&_body);")
                lines.append("    return _result;")
            else:
                send = (
                    f"yetty_yrdawn_client_send_cmd_sync(c, YETTY_YRDAWN_METHOD_{m['name']},"
                    " _body.data, _body.size)"
                )
                lines.append(f"    {{ struct yetty_ycore_void_result _r = {send};")
                lines.append("      if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }")
                lines.append("    yetty_ycore_buffer_destroy(&_body);")
                if m["returns"] == "handle":
                    lines.append("    return h;")
                else:
                    lines.append("    return YETTY_OK_VOID();")
        else:
            has_out_struct = m.get("has_out_struct", False)
            out_struct_args = [a for a in m["args"] if a.get("wgpu_kind") == "out_struct"]
            if has_out_struct:
                lines.append("    uint8_t *_reply_buf = NULL;")
                lines.append("    size_t _reply_len = 0;")
                lines.append(f"    {{ struct yetty_ycore_void_result _r = "
                             f"yetty_yrdawn_client_send_cmd_blocking_dyn(c, "
                             f"YETTY_YRDAWN_METHOD_{m['name']}, "
                             f"&args, sizeof(args), &_reply_buf, &_reply_len, NULL);")
                lines.append("      if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }")
                lines.append("    const uint8_t *_rp = _reply_buf;")
                lines.append("    size_t _rrem = _reply_len;")
                if m["returns"] == "value":
                    ret_ct = m.get("return_ctype", "uint32_t")
                    lines.append(f"    {ret_ct} _result = ({ret_ct}){{0}};")
                    lines.append("    if (_rrem >= sizeof(_result)) { "
                                 "memcpy(&_result, _rp, sizeof(_result)); "
                                 "_rp += sizeof(_result); _rrem -= sizeof(_result); }")
                for oa in out_struct_args:
                    lines.append(f"    if ({oa['name']}) (void)yrdawn_client_decode_"
                                 f"{oa['ctype']}(&_rp, &_rrem, {oa['name']});")
                lines.append("    free(_reply_buf);")
                if m["returns"] == "value":
                    lines.append("    return _result;")
                else:
                    lines.append("    return YETTY_OK_VOID();")
            elif is_async:
                # Async methods that ALSO return a value (typically
                # WGPUFuture) — the value is irrelevant on the client
                # side because the server already drove WaitAny to
                # completion before emitting the REPLY; the cb fires
                # when the REPLY arrives carrying the status. Hand back
                # a zero value of the return type so callers that ignore
                # the future are unaffected.
                send = (
                    f"yetty_yrdawn_client_send_cmd_async(c, YETTY_YRDAWN_METHOD_{m['name']},"
                    " &args, sizeof(args), cb, user)"
                )
                if m["returns"] == "handle":
                    lines.append(f"    (void){send};")
                    lines.append(f"    return h;")
                elif m["returns"] == "value":
                    ret_ct = m.get("return_ctype", "uint32_t")
                    lines.append(f"    (void){send};")
                    lines.append(f"    return ({ret_ct}){{0}};")
                else:
                    lines.append(f"    return {send};")
            elif m["returns"] == "value":
                ret_ct = m.get("return_ctype", "uint32_t")
                lines.append(f"    {ret_ct} _result = ({ret_ct}){{0}};")
                lines.append(f"    {{ struct yetty_ycore_void_result _r = "
                             f"yetty_yrdawn_client_send_cmd_blocking(c, "
                             f"YETTY_YRDAWN_METHOD_{m['name']}, "
                             f"&args, sizeof(args), &_result, sizeof(_result), NULL);")
                lines.append("      if (YETTY_IS_ERR(_r)) yetty_ycore_error_destroy(_r.error); }")
                lines.append("    return _result;")
            else:
                send = (
                    f"yetty_yrdawn_client_send_cmd_sync(c, YETTY_YRDAWN_METHOD_{m['name']},"
                    " &args, sizeof(args))"
                )
                if m["returns"] == "handle":
                    lines.append(f"    (void){send};")
                    lines.append(f"    return h;")
                else:
                    lines.append(f"    return {send};")
        lines.append("}")
        lines.append("")
    return "\n".join(lines)


def emit_server_c(methods: list[dict], codec_meta: dict | None = None) -> str:
    lines: list[str] = []
    lines.append("/* Generated by src/yetty/yrdawn/generate.py — DO NOT EDIT. */")
    lines.append("#include <stddef.h>")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#include <stdlib.h>")
    lines.append("#include <string.h>")
    lines.append("#include <webgpu/webgpu.h>")
    lines.append("#include <yetty/ycore/result.h>")
    lines.append("#include <yetty/ycore/types.h>")
    lines.append("#include <yetty/ytrace/ytrace.h>")
    lines.append("#include <yetty/yrdawn/methods.gen.h>")
    lines.append("#include <yetty/yrdawn/server.h>")
    lines.append("#include <yetty/yrdawn/wire.h>")
    lines.append("")
    # Closure used by every async trampoline. Heap-allocated, owned by the
    # trampoline once the dispatcher hands control to Dawn.
    lines.append("struct yrdawn_async_cb {")
    lines.append("    void *ctx;")
    lines.append("    uint32_t req_id;")
    lines.append("    uint32_t method_id;")
    lines.append("    uint64_t result_handle;")
    lines.append("};")
    lines.append("")
    _emit_codec_runtime_decls(lines, codec_meta, "server")
    lines.append("")
    _emit_codec_bodies(lines, codec_meta, "server")
    lines.append("")
    for m in methods:
        if m.get("trampoline"):
            lines.append(m["trampoline"])
            lines.append("")
    lines.append("uint32_t yrdawn_server_dispatch(void *ctx, uint32_t method_id, uint32_t req_id,")
    lines.append("                                const void *body, size_t body_len)")
    lines.append("{")
    lines.append("    (void)req_id;")
    lines.append("    switch (method_id) {")
    for m in methods:
        lines.append(f"    case YETTY_YRDAWN_METHOD_{m['name']}: {{")
        lines.append(f"        if (body_len < sizeof(struct yrdawn_args_{m['name']}))")
        lines.append(f"            return YETTY_YRDAWN_REPLY_VALIDATION_ERROR;")
        lines.append(
            f"        const struct yrdawn_args_{m['name']} *a = "
            f"(const struct yrdawn_args_{m['name']} *)body;"
        )
        body_src = m.get("server_body")
        if body_src:
            for ln in body_src.splitlines():
                lines.append(ln if ln.startswith(" ") else "        " + ln)
        else:
            lines.append("        (void)a;")
            lines.append("        return YETTY_YRDAWN_REPLY_OK;")
        lines.append("    }")
    lines.append("    default:")
    lines.append("        return YETTY_YRDAWN_REPLY_UNKNOWN_METHOD;")
    lines.append("    }")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def write_if_changed(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text() == content:
        return
    path.write_text(content)


def main(argv: list[str]) -> int:
    # Defaults point to the in-tree committed locations relative to this
    # script (src/yetty/yrdawn/generate.py). Header lives in include/,
    # .c files alongside the generator.
    here = pathlib.Path(__file__).resolve().parent
    repo = here.parent.parent.parent  # src/yetty/yrdawn → repo root
    default_methods_h   = repo / "include" / "yetty" / "yrdawn" / "methods.gen.h"
    default_client_c    = here / "client-stubs.gen.c"
    default_server_c    = here / "server-dispatch.gen.c"
    # Read from the committed in-tree webgpu.h so regen is deterministic
    # and independent of whatever Dawn happens to be fetched in any
    # build tree (Android / webasm prebuilts ship slimmer headers that
    # would silently strip types from the wire protocol). Override with
    # --webgpu-h when intentionally regenerating against a different
    # Dawn version, then re-copy the new header into include/yetty/
    # yrdawn/dawn/webgpu.h to make the change part of the source.
    default_webgpu_h    = repo / "include" / "yetty" / "yrdawn" / "dawn" / "webgpu.h"

    ap = argparse.ArgumentParser()
    ap.add_argument("--out-methods-h", type=pathlib.Path, default=default_methods_h)
    ap.add_argument("--out-client-c",  type=pathlib.Path, default=default_client_c)
    ap.add_argument("--out-server-c",  type=pathlib.Path, default=default_server_c)
    ap.add_argument("--webgpu-h",      type=pathlib.Path, default=default_webgpu_h,
                    help="Dawn webgpu.h to auto-discover wgpu* entrypoints from")
    ns = ap.parse_args(argv)

    methods_full = merge_with_auto(METHODS, ns.webgpu_h)
    # Split off the codec metadata sentinel if present (only when
    # webgpu.h was found and we ran the discovery pass).
    codec_meta: dict | None = None
    methods = []
    for m in methods_full:
        if "_codec_meta" in m:
            codec_meta = m["_codec_meta"]
        else:
            methods.append(m)
    auto_n = sum(1 for m in methods if m.get("auto"))
    sys.stderr.write(
        f"yrdawn-gen: {len(METHODS)} curated + {auto_n} auto-discovered = "
        f"{len(methods)} total methods\n"
    )

    write_if_changed(ns.out_methods_h, emit_methods_h(methods))
    write_if_changed(ns.out_client_c, emit_client_c(methods, codec_meta))
    write_if_changed(ns.out_server_c, emit_server_c(methods, codec_meta))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
