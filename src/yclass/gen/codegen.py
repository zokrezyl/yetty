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
    <class>.gen.c      — accessor body, included at the foot of <class>.c.
    methods.gen.c      — public stub bodies.
    rpc.gen.c          — skels + accessor/skel lookup hooks + constructor.
    model.yaml         — informational dump.

Symbol naming:
  Every generated C identifier visible at link time is prefixed
  `yetty_<module>_<class>` / `yetty_<module>_<slot>`. The same
  `yetty_<module>_<localname>` form is also the slot_table key and the
  wire label — one canonical name, three uses.

Method signature contract:
  RetT slot(struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, <rest...>);

  ctx is *not* on the wire. Public stub branches on ctx->session:
    NULL → local: vtable dispatch via obj->klass.
    set  → remote: look up remote_id via xlat (batched per-class via
           yetty_yclass_rpc_session_translate_class, lazy fallback via
           yetty_yclass_rpc_session_ensure_remote_id), then
           yetty_yclass_rpc_call(YETTY_YCLASS_RPC_OP_CALL, rid).

Object creation:
  <class>_create(ctx) — local: yetty_yclass_object_alloc; remote: triggers
  per-class translate handshake, then yetty_yclass_rpc_call(CREATE,
  "<name>"), wraps the returned handle in a proxy with the same class
  accessor.

Usage:
  ./codegen.py <module> <include_base> <module_src_dir> <source.c>...

  <include_base>      shared include root, e.g. "include". Generated
                      public headers land in <include_base>/<module>/.
  <module_src_dir>    where the annotated .c files live. Generated
                      internal artefacts (.gen.c, model.yaml) land here.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path


HEADER = "/* GENERATED — do not edit. */\n"


# Qualified-name helpers: every generated C identifier visible at link
# time gets a `yetty_<module>_<localname>` prefix. The same form is the
# slot_table key and the wire label.
def qualified_class(c: dict) -> str:
    return f"yetty_{c['domain']}_{c['name']}"

def qualified_slot(m: dict) -> str:
    return f"yetty_{m['domain']}_{m['slot']}"

def qualified(domain: str, name: str) -> str:
    return f"yetty_{domain}_{name}"

def op_c_name(op: dict) -> str:
    """C identifier of the public stub for this op's slot."""
    return f"yetty_{op['slot_domain']}_{op['slot']}"

# Slot_table key: the per-domain hash key used at runtime. Must match
# the qualified-name string the C symbol carries, so the wire label
# (slot_qname) and the C symbol of the public stub round-trip.
def slot_table_qname(domain: str, name: str) -> str:
    return f"yetty_{domain}_{name}"


def result_type_id(ret: str) -> str:
    """Map an impl's return type to the YETTY_YRESULT_DECLARE identifier.
    Every impl now returns a Result, so the canonical input here is a
    `struct <id>_result` — we peel `_result` off. Raw scalar / pointer
    inputs are still accepted (handy for tests), in which case the id
    is the conventional one (yetty_ycore_int, yetty_ycore_void, …).
    Identifiers used here MUST exist (be declared via
    YETTY_YRESULT_DECLARE somewhere visible to the .gen.c)."""
    r = ret.strip()
    m = re.match(r"^struct\s+(\w+)_result\s*$", r)
    if m:
        return m.group(1)
    if r == "void":
        return "yetty_ycore_void"
    if r == "int":
        return "yetty_ycore_int"
    if r == "size_t":
        return "yetty_ycore_size"
    m = re.match(r"^struct\s+(\w+)\s*\*\s*$", r)
    if m:
        return f"{m.group(1)}_ptr"
    m = re.match(r"^struct\s+(\w+)\s*$", r)
    if m:
        return m.group(1)
    return r


def result_type(ret: str) -> str:
    """The full C type — `struct <id>_result`."""
    return f"struct {result_type_id(ret)}_result"


