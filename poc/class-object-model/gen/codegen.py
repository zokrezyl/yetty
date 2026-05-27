#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""Annotated C -> all generated artefacts for one MODULE.

Each module has its own annotated sources, public-stub header, RPC
plumbing, and skel table. Modules are independent — adding one doesn't
touch another.

Reads annotated .c sources, builds the in-memory model, emits:

  Public (under <include_base>/<module>/):
    <class>.h          — accessor decl, includes sibling methods.gen.h.
                         GENERATED (replaces any hand-written class header).
    methods.gen.h      — every public stub in this module.
    rpc.gen.h          — every <class>_create() in this module.

  Internal (under <module_src_dir>/):
    <class>.gen.inc    — accessor body, included at the foot of <class>.c.
    methods.gen.c      — public stub bodies.
    rpc.gen.c          — skels + accessor/skel lookup hooks + constructor.
    model.yaml         — informational dump.

Method signature contract:
  RetT slot(struct ctx *ctx, struct object *obj, <rest...>);

  ctx is *not* on the wire. Public stub branches on ctx->session:
    NULL → local: vtable dispatch via obj->klass.
    set  → remote: look up remote_id via xlat (batched per-class via
           rpc_session_translate_class, lazy fallback via
           rpc_session_ensure_remote_id), then rpc_call(RPC_OP_CALL, rid).

Object creation:
  <class>_create(ctx) — local: object_alloc; remote: triggers per-class
  translate handshake, then rpc_call(RPC_OP_CREATE, "<name>"), wraps the
  returned handle in a proxy with the same class accessor.

Usage:
  ./codegen.py <module> <include_base> <module_src_dir> <source.c>...

  <include_base>      shared include root, e.g. "include". Generated
                      public headers land in <include_base>/<module>/.
  <module_src_dir>    where the annotated .c files live. Generated
                      internal artefacts (.gen.inc, .gen.c, model.yaml)
                      land here.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path


HEADER = "/* GENERATED — do not edit. */\n"


# ============== model — annotated C → in-memory dict ====================
#
# Annotation schema (role-first, colon-separated, class name always
# second segment):
#
#   class:<CLASS>                 on accessor decl  — regular class
#   mixin:<CLASS>                 on accessor decl  — mixin class
#   data:<CLASS>                  on struct decl    — class's data slice
#   override:<CLASS>:<SLOT>       on impl fn        — register impl for SLOT on CLASS
#   parent:<CLASS>                on accessor decl  — parent class
#   uses:<CLASS>                  on accessor decl  — included mixin
#
# Methods are inferred — every distinct SLOT seen across all `override:`
# annotations becomes a public method. Its signature is taken from the
# first impl encountered for that slot.

def ast_dump(path: Path, include_dirs: list) -> dict:
    clang = os.environ.get("CLANG", "clang")
    cmd = [clang, "-Xclang", "-ast-dump=json", "-fsyntax-only", "-std=c2x"]
    for d in include_dirs:
        cmd.append(f"-I{d}")
    cmd.append(str(path))
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        sys.exit(1)
    return json.loads(r.stdout)


def _annotate_value(attr_node: dict, src: bytes):
    rng = attr_node.get("range", {})
    beg = rng.get("begin", {}); end = rng.get("end", {})
    if "offset" not in beg or "offset" not in end:
        return None
    start = beg["offset"]
    stop = end["offset"] + end.get("tokLen", 1)
    blob = src[start:stop].decode("utf-8", errors="replace")
    m = re.search(r'annotate\s*\(\s*"([^"]*)"', blob)
    return m.group(1) if m else None


def _collect_annotations(decl: dict, src: bytes) -> list:
    out = []
    for child in decl.get("inner", []):
        if child.get("kind") == "AnnotateAttr":
            v = _annotate_value(child, src)
            if v: out.append(v)
    return out


def _fn_args(decl: dict) -> list:
    args = []
    for child in decl.get("inner", []):
        if child.get("kind") == "ParmVarDecl":
            args.append({
                "name": child.get("name") or "",
                "type": child.get("type", {}).get("qualType", ""),
            })
    return args


def _parse_return_type(qual_type: str) -> str:
    m = re.match(r"^(.*?)\s*\((.*)\)$", qual_type.strip())
    return m.group(1).strip() if m else qual_type


