#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""RPC / remote-object code generation for the yclass codegen.

Everything here is specific to the wire protocol: the per-slot public stub's
remote branch, the server-side skeletons, the per-class create() handshake, the
wire-arg packing, and the registration hooks. codegen.py owns the parse, the
model, the class accessors and the public headers, and calls into this module
for the RPC pieces.

This is the ONE place that looks inside a Result: on the wire path it reads the
return type's fixed `.ok`/`.value`/`.error` fields (the YETTY_YRESULT_DECLARE
layout) and `sizeof(r.value)` to marshal the call across a session — never by
reconstructing a result type name.

KNOWN BUG (issue #340): the error path is NOT yet symmetric with the value
path. On a remote failure only a 1-byte status flag crosses the wire and the
client fabricates a generic error; the server's real message + cause chain are
dropped. They must instead be serialized and rebuilt+chained on the client.
"""

import re

from codegen import (
    HEADER,
    VOID_RESULT_ID,
    qualified_class,
    qualified_slot,
    result_type,
    result_type_id,
    is_struct_ptr,
    is_buffer,
)

def ret_value_type(rid: str, results: dict):
    """Underlying value C type for a Result identifier (its wire payload), read
    from the value-field map clang gave us — no hardcoded table. None for the
    void result (no payload)."""
    if rid == VOID_RESULT_ID:
        return None
    value_by_id = results["value_by_id"]
    if rid in value_by_id:
        return value_by_id[rid]
    sys.stderr.write(
        f"error: result id '{rid}' has no parsed `struct {rid}_result`, so its "
        f"value type is unknown. It must be declared via YETTY_YRESULT_DECLARE "
        f"somewhere the module's sources include.\n")
    sys.exit(1)

def wire_type(t: str) -> str:
    if is_struct_ptr(t):
        return "uint64_t"
    if is_buffer(t):
        # Only the size lives in the packed header; the payload bytes are
        # appended to the body after the packed struct, in declared order.
        return "uint32_t"
    return t.strip()

def wire_name(arg: dict) -> str:
    if is_struct_ptr(arg["type"]):
        return f"{arg['name']}_handle"
    if is_buffer(arg["type"]):
        return f"{arg['name']}_len"
    return arg["name"]

def wire_args(m: dict) -> list:
    """All args except ctx."""
    return m["args"][1:]

def args_struct_body(args: list, indent: str) -> str:
    if not args:
        return f"{indent}char _empty;\n"
    return "".join(f"{indent}{wire_type(a['type'])} {wire_name(a)};\n" for a in args)

def emit_dispatch_body(m: dict, results: dict) -> str:
    args = m["args"]
    rid = result_type_id(m["return_type"], results)
    vt = ret_value_type(rid, results)
    slot_fn = f"{qualified_slot(m)}_fn"
    ctx_name = args[0]["name"]
    obj_name = args[1]["name"]
    call_args = ", ".join(a["name"] for a in args)
    qs = qualified_slot(m)

    # Local-only slot: the public stub is dispatch + NULL-check, no
    # session/RPC branch. Args that don't survive marshalling (raw
    # producer pointers to local carriers) stay in-process by
    # construction.
    if m.get("local"):
        return f"""\
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {{
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_{m['domain']}", (yetty_yclass_method_id_t){qs});
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR({rid}, "{qs}: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }}

    if (!{obj_name}) return YETTY_ERR({rid}, "{qs}: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class({obj_name});
    YETTY_RETURN_IF_ERR({rid}, object_class_r, "{qs}: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR({rid}, dispatch_impl_r, "{qs}: dispatch_lookup failed");
    return (({slot_fn})dispatch_impl_r.value)({call_args});
"""

    wargs = wire_args(m)
    init_parts = []
    for i, a in enumerate(wargs):
        if is_struct_ptr(a["type"]):
            # Every struct-ptr wire arg (obj at i=0 plus any extra
            # object handles) was minted on this side by `<class>_create`
            # and is a `struct yetty_yclass_proxy *` wearing a `struct
            # yetty_yclass_object *` (or class-specific *) hat. The
            # handle is the proxy's `handle` field, reached via
            # container_of so the layout is alignment-correct on 32-bit
            # ABIs (the old `*(uint64_t *)(obj + 1)` byte-pun was
            # misaligned when sizeof(yclass_object) == 4).
            init_parts.append(
                f"container_of((struct yetty_yclass_object *){a['name']}, "
                f"struct yetty_yclass_proxy, header)->handle")
        elif is_buffer(a["type"]):
            # Buffer arg: only the payload size lives in the packed header.
            # The payload bytes are appended to the wire body after the
            # packed struct (see buffer-aware remote_call below).
            init_parts.append(f"(uint32_t){a['name']}.size")
        else:
            init_parts.append(a["name"])
    init = ", ".join(init_parts)
    fields = args_struct_body(wargs, indent="            ")
    buf_args = [a for a in wargs if is_buffer(a["type"])]

    # Build the wire body. No buffer args = fast path, body is
    # &wire_args / sizeof(wire_args). With buffer args we heap-allocate
    # `sizeof(wire_args) +
    # Σ size_i`, copy the packed scalars, then concatenate every blob's
    # bytes in declared order; the skel decodes by reading the same
    # lengths back out of the packed header.
    if buf_args:
        buf_total_terms = " + ".join(
            [f"sizeof(wire_args)"] + [f"(size_t){a['name']}.size" for a in buf_args])
        buf_copies = "".join(
            f"        memcpy(body_buf + body_offset, {a['name']}.data, {a['name']}.size);\n"
            f"        body_offset += {a['name']}.size;\n"
            for a in buf_args
        )
        body_setup = f"""\
        size_t body_total = {buf_total_terms};
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) return YETTY_ERR({rid}, "{qs}: body buf oom");
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
{buf_copies}\
"""
        body_arg = "body_buf, body_total"
        body_cleanup = "        free(body_buf);\n"
    else:
        body_setup = ""
        body_arg = "&wire_args, sizeof(wire_args)"
        body_cleanup = ""

    # Wire response: status byte (0=OK, 1=ERR) + optional payload.
    # For void slots the payload is empty so resp_len == 1 on success;
    # for value slots it's 1 + sizeof(value).
    if vt is None:
        remote_call = f"""\
{body_setup}\
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r = yetty_yclass_rpc_call(
            rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, {body_arg},
            resp_buf, sizeof(resp_buf));
{body_cleanup}\
        YETTY_RETURN_IF_ERR({rid}, rpc_call_r, "{qs}: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) return YETTY_ERR({rid}, "{qs}: short RPC response");
        if (resp_buf[0] != 0) return YETTY_ERR({rid}, "{qs}: remote impl returned error");
        return YETTY_OK_VOID();
"""
    else:
        remote_call = f"""\
{body_setup}\
        uint8_t resp_buf[1 + sizeof({vt})];
        struct yetty_ycore_size_result rpc_call_r = yetty_yclass_rpc_call(
            rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, {body_arg},
            resp_buf, sizeof(resp_buf));
{body_cleanup}\
        YETTY_RETURN_IF_ERR({rid}, rpc_call_r, "{qs}: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) return YETTY_ERR({rid}, "{qs}: short RPC response");
        if (resp_buf[0] != 0) return YETTY_ERR({rid}, "{qs}: remote impl returned error");
        if (response_len != sizeof(resp_buf)) return YETTY_ERR({rid}, "{qs}: truncated RPC payload");
        {vt} return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK({rid}, return_value);
"""

    return f"""\
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {{
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_{m['domain']}", (yetty_yclass_method_id_t){qs});
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR({rid}, "{qs}: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }}

    if (!{obj_name}) return YETTY_ERR({rid}, "{qs}: NULL object");

    struct yetty_yclass_ctx *rpc_ctx = {ctx_name};
    if (rpc_ctx && rpc_ctx->session) {{
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR({rid}, remote_id_r, "{qs}: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {{
{fields}\
        }} wire_args = {{ {init} }};
#pragma pack(pop)
{remote_call}\
    }} else {{
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class({obj_name});
        YETTY_RETURN_IF_ERR({rid}, object_class_r, "{qs}: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR({rid}, dispatch_impl_r, "{qs}: dispatch_lookup failed");
        return (({slot_fn})dispatch_impl_r.value)({call_args});
    }}
"""

def emit_stem_hooks(classes: list, wire_methods: list, module: str, stem_id: str) -> str:
    """Per-stem registration hooks, appended into <stem>.gen.c. Each stem owns
    a private accessor-lookup + skel-lookup over ITS OWN classes/skels (the
    skels are `static` in this same TU, so the table can reference them), and a
    non-static `yetty_<module>_<stem>_register_hooks()` that installs both. The
    module-wide `yetty_<module>_register()` (emitted once, in the primary stem)
    calls every stem's hooks."""
    class_branches = "\n".join(
        f'    if (strcmp(name, "{qualified_class(c)}") == 0) return {c["accessor"]}();'
        for c in classes)
    accessor = f"""
/* ---- {module}/{stem_id}: class name -> accessor ---------------------- */
static struct yetty_yclass_ptr_result yetty_{module}_{stem_id}_accessor_lookup(const char *name)
{{
{class_branches}
    return YETTY_OK(yetty_yclass_ptr, NULL);
}}
"""
    if wire_methods:
        skel_rows = ",\n".join(
            f'    {{"{qualified_slot(m)}", {qualified_slot(m)}_skel}}' for m in wire_methods)
        rows_id = f"yetty_{module}_{stem_id}_skel_rows"
        skel = f"""
struct yetty_{module}_{stem_id}_skel_row {{ const char *name; yetty_yclass_rpc_skel_fn fn; }};
static const struct yetty_{module}_{stem_id}_skel_row {rows_id}[] = {{
{skel_rows}
}};
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_{module}_{stem_id}_skel_lookup(yetty_yclass_method_slot slot)
{{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {{ yetty_ycore_error_destroy(slot_name_r.error); return NULL; }}
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof({rows_id}) / sizeof({rows_id}[0]); ++i)
        if (strcmp({rows_id}[i].name, name) == 0)
            return {rows_id}[i].fn;
    return NULL;
}}
"""
        skel_install = (
            f"    {{\n"
            f"        struct yetty_ycore_void_result add_skel_r =\n"
            f"            yetty_yclass_rpc_add_skel_lookup(yetty_{module}_{stem_id}_skel_lookup);\n"
            f"        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,\n"
            f'                            "yetty_{module}_{stem_id}_register_hooks: skel");\n'
            f"    }}\n"
        )
    else:
        skel = ""
        skel_install = ""
    return f"""{accessor}{skel}
struct yetty_ycore_void_result yetty_{module}_{stem_id}_register_hooks(void)
{{
    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_{module}_{stem_id}_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_{module}_{stem_id}_register_hooks: accessor");
{skel_install}    return YETTY_OK_VOID();
}}
"""

def emit_module_register(module: str, all_stem_ids: list) -> str:
    """The single module entry point — emitted once, into the primary stem. It
    forward-declares and calls every stem's register_hooks()."""
    decls = "\n".join(
        f"struct yetty_ycore_void_result yetty_{module}_{s}_register_hooks(void);"
        for s in all_stem_ids)
    calls = "\n".join(
        f"    {{\n"
        f"        struct yetty_ycore_void_result hook_r = yetty_{module}_{s}_register_hooks();\n"
        f'        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_{module}_register: {s}");\n'
        f"    }}"
        for s in all_stem_ids)
    return f"""
/* ===== module registration (was rpc.gen.c) ========================== */
{decls}

struct yetty_ycore_void_result yetty_{module}_register(void)
{{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();
{calls}
    registered = true;
    return YETTY_OK_VOID();
}}
"""

def emit_stem_methods_and_rpc(classes: list, model: dict, module: str, stem_id: str,
                              all_stem_ids: list, is_primary: bool, results: dict) -> str:
    """The methods.gen.c + rpc.gen.c content for ONE stem's classes, appended
    into its <stem>.gen.c. Stubs/skels reference only types already in scope in
    the hand-written <stem>.c (their impls live there), so no header include is
    needed and the redefine-an-`expose`d-struct hazard never arises."""
    class_keys = {(c["domain"], c["name"]) for c in classes}
    stem_methods = [m for m in model.get("methods", [])
                    if (m["domain"], m.get("owning_class")) in class_keys]
    wire_methods = [m for m in stem_methods if not m.get("local")]
    regular = [c for c in classes if c.get("type") == "regular"]

    parts = []
    if stem_methods:
        parts.append("\n/* ===== public method stubs (was methods.gen.c) ===== */\n\n")
        for m in stem_methods:
            params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
            rt = result_type(m["return_type"], results)
            parts.append(f"{rt} {qualified_slot(m)}({params})\n{{\n{emit_dispatch_body(m, results)}}}\n\n")

    parts.append("\n/* ===== rpc skeletons + create (was rpc.gen.c) ===== */\n\n")
    for m in wire_methods:
        parts.append(emit_skel(m, results))
        parts.append("\n")
    for c in regular:
        parts.append(emit_create_fn(c, model, module))
        parts.append("\n")

    parts.append(emit_stem_hooks(classes, wire_methods, module, stem_id))
    if is_primary:
        parts.append(emit_module_register(module, all_stem_ids))
    return "".join(parts)

def emit_skel(m: dict, results: dict) -> str:
    """Unpack the wire body, resolve obj handle, re-enter the public stub
    with a local ctx so the right override fires on the actual class.
    The wire response carries a status byte (0=OK, 1=ERR); for value
    slots the OK payload is the raw value bytes that follow.

    The skel itself returns size_t per the rpc_skel_fn contract
    (the RPC engine calls it as a fn-pointer); Result-shaped failures
    surfaced from handle_resolve / the user impl are encoded as the
    1-byte status=1 wire response and the skel returns 1."""
    slot = qualified_slot(m)
    rid = result_type_id(m["return_type"], results)
    vt = ret_value_type(rid, results)
    args = wire_args(m)
    fields = args_struct_body(args, indent="        ")

    # Pre-resolve every struct-ptr arg as a separate statement — each
    # handle_resolve now returns a Result, so we must unpack it (and
    # bail with a 1-byte ERR response on failure) before the call.
    # Buffer args get reconstructed as local `struct yetty_ycore_buffer`
    # values pointing at slices of the wire body (zero-copy on the
    # server side — the impl sees the bytes for the duration of the
    # call, and is forbidden from holding onto `.data` past return).
    resolves = []
    call_parts = ["&local_ctx"]
    buf_args = [a for a in args if is_buffer(a["type"])]
    for a in args:
        if is_struct_ptr(a["type"]):
            var = f"{a['name']}_resolve_r"
            resolves.append(
                f"""\
    struct yetty_yclass_void_ptr_result {var} =
        yetty_yclass_rpc_handle_resolve(wire_args.{wire_name(a)});
    if (YETTY_IS_ERR({var})) {{
        yetty_ycore_error_print(stderr,
            "[skel] {slot}: handle_resolve", {var}.error);
        yetty_ycore_error_destroy({var}.error);
        if (resp_max < 1) return 0;
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }}
""")
            call_parts.append(f"({a['type'].strip()}){var}.value")
        elif is_buffer(a["type"]):
            call_parts.append(f"{a['name']}_buf")
        else:
            call_parts.append(f"wire_args.{a['name']}")
    resolve_block = "".join(resolves)
    call = ", ".join(call_parts)

    if buf_args:
        # Server-side framing: after the packed scalar header, the body
        # carries each buffer's payload bytes back-to-back in declared
        # order. Walk them, reconstruct local buffer values, and check
        # the total length matches exactly — leftover bytes mean signature
        # drift between the two sides.
        len_terms = " + ".join(
            ["sizeof(wire_args)"] + [f"(size_t)wire_args.{wire_name(a)}" for a in buf_args])
        buf_block_lines = [
            f"    if (body_len < sizeof(wire_args)) return 0;",
            f"    memcpy(&wire_args, body, sizeof(wire_args));",
            f"    if (body_len != {len_terms}) return 0;",
            f"    size_t body_offset = sizeof(wire_args);",
        ]
        for a in buf_args:
            wn = wire_name(a)
            buf_block_lines += [
                f"    struct yetty_ycore_buffer {a['name']}_buf = {{",
                f"        .data = (uint8_t *)((const uint8_t *)body + body_offset),",
                f"        .size = (size_t)wire_args.{wn},",
                f"        .capacity = (size_t)wire_args.{wn},",
                f"    }};",
                f"    body_offset += (size_t)wire_args.{wn};",
            ]
        unpack_block = "\n".join(buf_block_lines) + "\n"
    else:
        unpack_block = (
            "    /* Strict length match — both sides regenerate from the same\n"
            "     * annotated source; a size mismatch means signature drift, and\n"
            "     * silently truncating to the local prefix would let the server\n"
            "     * execute against a misaligned struct. */\n"
            "    if (body_len != sizeof(wire_args)) return 0;\n"
            "    memcpy(&wire_args, body, sizeof(wire_args));\n"
        )

    rt = f"struct {rid}_result"
    if vt is None:
        body = f"""\
    {rt} call_r = {slot}({call});
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {{
        yetty_ycore_error_print(stderr, "[skel] {slot}", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }}
    ((uint8_t *)resp)[0] = 0;
    return 1;
"""
    else:
        body = f"""\
    {rt} call_r = {slot}({call});
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {{
        yetty_ycore_error_print(stderr, "[skel] {slot}", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }}
    if (resp_max < 1 + sizeof(call_r.value)) return 0;
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
"""

    return f"""\
/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. */
YETTY_EXTERNAL_CALLBACK
static size_t {slot}_skel(const void *body, size_t body_len,
                          void *resp, size_t resp_max)
{{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {{
{fields}\
    }} wire_args;
#pragma pack(pop)
{unpack_block}\
    struct yetty_yclass_ctx local_ctx = {{0}};
{resolve_block}\
{body}}}
"""

def emit_create_fn(cls: dict, model: dict, module: str) -> str:
    """Per-class factory. ctx decides; caller is location-agnostic.

    If the module declares a `constructor` slot (any class in the
    module overrides `<module>:<class>:constructor`), the factory's
    local branch invokes that slot automatically after allocating —
    so the returned object is fully constructed in one call. No
    separate _init step; the caller-side flow is identical
    regardless of whether the class has a constructor or not."""
    accessor = cls["accessor"]
    qname = qualified_class(cls)
    has_constructor = any(
        m["domain"] == module and m["slot"] == "constructor"
        for m in model.get("methods", [])
    )
    if has_constructor:
        ctor_call = f"""\

        struct yetty_ycore_void_result ctor_r =
            yetty_{module}_constructor(ctx, alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {{
            struct yetty_ycore_void_result free_r =
                yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) yetty_ycore_error_destroy(free_r.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "{qname}_create: constructor failed", ctor_r);
        }}"""
    else:
        ctor_call = ""
    return f"""\
struct yetty_yclass_object_ptr_result {qname}_create(struct yetty_yclass_ctx *ctx)
{{
    ydebug("class={qname}");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = {accessor}();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "{qname}_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {{
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;{ctor_call}
        return alloc_r;
    }}

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {{
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "{qname}");
        if (YETTY_IS_ERR(translate_class_r)) {{
            yetty_ycore_error_print(stderr,
                "{qname}_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }}
    }}

    uint64_t handle = 0;
    const char *class_name = "{qname}";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "{qname}_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "{qname}_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "{qname}_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}}
"""