def ret_value_type(rid: str):
    """Underlying value C type for a Result identifier. None for void
    (no payload). Mirrors the YETTY_YRESULT_DECLARE table in
    yetty/ycore/result.h."""
    if rid == "yetty_ycore_void":
        return None
    if rid == "yetty_ycore_int":
        return "int"
    if rid == "yetty_ycore_size":
        return "size_t"
    if rid.endswith("_ptr"):
        return f"struct {rid[:-4]} *"
    return f"struct {rid}"


# ============== model — annotated C → in-memory dict ====================
#
# Annotation schema. Every annotation is `<verb>@<domain>:<path...>` —
# the `@` separates the role from the colon-separated path. The first
# `<domain>` segment is the impl/owning module's domain (must match the
# codegen's --module argument). `parent` and `uses` may name a foreign
# domain (cross-module reference).
#
#   class@<DOMAIN>:<CLASS>                            on data struct  — regular class
#   mixin@<DOMAIN>:<CLASS>                            on data struct  — mixin class
#   parent@<DOMAIN>:<CLASS>                           on data struct  — parent class
#   uses@<DOMAIN>:<MIXIN>                             on data struct  — included mixin
#   override@<DOMAIN>:<CLASS>:<SLOT>                  on impl fn      — same-domain
#                                                                       override (slot lives
#                                                                       in <DOMAIN>)
#   override@<DOMAIN>:<CLASS>:<SLOT_DOMAIN>:<SLOT>    on impl fn      — cross-domain
#                                                                       override; the impl
#                                                                       class is in <DOMAIN>,
#                                                                       the slot is owned by
#                                                                       <SLOT_DOMAIN> (a
#                                                                       different module).
#
# Methods are inferred — every distinct SLOT whose owning domain equals
# the current module becomes a public method. Its C signature is taken
# from the first impl encountered. Cross-domain overrides do NOT
# introduce a new method entry in this module: the slot's public stub
# is owned by the slot's home module and is included from there.

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
    """Annotation grammar: `<verb>@<domain>:<…>`. The `@` separates the
    role from the colon-separated path. Legacy `verb:path` is rejected."""
    if "@" not in ann:
        sys.stderr.write(
            f"error: annotation '{ann}' lacks '@' separator; "
            f"expected `<verb>@<domain>:<path...>`\n")
        sys.exit(1)
    verb, rest = ann.split("@", 1)
    return verb.strip(), [p.strip() for p in rest.split(":") if p.strip()]