def _walk_decls(node: dict):
    if node.get("kind") in ("FunctionDecl", "RecordDecl"):
        yield node
    for child in node.get("inner", []):
        yield from _walk_decls(child)


def _split_ann(ann: str):
    parts = [p.strip() for p in ann.split(":")]
    return parts[0], parts[1:]


def parse_sources(include_dirs: list, sources: list) -> dict:
    methods: dict = {}
    classes: dict = {}

    def bucket(name: str) -> dict:
        if name not in classes:
            classes[name] = {
                "name": name, "accessor": None, "type": None,
                "source_file": None, "parent": None, "mixins": [],
                "data": None, "ops": [],
            }
        return classes[name]

    for path in sources:
        src = path.read_bytes()
        tu = ast_dump(path, include_dirs)
        for decl in _walk_decls(tu):
            anns = _collect_annotations(decl, src)
            if not anns: continue
            kind = decl.get("kind")

            if kind == "FunctionDecl":
                for ann in anns:
                    role, args = _split_ann(ann)
                    if role == "override" and len(args) >= 2:
                        cls, slot = args[0], args[1]
                        b = bucket(cls)
                        b["ops"].append({"slot": slot, "impl": decl["name"]})
                        if slot not in methods:
                            qt = decl.get("type", {}).get("qualType", "")
                            methods[slot] = {
                                "slot": slot, "owning_class": cls,
                                "return_type": _parse_return_type(qt),
                                "args": _fn_args(decl),
                            }
            elif kind == "RecordDecl":
                # class:X / mixin:X on a struct subsume the old data:X —
                # the annotated struct IS the class's data slice.
                primary, kind2 = None, None
                for ann in anns:
                    role, args = _split_ann(ann)
                    if role == "class" and args:
                        primary, kind2 = args[0], "regular"; break
                    if role == "mixin" and args:
                        primary, kind2 = args[0], "mixin"; break
                if primary:
                    b = bucket(primary)
                    b["type"] = kind2
                    b["source_file"] = str(path)
                    suffix = "_mixin_get" if kind2 == "mixin" else "_class_get"
                    b["accessor"] = primary + suffix
                    tag = decl.get("name")
                    if tag:
                        b["data"] = f"struct {tag}"
                    for ann in anns:
                        role, args = _split_ann(ann)
                        if role == "parent" and args:
                            b["parent"] = args[0]
                        elif role == "uses" and args:
                            b["mixins"].append(args[0])

    return {
        "methods": list(methods.values()),
        "classes": [c for c in classes.values() if c["accessor"]],
    }


# ============== yaml writer (informational dump) ========================

def _yaml_scalar(v) -> str:
    if v is None: return "null"
    if isinstance(v, bool): return "true" if v else "false"
    if isinstance(v, (int, float)): return str(v)
    s = str(v)
    if any(ch in s for ch in ":#[]{}&*!|>'\"%@`") or s != s.strip():
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s


def yaml_dump(obj, indent=0) -> str:
    pad = "  " * indent
    if isinstance(obj, dict):
        if not obj: return "{}"
        out = []
        for k, v in obj.items():
            if isinstance(v, dict) and v:
                out.append(f"{pad}{k}:\n{yaml_dump(v, indent + 1)}")
            elif isinstance(v, list) and v:
                out.append(f"{pad}{k}:\n{yaml_dump(v, indent + 1)}")
            elif isinstance(v, list):
                out.append(f"{pad}{k}: []")
            elif isinstance(v, dict):
                out.append(f"{pad}{k}: {{}}")
            else:
                out.append(f"{pad}{k}: {_yaml_scalar(v)}")
        return "\n".join(out)
    if isinstance(obj, list):
        if not obj: return f"{pad}[]"
        out = []
        for item in obj:
            if isinstance(item, dict):
                lines = yaml_dump(item, indent + 1).splitlines()
                if lines: lines[0] = pad + "- " + lines[0].lstrip()
                out.append("\n".join(lines))
            else:
                out.append(f"{pad}- {_yaml_scalar(item)}")
        return "\n".join(out)
    return f"{pad}{_yaml_scalar(obj)}"


# ---------------- helpers -------------------------------------------------

def is_struct_ptr(t: str) -> bool:
    return bool(re.match(r"^\s*(const\s+)?struct\s+\w+\s*\*\s*$", t))


def is_specific_struct_ptr(t: str, name: str) -> bool:
    return bool(re.match(rf"^\s*(const\s+)?struct\s+{name}\s*\*\s*$", t))


def wire_type(t: str) -> str:
    return "uint64_t" if is_struct_ptr(t) else t.strip()


def wire_name(arg: dict) -> str:
    return f"{arg['name']}_handle" if is_struct_ptr(arg["type"]) else arg["name"]


def struct_names_in(*types: str) -> set:
    out = set()
    for t in types:
        for m in re.finditer(r"\bstruct\s+(\w+)", t):
            out.add(m.group(1))
    return out


def default_return_for(ret: str) -> str:
    ret = ret.strip()
    if ret == "void":
        return ""
    return f"({ret}){{0}}"


def validate_method(m: dict):
    args = m["args"]
    if len(args) < 2:
        sys.stderr.write(
            f"error: method {m['slot']} needs (ctx*, obj*, ...). got {len(args)}\n")
        sys.exit(1)
    if not is_specific_struct_ptr(args[0]["type"], "ctx"):
        sys.stderr.write(
            f"error: method {m['slot']}: first arg must be 'struct ctx *' "
            f"(got '{args[0]['type']}')\n")
        sys.exit(1)
    if not is_specific_struct_ptr(args[1]["type"], "object"):
        sys.stderr.write(
            f"error: method {m['slot']}: second arg must be 'struct object *' "
            f"(got '{args[1]['type']}')\n")
        sys.exit(1)
    # Extra struct-ptr args after obj would silently marshal as NULL on the
    # wire — bail rather than ship broken behaviour.
    for a in args[2:]:
        if is_struct_ptr(a["type"]):
            sys.stderr.write(
                f"error: method {m['slot']}: arg '{a['name']}' is a struct "
                f"pointer ({a['type']}). Only the obj at arg[1] is supported "
                f"as a wire handle. Pass scalars or extend the wire format.\n")
            sys.exit(1)


def wire_args(m: dict) -> list:
    """All args except ctx."""
    return m["args"][1:]


def args_struct_body(args: list, indent: str) -> str:
    if not args:
        return f"{indent}char _empty;\n"
    return "".join(f"{indent}{wire_type(a['type'])} {wire_name(a)};\n" for a in args)


# ---------------- methods.gen.h ------------------------------------------

def emit_methods_h(model: dict, module: str, out_path: Path):
    """Public-stub-only header. Knows nothing about RPC — anyone who only
    wants to *call* methods includes this and gets typed declarations.
    Lives at include/<module>/methods.gen.h. Per-class headers in the
    same directory `#include "methods.gen.h"` (sibling)."""
    guard = f"POC_{module.upper()}_METHODS_GEN_H"
    parts = [HEADER]
    parts.append(f"#ifndef {guard}\n#define {guard}\n\n")
    parts.append('#include "class.h"\n\n')

    structs = set()
    for m in model["methods"]:
        structs |= struct_names_in(m["return_type"])
        for a in m["args"]:
            structs |= struct_names_in(a["type"])
    for known in ("ctx", "object", "class", "str"):
        structs.discard(known)
    for s in sorted(structs):
        parts.append(f"struct {s};\n")
    parts.append("\n")

    for m in model["methods"]:
        params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
        parts.append(f"{m['return_type']} {m['slot']}({params});\n")

    parts.append("\n#endif\n")
    out_path.write_text("".join(parts))


# ---------------- methods.gen.c — unified public stub --------------------