def parse_sources(include_dirs: list, sources: list, module: str) -> dict:
    """Walk annotated sources, build the model. `module` is the
    codegen's CLI argument; the `<domain>` segment in every class/
    mixin/override annotation MUST equal it. parent/uses annotations
    may name a different domain (cross-module reference). Override
    signatures are NOT checked here — the generator emits a C-side
    typedef assignment per override that the compiler type-checks."""
    methods: dict = {}
    classes: dict = {}

    def bucket(name: str) -> dict:
        if name not in classes:
            classes[name] = {
                "name": name, "domain": None, "accessor": None, "type": None,
                "source_file": None, "parent": None, "mixins": [],
                "data": None, "ops": [],
            }
        return classes[name]

    def require_segments(role: str, args: list, n: int, shape: str):
        if len(args) != n:
            sys.stderr.write(
                f"error: '{role}@{':'.join(args)}' — expected {shape}\n")
            sys.exit(1)

    def require_local_domain(role: str, dom: str):
        if dom != module:
            sys.stderr.write(
                f"error: '{role}' domain '{dom}' != current module '{module}'. "
                f"class/mixin/override must live in their own module's domain.\n")
            sys.exit(1)

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
                    if role == "override":
                        # Two shapes accepted:
                        #   3 segs — slot lives in the impl class's domain
                        #            (same-module override)
                        #   4 segs — slot's domain explicit; may differ
                        #            (cross-module override)
                        if len(args) == 3:
                            impl_dom, cls, slot = args
                            slot_dom = impl_dom
                        elif len(args) == 4:
                            impl_dom, cls, slot_dom, slot = args
                        else:
                            sys.stderr.write(
                                f"error: 'override@{':'.join(args)}' — expected "
                                "override@<DOMAIN>:<CLASS>:<SLOT> or "
                                "override@<DOMAIN>:<CLASS>:<SLOT_DOMAIN>:<SLOT>\n")
                            sys.exit(1)
                        require_local_domain(role, impl_dom)
                        b = bucket(cls)
                        b["ops"].append({
                            "slot": slot,
                            "slot_domain": slot_dom,
                            "impl": decl["name"],
                        })
                        # Only emit a public stub for slots OWNED by this
                        # module. A cross-domain override targets a slot
                        # whose stub already lives in the slot's home
                        # module.
                        if slot_dom == module and slot not in methods:
                            qt = decl.get("type", {}).get("qualType", "")
                            methods[slot] = {
                                "slot": slot, "domain": slot_dom,
                                "owning_class": cls,
                                "return_type": _parse_return_type(qt),
                                "args": _fn_args(decl),
                            }
            elif kind == "RecordDecl":
                # class@<D>:<C> / mixin@<D>:<C> sit on the data struct.
                primary, kind2, primary_dom = None, None, None
                for ann in anns:
                    role, args = _split_ann(ann)
                    if role == "class":
                        require_segments(role, args, 2, "class@<DOMAIN>:<CLASS>")
                        primary_dom, primary, kind2 = args[0], args[1], "regular"
                        break
                    if role == "mixin":
                        require_segments(role, args, 2, "mixin@<DOMAIN>:<CLASS>")
                        primary_dom, primary, kind2 = args[0], args[1], "mixin"
                        break
                if primary:
                    require_local_domain(kind2, primary_dom)
                    b = bucket(primary)
                    b["domain"] = primary_dom
                    b["type"] = kind2
                    b["source_file"] = str(path)
                    suffix = "_mixin_get" if kind2 == "mixin" else "_class_get"
                    b["accessor"] = f"yetty_{primary_dom}_{primary}{suffix}"
                    tag = decl.get("name")
                    if tag:
                        b["data"] = f"struct {tag}"
                    for ann in anns:
                        role, args = _split_ann(ann)
                        if role == "parent":
                            require_segments(role, args, 2, "parent@<DOMAIN>:<CLASS>")
                            b["parent"] = {"domain": args[0], "name": args[1]}
                        elif role == "uses":
                            require_segments(role, args, 2, "uses@<DOMAIN>:<MIXIN>")
                            b["mixins"].append({"domain": args[0], "name": args[1]})

    return {
        "methods": list(methods.values()),
        "classes": [c for c in classes.values() if c["accessor"]],
    }