def emit_dispatch_body(m: dict) -> str:
    args = m["args"]
    ret = m["return_type"].strip()
    ctx_name = args[0]["name"]
    obj_name = args[1]["name"]
    cast_params = ", ".join(a["type"] for a in args)
    call_args = ", ".join(a["name"] for a in args)
    null_ret = "return;" if ret == "void" else f"return {default_return_for(ret)};"

    wargs = wire_args(m)
    init_parts = []
    for i, a in enumerate(wargs):
        if is_struct_ptr(a["type"]):
            if i == 0:
                init_parts.append(
                    f"*(uint64_t *)((char *){a['name']} + sizeof(*{a['name']}))")
            else:
                init_parts.append("0")
        else:
            init_parts.append(a["name"])
    init = ", ".join(init_parts)
    fields = args_struct_body(wargs, indent="            ")

    if ret == "void":
        remote_call = (
            "        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), NULL, 0);\n"
            "        return;\n"
        )
    else:
        remote_call = (
            f"        {ret} _r = {{0}};\n"
            "        rpc_call(_s->session, RPC_OP_CALL, _rid, &_a, sizeof(_a), &_r, sizeof(_r));\n"
            "        return _r;\n"
        )

    if ret == "void":
        local_call = (
            f"        if (fn) ((void (*)({cast_params}))fn)({call_args});\n"
            "        return;\n"
        )
    else:
        local_call = (
            f"        if (!fn) {null_ret}\n"
            f"        return (({ret} (*)({cast_params}))fn)({call_args});\n"
        )

    return f"""\
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED)
        _slot = method_slot_get((method_id_t){m['slot']});

    if (!{obj_name}) {{ {null_ret} }}

    struct ctx *_s = {ctx_name};
    if (_s && _s->session) {{
        uint32_t _rid = rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED) {{ {null_ret} }}
        struct __attribute__((packed)) {{
{fields}\
        }} _a = {{ {init} }};
{remote_call}\
    }} else {{
        impl_t fn = class_dispatch_lookup(object_class({obj_name}), _slot);
{local_call}\
    }}
"""


def emit_methods_c(model: dict, module: str, out_path: Path):
    parts = [HEADER,
             f'#include "{module}/methods.gen.h"\n',
             '#include "rpc.h"\n',
             '#include <stdint.h>\n#include <string.h>\n\n']
    for m in model["methods"]:
        params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
        parts.append(f"{m['return_type']} {m['slot']}({params})\n{{\n"
                     f"{emit_dispatch_body(m)}}}\n\n")
    out_path.write_text("".join(parts))


# ---------------- .gen.inc — class accessor body -------------------------

def emit_class_accessor(cls: dict, class_index: dict) -> str:
    accessor = cls["accessor"]
    is_mixin = cls["type"] == "mixin"
    data = cls["data"] or "char"
    type_const = "CLASS_TYPE_MIXIN" if is_mixin else "CLASS_TYPE_REGULAR"
    op_lines = [
        f'        {{"{op["slot"]}", (method_id_t){op["slot"]}, '
        f"(impl_t){op['impl']}}},"
        for op in cls["ops"]
    ]
    ops_block = "\n".join(op_lines)
    parent = cls.get("parent")
    if parent:
        p = class_index.get(parent)
        if not p:
            sys.stderr.write(f"unknown parent class: {parent}\n"); sys.exit(1)
        parent_expr = f"{p['accessor']}()"
    else:
        parent_expr = "NULL"
    mixins = cls.get("mixins") or []
    if mixins:
        resolved = []
        for mname in mixins:
            mm = class_index.get(mname)
            if not mm:
                sys.stderr.write(f"unknown mixin class: {mname}\n"); sys.exit(1)
            resolved.append(f"{mm['accessor']}()")
        mixin_inits = ", ".join(resolved)
        mixin_decl = f"    const struct class *mixins[] = {{ {mixin_inits} }};\n"
        mixin_arg = "mixins"
        mixin_count = str(len(mixins))
    else:
        mixin_decl = ""
        mixin_arg = "NULL"
        mixin_count = "0"
    return f"""\
const struct class *{accessor}(void)
{{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {{
        .name = "{cls['name']}",
        .type = {type_const},
        .data_size = sizeof({data}),
    }};
    static const struct op ops[] = {{
{ops_block}
    }};
{mixin_decl}\
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         {parent_expr}, {mixin_arg}, {mixin_count});
    return cls;
}}
"""


def emit_class_public_headers(model: dict, module: str, include_module_dir: Path):
    """One generated public header per class. Declares the accessor and
    pulls the module's public method stubs (sibling header in the same
    directory) so a single `#include "<module>/<class>.h"` is enough to
    instantiate AND call methods on that class."""
    for cls in model.get("classes", []):
        is_mixin = cls["type"] == "mixin"
        suffix = "_mixin_get" if is_mixin else "_class_get"
        kind = "mixin" if is_mixin else "regular class"
        name = cls["name"]
        guard = f"POC_{module.upper()}_{name.upper()}_H"
        header_path = include_module_dir / f"{name}.h"
        header_path.write_text(
            HEADER
            + f"/* Public interface for {kind} `{name}` (module: {module}).\n"
            + " * GENERATED — do not edit. Edit the annotated source under "
            + f"src/{module}/ instead. */\n"
            + f"#ifndef {guard}\n#define {guard}\n\n"
            + '#include "class.h"\n'
            + '#include "methods.gen.h"  /* every public method stub in this module */\n\n'
            + f"const struct class *{name}{suffix}(void);\n\n"
            + "#endif\n"
        )