# Signature validation for cross-domain (and same-domain) overrides is
# done in *C*, not here. Each public stub gets a per-slot function-
# pointer typedef in methods.gen.h (see emit_methods_h). Each override
# emits a file-scope `static <slot>_fn _check_… = <impl>;` line in
# the class's .gen.c (see emit_class_accessor). If the impl signature
# doesn't match the slot's, the C compiler errors at the assignment.


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
    if not is_specific_struct_ptr(args[0]["type"], "yetty_yclass_ctx"):
        sys.stderr.write(
            f"error: method {m['slot']}: first arg must be 'struct yetty_yclass_ctx *' "
            f"(got '{args[0]['type']}')\n")
        sys.exit(1)
    if not is_specific_struct_ptr(args[1]["type"], "yetty_yclass_object"):
        sys.stderr.write(
            f"error: method {m['slot']}: second arg must be 'struct yetty_yclass_object *' "
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
    guard = f"YETTY_{module.upper()}_METHODS_GEN_H"
    parts = [HEADER]
    parts.append(f"#ifndef {guard}\n#define {guard}\n\n")
    parts.append('#include <yclass/class.h>\n\n')

    structs = set()
    for m in model["methods"]:
        structs |= struct_names_in(m["return_type"])
        for a in m["args"]:
            structs |= struct_names_in(a["type"])
    for known in ("yetty_yclass_ctx", "yetty_yclass_object", "yetty_yclass"):
        structs.discard(known)
    for s in sorted(structs):
        parts.append(f"struct {s};\n")
    parts.append("\n")

    for m in model["methods"]:
        params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
        type_only = ", ".join(a["type"] for a in m["args"])
        rt = result_type(m["return_type"])
        # Public-stub declaration (returns Result) + a function-pointer
        # typedef any override impl can be type-checked against from
        # its own .gen.c. Impls return the same Result type.
        parts.append(f"{rt} {qualified_slot(m)}({params});\n")
        parts.append(f"typedef {rt} (*{qualified_slot(m)}_fn)({type_only});\n")

    parts.append("\n#endif\n")
    out_path.write_text("".join(parts))


# ---------------- methods.gen.c — unified public stub --------------------

def emit_dispatch_body(m: dict) -> str:
    args = m["args"]
    rid = result_type_id(m["return_type"])
    vt = ret_value_type(rid)
    slot_fn = f"{qualified_slot(m)}_fn"
    ctx_name = args[0]["name"]
    obj_name = args[1]["name"]
    call_args = ", ".join(a["name"] for a in args)

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

    qs = qualified_slot(m)

    # Wire response: status byte (0=OK, 1=ERR) + optional payload.
    # For void slots the payload is empty so resp_len == 1 on success;
    # for value slots it's 1 + sizeof(value).
    if vt is None:
        remote_call = f"""\
        uint8_t _wbuf[1];
        size_t _wn = yetty_yclass_rpc_call(_s->session, YETTY_YCLASS_RPC_OP_CALL, _rid,
                                           &_a, sizeof(_a), _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YETTY_ERR({rid}, "{qs}: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR({rid}, "{qs}: remote impl returned error");
        return YETTY_OK_VOID();
"""
    else:
        remote_call = f"""\
        uint8_t _wbuf[1 + sizeof({vt})];
        size_t _wn = yetty_yclass_rpc_call(_s->session, YETTY_YCLASS_RPC_OP_CALL, _rid,
                                           &_a, sizeof(_a), _wbuf, sizeof(_wbuf));
        if (_wn < 1) return YETTY_ERR({rid}, "{qs}: short RPC response");
        if (_wbuf[0] != 0) return YETTY_ERR({rid}, "{qs}: remote impl returned error");
        if (_wn != sizeof(_wbuf)) return YETTY_ERR({rid}, "{qs}: truncated RPC payload");
        {vt} _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YETTY_OK({rid}, _v);
"""

    return f"""\
    static yetty_yclass_method_slot _slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {{
        struct yetty_yclass_method_slot_result _sr =
            yetty_yclass_method_slot_get("{m['domain']}", (yetty_yclass_method_id_t){qs});
        if (YETTY_IS_ERR(_sr))
            return YETTY_ERR({rid}, "{qs}: method_slot_get failed", _sr);
        _slot = _sr.value;
    }}

    if (!{obj_name}) return YETTY_ERR({rid}, "{qs}: NULL object");

    struct yetty_yclass_ctx *_s = {ctx_name};
    if (_s && _s->session) {{
        uint32_t _rid = yetty_yclass_rpc_session_ensure_remote_id(_s->session, _slot);
        if (_rid == YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED)
            return YETTY_ERR({rid}, "{qs}: remote id unresolved");
        struct __attribute__((packed)) {{
{fields}\
        }} _a = {{ {init} }};
{remote_call}\
    }} else {{
        yetty_yclass_impl_t fn =
            yetty_yclass_dispatch_lookup(yetty_yclass_object_class({obj_name}), _slot);
        if (!fn) return YETTY_ERR({rid}, "{qs}: no impl on this class");
        return (({slot_fn})fn)({call_args});
    }}
"""


def emit_methods_c(model: dict, module: str, out_path: Path):
    parts = [HEADER,
             f'#include "{module}/methods.gen.h"\n',
             '#include <yclass/rpc.h>\n',
             '#include <yetty/ycore/result.h>\n',
             '#include <yetty/ytrace/ytrace.h>\n',
             '#include <stdint.h>\n#include <string.h>\n\n']
    for m in model["methods"]:
        params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
        rt = result_type(m["return_type"])
        parts.append(f"{rt} {qualified_slot(m)}({params})\n{{\n"
                     f"{emit_dispatch_body(m)}}}\n\n")
    out_path.write_text("".join(parts))


# ---------------- .gen.c — class accessor body ---------------------------

def emit_class_accessor(cls: dict) -> str:
    accessor = cls["accessor"]
    is_mixin = cls["type"] == "mixin"
    data = cls["data"] or "char"
    type_const = "YETTY_YCLASS_TYPE_MIXIN" if is_mixin else "YETTY_YCLASS_TYPE_REGULAR"

    # Compile-time signature check: assigning each override impl to a
    # variable of the slot's function-pointer typedef forces the C
    # compiler to verify return type + parameter types match. Mismatch
    # ⇒ compile error at this line, well before the impl_t cast in the
    # ops table erases types. Works for both same-domain and cross-
    # domain overrides — the typedef lives in <slot_domain>/methods.gen.h
    # which the .gen.c already includes.
    #
    # The check variable name MUST include slot_domain — a class that
    # overrides the same local slot name from two different domains
    # would otherwise collide on the static variable name.
    qcls = qualified_class(cls)
    typecheck_lines = [
        f"__attribute__((unused))\n"
        f"static {op_c_name(op)}_fn _{qcls}_{op_c_name(op)}_check = {op['impl']};"
        for op in cls["ops"]
    ]
    typecheck_block = "\n".join(typecheck_lines)
    if typecheck_block:
        typecheck_block += "\n\n"

    # Each op references its slot by (slot_domain, local_name). The
    # slot_table is per-domain (see yetty_yclass_slot_table_get), so
    # the runtime can locate the right table without any string
    # splitting. The slot's C public-stub identifier is
    # "yetty_<domain>_<local_name>".
    op_lines = [
        f'        {{"{op["slot_domain"]}", "{op["slot"]}", '
        f"(yetty_yclass_method_id_t){op_c_name(op)}, "
        f"(yetty_yclass_impl_t){op['impl']}}},"
        for op in cls["ops"]
    ]
    ops_block = "\n".join(op_lines)

    qname = qualified_class(cls)

    parent = cls.get("parent")
    if parent:
        parent_accessor = f"yetty_{parent['domain']}_{parent['name']}_class_get"
        parent_block = (
            f"    struct yetty_yclass_ptr_result _parent_r = {parent_accessor}();\n"
            f"    if (YETTY_IS_ERR(_parent_r))\n"
            f"        return YETTY_ERR(yetty_yclass_ptr, "
            f"\"{qname}_class_get: parent accessor failed\", _parent_r);\n"
        )
        parent_expr = "_parent_r.value"
    else:
        parent_block = ""
        parent_expr = "NULL"

    mixins = cls.get("mixins") or []
    if mixins:
        mixin_lines = []
        mixin_values = []
        for i, m in enumerate(mixins):
            mixin_accessor = f"yetty_{m['domain']}_{m['name']}_mixin_get"
            mixin_lines.append(
                f"    struct yetty_yclass_ptr_result _mixin{i}_r = {mixin_accessor}();\n"
                f"    if (YETTY_IS_ERR(_mixin{i}_r))\n"
                f"        return YETTY_ERR(yetty_yclass_ptr, "
                f"\"{qname}_class_get: mixin{i} accessor failed\", _mixin{i}_r);\n"
            )
            mixin_values.append(f"_mixin{i}_r.value")
        mixin_block = "".join(mixin_lines)
        mixin_block += (
            f"    const struct yetty_yclass *mixins[] = "
            f"{{ {', '.join(mixin_values)} }};\n"
        )
        mixin_arg = "mixins"
        mixin_count = str(len(mixins))
    else:
        mixin_block = ""
        mixin_arg = "NULL"
        mixin_count = "0"

    return f"""\
{typecheck_block}\
struct yetty_yclass_ptr_result {accessor}(void)
{{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class={qname}");

    static const struct yetty_yclass_descriptor desc = {{
        .name = "{qname}",
        .type = {type_const},
        .data_size = sizeof({data}),
    }};
    static const struct yetty_yclass_op ops[] = {{
{ops_block}
    }};
{parent_block}\
{mixin_block}\
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              {parent_expr}, {mixin_arg}, {mixin_count});
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(yetty_yclass_ptr, "{qname}_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
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
        qname = qualified_class(cls)
        guard = f"YETTY_{module.upper()}_{name.upper()}_H"
        header_path = include_module_dir / f"{name}.h"
        header_path.write_text(
            HEADER
            + f"/* Public interface for {kind} `{name}` (module: {module}).\n"
            + " * GENERATED — do not edit. Edit the annotated source under "
            + f"src/{module}/ instead. */\n"
            + f"#ifndef {guard}\n#define {guard}\n\n"
            + '#include <yclass/class.h>\n'
            + '#include "methods.gen.h"  /* every public method stub in this module */\n\n'
            + f"struct yetty_yclass_ptr_result {qname}{suffix}(void);\n\n"
            + "#endif\n"
        )


def emit_class_gen_c(model: dict, module: str, module_dir: Path):
    """One `<class>.gen.c` per annotated source. `#include`d at the foot
    of the matching hand-written `<class>.c`. The generator pulls every
    header the accessor body needs (the class's own .h for the
    prototype, any parent and mixin headers for their accessor calls)
    so the hand-written .c stays minimal — typically only
    `#include <yclass/class.h>`."""
    groups: dict = {}
    for c in model["classes"]:
        groups.setdefault(c["source_file"], []).append(c)
    for src_path, classes in groups.items():
        inc_path = module_dir / (Path(src_path).stem + ".gen.c")

        # Collect headers needed for this .gen.c: each class's own
        # header + the (possibly cross-domain) parent + mixin headers
        # + the slot's home module's methods.gen.h for every op whose
        # slot lives in a foreign domain (cross-module override). The
        # last is needed so the foreign public-stub C symbol used as
        # yetty_yclass_method_id_t is declared.
        needed = set()
        for c in classes:
            needed.add(f"{c['domain']}/{c['name']}.h")
            p = c.get("parent")
            if p:
                needed.add(f"{p['domain']}/{p['name']}.h")
            for mx in c.get("mixins", []):
                needed.add(f"{mx['domain']}/{mx['name']}.h")
            for op in c.get("ops", []):
                sd = op.get("slot_domain", c["domain"])
                if sd != c["domain"]:
                    needed.add(f"{sd}/methods.gen.h")
        include_block = "".join(f'#include "{h}"\n' for h in sorted(needed))

        body = HEADER + include_block + "\n" \
             + "\n".join(emit_class_accessor(c) for c in classes)
        inc_path.write_text(body)


# ---------------- rpc.gen.{h,c} ------------------------------------------

def regular_classes(model: dict) -> list:
    return [c for c in model.get("classes", []) if c.get("type") == "regular"]


def class_header_for(cls: dict, module: str) -> str:
    return f"{module}/{Path(cls['source_file']).stem}.h"


def emit_skel(m: dict) -> str:
    """Unpack the wire body, resolve obj handle, re-enter the public stub
    with a local ctx so the right override fires on the actual class.
    The wire response carries a status byte (0=OK, 1=ERR); for value
    slots the OK payload is the raw value bytes that follow."""
    slot = qualified_slot(m)
    rid = result_type_id(m["return_type"])
    vt = ret_value_type(rid)
    args = wire_args(m)
    fields = args_struct_body(args, indent="        ")

    call_parts = ["&_local"]
    for a in args:
        if is_struct_ptr(a["type"]):
            call_parts.append(
                f"({a['type'].strip()})yetty_yclass_rpc_handle_resolve(_a.{wire_name(a)})")
        else:
            call_parts.append(f"_a.{a['name']}")
    call = ", ".join(call_parts)

    rt = f"struct {rid}_result"
    if vt is None:
        body = f"""\
    {rt} _r = {slot}({call});
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {{
        yetty_ycore_error_print(stderr, "[skel] {slot}", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }}
    ((uint8_t *)_resp)[0] = 0;
    return 1;
"""
    else:
        body = f"""\
    {rt} _r = {slot}({call});
    if (_resp_max < 1) return 0;
    if (YETTY_IS_ERR(_r)) {{
        yetty_ycore_error_print(stderr, "[skel] {slot}", _r.error);
        yetty_ycore_error_destroy(_r.error);
        ((uint8_t *)_resp)[0] = 1;
        return 1;
    }}
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
"""

    return f"""\
static size_t {slot}_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{{
    struct __attribute__((packed)) {{
{fields}\
    }} _a;
    if (_body_len < sizeof(_a)) return 0;
    memcpy(&_a, _body, sizeof(_a));
    struct yetty_yclass_ctx _local = {{0}};
{body}}}
"""


def emit_create_fn(cls: dict) -> str:
    """Per-class factory. ctx decides; caller is location-agnostic."""
    accessor = cls["accessor"]
    qname = qualified_class(cls)
    return f"""\
struct yetty_yclass_object_ptr_result {qname}_create(struct yetty_yclass_ctx *ctx)
{{
    ydebug("class={qname}");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result _kr = {accessor}();
    if (YETTY_IS_ERR(_kr))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "{qname}_create: class accessor failed", _kr);
    const struct yetty_yclass *_klass = _kr.value;

    if (!ctx || !ctx->session)
        return yetty_yclass_object_alloc(_klass);

    /* Prefetch the class's local-id ↔ remote-id mapping (idempotent). */
    yetty_yclass_rpc_session_translate_class(ctx->session, "{qname}");

    uint64_t _h = 0;
    const char *_name = "{qname}";
    if (yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, _name,
                              strlen(_name), &_h, sizeof(_h)) != sizeof(_h) || !_h)
        return YETTY_ERR(yetty_yclass_object_ptr, "{qname}_create: remote create failed");

    /* Proxy: object header + uint64_t handle. Same class accessor on
     * both sides — proxies never local-dispatch, so the class's
     * data_size contract isn't honoured for this allocation. */
    void *_mem = calloc(1, sizeof(struct yetty_yclass_object) + sizeof(uint64_t));
    if (!_mem)
        return YETTY_ERR(yetty_yclass_object_ptr, "{qname}_create: calloc(proxy) failed");
    struct yetty_yclass_object *_obj = _mem;
    *(const struct yetty_yclass **)_obj = _klass;
    *(uint64_t *)((char *)_obj + sizeof(*_obj)) = _h;
    return YETTY_OK(yetty_yclass_object_ptr, _obj);
}}
"""


def emit_lookup_tables(model: dict, module: str) -> str:
    classes = model.get("classes", [])
    methods = model.get("methods", [])

    # Lookup keys are the QUALIFIED class names — that's what the wire
    # carries on GET_CLASS/CREATE, and what yetty_yclass_by_name
    # resolves to. Each branch calls the class accessor, which now
    # returns yetty_yclass_ptr_result; we forward that result on a
    # name match.
    class_branches = "\n".join(
        f'    if (strcmp(name, "{qualified_class(c)}") == 0) return {c["accessor"]}();'
        for c in classes
    )

    accessor_section = f"""\
/* ---- {module}: class name → accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_{module}_accessor_lookup(const char *name)
{{
{class_branches}
    /* "Not mine": OK with NULL value — yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}}
"""

    # A module that owns zero slots (cross-domain-only — every override
    # targets a foreign-owned slot) has nothing to dispatch on the wire.
    # Skip the skel table and the rpc_add_skel_lookup install — the
    # foreign module's skel_lookup already covers those slots.
    if not methods:
        skel_install = ""
        skel_section = ""
    else:
        skel_rows = ",\n".join(
            f'    {{"{qualified_slot(m)}", {qualified_slot(m)}_skel}}'
            for m in methods
        )
        skel_section = f"""\

/* ---- {module}: slot → skel, name-keyed static data --------------- */

struct yetty_{module}_skel_row {{ const char *name; yetty_yclass_rpc_skel_fn fn; }};

static const struct yetty_{module}_skel_row yetty_{module}_skel_rows[] = {{
{skel_rows}
}};

static yetty_yclass_rpc_skel_fn yetty_{module}_skel_lookup(yetty_yclass_method_slot slot)
{{
    struct yetty_yclass_const_char_ptr_result nr = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(nr)) {{ yetty_ycore_error_destroy(nr.error); return NULL; }}
    const char *name = nr.value;
    for (size_t i = 0;
         i < sizeof(yetty_{module}_skel_rows) / sizeof(yetty_{module}_skel_rows[0]); ++i)
        if (strcmp(yetty_{module}_skel_rows[i].name, name) == 0)
            return yetty_{module}_skel_rows[i].fn;
    return NULL;
}}
"""
        skel_install = f"    yetty_yclass_rpc_add_skel_lookup(yetty_{module}_skel_lookup);\n"

    return f"""\
{accessor_section}\
{skel_section}\