def emit_class_gen_c(model: dict, module: str, module_dir: Path):
    """One `<class>.gen.c` per annotated source. `#include`d at the foot
    of the matching hand-written `<class>.c`. The generator pulls every
    header the accessor body needs (the class's own .h for the
    prototype, any parent and mixin headers for their accessor calls)
    so the hand-written .c stays minimal — typically only
    `#include "class.h"`."""
    class_index = {c["name"]: c for c in model["classes"]}
    groups: dict = {}
    for c in model["classes"]:
        groups.setdefault(c["source_file"], []).append(c)
    for src_path, classes in groups.items():
        inc_path = module_dir / (Path(src_path).stem + ".gen.c")

        # Collect headers needed for this .gen.c: each class's own + parents + mixins.
        needed = set()
        for c in classes:
            needed.add(f"{module}/{c['name']}.h")
            if c.get("parent"):
                needed.add(f"{module}/{c['parent']}.h")
            for mx in c.get("mixins", []):
                needed.add(f"{module}/{mx}.h")
        include_block = "".join(f'#include "{h}"\n' for h in sorted(needed))

        body = HEADER + include_block + "\n" \
             + "\n".join(emit_class_accessor(c, class_index) for c in classes)
        inc_path.write_text(body)


# ---------------- rpc.gen.{h,c} ------------------------------------------

def regular_classes(model: dict) -> list:
    return [c for c in model.get("classes", []) if c.get("type") == "regular"]


def class_header_for(cls: dict, module: str) -> str:
    return f"{module}/{Path(cls['source_file']).stem}.h"


def emit_skel(m: dict) -> str:
    """Unpack the wire body, resolve obj handle, re-enter the public stub
    with a local ctx so the right override fires on the actual class."""
    slot = m["slot"]
    ret = m["return_type"].strip()
    args = wire_args(m)
    fields = args_struct_body(args, indent="        ")

    call_parts = ["&_local"]
    for a in args:
        if is_struct_ptr(a["type"]):
            call_parts.append(
                f"({a['type'].strip()})rpc_handle_resolve(_a.{wire_name(a)})")
        else:
            call_parts.append(f"_a.{a['name']}")
    call = ", ".join(call_parts)

    if ret == "void":
        body = (
            f"    {slot}({call});\n"
            "    (void)_resp; (void)_resp_max;\n"
            "    return 0;\n"
        )
    else:
        body = (
            f"    {ret} _r = {slot}({call});\n"
            "    if (_resp_max < sizeof(_r)) return 0;\n"
            "    memcpy(_resp, &_r, sizeof(_r));\n"
            "    return sizeof(_r);\n"
        )

    return f"""\
static size_t {slot}_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{{
    struct __attribute__((packed)) {{
{fields}\
    }} _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct ctx _local = {{0}};
{body}}}
"""


def emit_create_fn(cls: dict) -> str:
    """Per-class factory. ctx decides; caller is location-agnostic."""
    name = cls["name"]
    accessor = cls["accessor"]
    return f"""\
struct object *{name}_create(struct ctx *ctx)
{{
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    const struct class *_klass = {accessor}();

    if (!ctx || !ctx->session)
        return object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    rpc_session_translate_class(ctx->session, "{name}");

    uint64_t _h = 0;
    const char *_name = "{name}";
    if (rpc_call(ctx->session, RPC_OP_CREATE, 0, _name, strlen(_name),
                 &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return NULL;

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct object) + sizeof(uint64_t));
    if (!_mem) return NULL;
    struct object *_obj = _mem;
    *(const struct class **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return _obj;
}}
"""


def emit_lookup_tables(model: dict, module: str) -> str:
    classes = model.get("classes", [])
    methods = model.get("methods", [])

    class_branches = "\n".join(
        f'    if (strcmp(name, "{c["name"]}") == 0) return {c["accessor"]}();'
        for c in classes
    )

    skel_rows = ",\n".join(
        f'    {{"{m["slot"]}", {m["slot"]}_skel}}'
        for m in methods
    )

    return f"""\
/* ---- {module}: class name → accessor (lazy) ---------------------- */

static const struct class *{module}_accessor_lookup(const char *name)
{{
{class_branches}
    return NULL;
}}

/* ---- {module}: slot → skel, name-keyed static data --------------- */

struct {module}_skel_row {{ const char *name; rpc_skel_fn fn; }};

static const struct {module}_skel_row {module}_skel_rows[] = {{
{skel_rows}
}};

static rpc_skel_fn {module}_skel_lookup(method_slot slot)
{{
    const char *name = method_slot_name(slot);
    if (!name) return NULL;
    for (size_t i = 0; i < sizeof({module}_skel_rows) / sizeof({module}_skel_rows[0]); ++i)
        if (strcmp({module}_skel_rows[i].name, name) == 0)
            return {module}_skel_rows[i].fn;
    return NULL;
}}

/* ---- {module}: install hooks before main ------------------------- */

__attribute__((constructor))
static void {module}_install_hooks(void)
{{
    class_add_accessor_lookup({module}_accessor_lookup);
    rpc_add_skel_lookup({module}_skel_lookup);
}}
"""


def emit_rpc_h(model: dict, module: str, out_path: Path):
    guard = f"POC_{module.upper()}_RPC_GEN_H"
    decls = "\n".join(
        f"struct object *{c['name']}_create(struct ctx *ctx);"
        for c in regular_classes(model)
    )
    out_path.write_text(
        HEADER
        + f"#ifndef {guard}\n#define {guard}\n\n"
        + '#include "rpc.h"\n\n'
        + (decls + "\n\n" if decls else "")
        + "#endif\n"
    )


def emit_rpc_c(model: dict, module: str, out_path: Path):
    class_includes = "".join(
        f'#include "{class_header_for(c, module)}"\n'
        for c in model.get("classes", [])
    )
    parts = [HEADER,
             '#include "rpc.h"\n',
             f'#include "{module}/rpc.gen.h"\n',
             f'#include "{module}/methods.gen.h"\n',
             '#include "class.h"\n',
             class_includes,
             '#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n\n']
    for m in model["methods"]:
        parts.append(emit_skel(m))
        parts.append("\n")
    for c in regular_classes(model):
        parts.append(emit_create_fn(c))
        parts.append("\n")
    parts.append(emit_lookup_tables(model, module))
    out_path.write_text("".join(parts))


# ---------------- main ---------------------------------------------------

def main():
    if len(sys.argv) < 5:
        sys.stderr.write(__doc__)
        sys.exit(2)
    module = sys.argv[1]
    include_base = Path(sys.argv[2])
    module_src = Path(sys.argv[3])
    sources = [Path(p) for p in sys.argv[4:]]

    include_module = include_base / module
    include_module.mkdir(parents=True, exist_ok=True)
    module_src.mkdir(parents=True, exist_ok=True)

    # Pre-touch placeholders so clang -fsyntax-only can resolve the
    # #includes the annotated sources pull in before the real generated
    # content has been emitted on the very first invocation. Per-class
    # headers seed with `#include "class.h"` so the runtime structs
    # (ctx, object, class, str) are visible during AST parsing.
    placeholder_class_h = '#include "class.h"\n'
    for s in sources:
        if s.suffix == ".c":
            inc = module_src / (s.stem + ".gen.c")
            if not inc.exists():
                inc.write_text("")
            hdr = include_module / (s.stem + ".h")
            if not hdr.exists():
                hdr.write_text(placeholder_class_h)
    for stub in ("methods.gen.h", "rpc.gen.h"):
        p = include_module / stub
        if not p.exists():
            p.write_text(placeholder_class_h)

    # Clang search path: shared include/, the module's own generated-
    # header dir (include/<module>/), and the module src dir (for
    # any neighbour .h that might still exist transiently).
    model = parse_sources([include_base, include_module, module_src], sources)
    for m in model["methods"]:
        validate_method(m)

    emit_class_public_headers(model, module, include_module)
    emit_methods_h(model, module, include_module / "methods.gen.h")
    emit_methods_c(model, module, module_src / "methods.gen.c")
    emit_class_gen_c(model, module, module_src)
    emit_rpc_h(model, module, include_module / "rpc.gen.h")
    emit_rpc_c(model, module, module_src / "rpc.gen.c")

    (module_src / "model.yaml").write_text(yaml_dump(model) + "\n")


if __name__ == "__main__":
    main()