/* ---- {module}: install hooks before main ------------------------- */

__attribute__((constructor))
static void yetty_{module}_install_hooks(void)
{{
    struct yetty_ycore_void_result _ar =
        yetty_yclass_add_accessor_lookup(yetty_{module}_accessor_lookup);
    if (YETTY_IS_ERR(_ar)) {{
        yetty_ycore_error_print(stderr, "yetty_{module}_install_hooks", _ar.error);
        yetty_ycore_error_destroy(_ar.error);
        abort();
    }}
{skel_install}\
}}
"""


def emit_rpc_h(model: dict, module: str, out_path: Path):
    guard = f"YETTY_{module.upper()}_RPC_GEN_H"
    decls = "\n".join(
        f"struct yetty_yclass_object_ptr_result "
        f"{qualified_class(c)}_create(struct yetty_yclass_ctx *ctx);"
        for c in regular_classes(model)
    )
    out_path.write_text(
        HEADER
        + f"#ifndef {guard}\n#define {guard}\n\n"
        + '#include <yclass/rpc.h>\n\n'
        + (decls + "\n\n" if decls else "")
        + "#endif\n"
    )


def emit_rpc_c(model: dict, module: str, out_path: Path):
    class_includes = "".join(
        f'#include "{class_header_for(c, module)}"\n'
        for c in model.get("classes", [])
    )
    parts = [HEADER,
             '#include <yclass/rpc.h>\n',
             '#include <yetty/ycore/result.h>\n',
             '#include <yetty/ytrace/ytrace.h>\n',
             f'#include "{module}/rpc.gen.h"\n',
             f'#include "{module}/methods.gen.h"\n',
             '#include <yclass/class.h>\n',
             class_includes,
             '#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n'
             '#include <string.h>\n\n']
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
    # headers seed with `#include <yclass/class.h>` so the runtime structs
    # (ctx, object, class) are visible during AST parsing.
    placeholder_class_h = '#include <yclass/class.h>\n'
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
    model = parse_sources([include_base, include_module, module_src], sources, module)
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
