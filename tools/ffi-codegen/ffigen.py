#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///
"""FFI binding generator — model.yaml → per-language bindings.

Reads each yclass module's generated `model.yaml` (the canonical, fully
self-describing API + type contract: methods, classes, data_fields, exposed,
and `types` — struct layouts, inlined Result unions, enum values) and emits
idiomatic language bindings. No libclang: model.yaml is the only input.

Layout (canonical, language-major; never under src/yetty):

    bindings/<lang>/yetty/runtime.<ext>          HAND-WRITTEN (not touched)
    bindings/<lang>/yetty/generated/_types.<ext> GENERATED — deduped types
    bindings/<lang>/yetty/generated/<module>.<ext> GENERATED — funcs + classes

The emitter only ever writes inside `generated/`; the hand-written runtime
(library load, Result decode, foundational handles) lives outside it.

Usage:
    tools/ffi-codegen/ffigen.py python [module ...]   # default: all modules
"""

from __future__ import annotations

import pathlib
import sys

import yaml

REPO = pathlib.Path(__file__).resolve().parents[2]
SRC = REPO / "src" / "yetty"


# ---------------------------------------------------------------------------
# Model loading
# ---------------------------------------------------------------------------

def discover_models(selected: list[str]) -> dict[str, dict]:
    """Load model.yaml for the requested modules (default: every module that
    has one)."""
    models: dict[str, dict] = {}
    names = selected or sorted(p.parent.name for p in SRC.glob("*/model.yaml"))
    for name in names:
        path = SRC / name / "model.yaml"
        if not path.exists():
            sys.stderr.write(f"ffigen: no model.yaml for module '{name}'\n")
            sys.exit(1)
        models[name] = yaml.safe_load(path.read_text()) or {}
    return models


def global_types(models: dict[str, dict]) -> list[dict]:
    """Dedup every module's `types` by name into one ordered list."""
    by_name: dict[str, dict] = {}
    for model in models.values():
        for entry in model.get("types", []) or []:
            by_name.setdefault(entry["name"], entry)
    return list(by_name.values())


# ---------------------------------------------------------------------------
# C type classification (shared across emitters)
# ---------------------------------------------------------------------------

def split_array(type_str: str) -> tuple[str, str]:
    """Split a C type into (element_type, array_suffix). 'char[64]' →
    ('char', '[64]'); 'struct X[4][2]' → ('struct X', '[4][2]'); 'int' →
    ('int', '')."""
    import re
    m = re.match(r"^(.*?)((?:\s*\[[0-9]+\])+)\s*$", (type_str or "").strip())
    if m:
        return m.group(1).strip(), m.group(2).replace(" ", "")
    return (type_str or "").strip(), ""


def classify(type_str: str) -> tuple[str, str | None]:
    """(category, tag): 'ptr' (opaque handle), 'struct'/'union'/'enum' (+tag),
    or 'scalar' (primitive/typedef). Array extents are stripped first."""
    import re
    t, _ = split_array(type_str or "")
    if "*" in t:
        return ("ptr", None)
    t = re.sub(r"^(const|volatile)\s+", "", t).strip()
    for kw in ("struct", "union", "enum"):
        m = re.match(rf"^{kw}\s+(\w+)$", t)
        if m:
            return (kw, m.group(1))
    return ("scalar", None)


def is_char_ptr(type_str: str) -> bool:
    t = (type_str or "").replace("const", "").replace("volatile", "").strip()
    return t in ("char *", "char*")


# ===========================================================================
# Python (ctypes) emitter
# ===========================================================================

PY_PRIM = {
    "void": "None", "int": "c_int", "unsigned int": "c_uint", "unsigned": "c_uint",
    "char": "c_char", "_Bool": "c_bool", "bool": "c_bool", "float": "c_float",
    "double": "c_double", "long": "c_long", "unsigned long": "c_ulong",
    "int8_t": "c_int8", "uint8_t": "c_uint8", "int16_t": "c_int16", "uint16_t": "c_uint16",
    "int32_t": "c_int32", "uint32_t": "c_uint32", "int64_t": "c_int64", "uint64_t": "c_uint64",
    "size_t": "c_size_t", "ssize_t": "c_ssize_t", "intptr_t": "c_ssize_t",
    "uintptr_t": "c_size_t",
}


def py_ctype(type_str: str, type_names: set[str], prefix: str = "") -> str:
    """Map a C type string to a ctypes expression. Pointers → opaque c_void_p
    (char* → c_char_p); by-value known struct → its class (optionally prefixed
    for references from a module file into _types); enum → c_int."""
    import re
    if is_char_ptr(type_str):
        return "c_char_p"
    category, tag = classify(type_str)
    if category == "ptr":
        return "c_void_p"
    if category in ("struct", "union"):
        return f"{prefix}{tag}" if tag in type_names else "c_void_p"
    if category == "enum":
        return "c_int"
    base = re.sub(r"^(const|volatile)\s+", "", (type_str or "").strip()).strip()
    return PY_PRIM.get(base, "c_void_p")


def py_field_ctype(type_str: str, type_names: set[str], prefix: str = "") -> str:
    """ctypes type for a struct field, wrapping array extents as (elem * N)."""
    import re
    base, arr = split_array(type_str)
    ctype = py_ctype(base, type_names, prefix)
    for dim in re.findall(r"\[(\d+)\]", arr):
        ctype = f"({ctype} * {dim})"
    return ctype


def py_type_deps(entry: dict, type_names: set[str]) -> set[str]:
    deps: set[str] = set()

    def scan(fields):
        for f in fields:
            if "fields" in f:
                scan(f["fields"])
            elif "type" in f:
                category, tag = classify(f["type"])
                if category in ("struct", "union") and tag in type_names and tag != entry["name"]:
                    deps.add(tag)

    if entry["kind"] in ("struct", "result"):
        scan(entry.get("fields", []))
    return deps


def py_topo(types: list[dict]) -> list[dict]:
    """Order types so every by-value dependency precedes its user."""
    names = {t["name"] for t in types}
    by_name = {t["name"]: t for t in types}
    ordered: list[dict] = []
    placed: set[str] = set()
    visiting: set[str] = set()

    def visit(name: str):
        if name in placed or name not in by_name:
            return
        visiting.add(name)
        for dep in sorted(py_type_deps(by_name[name], names)):
            if dep not in visiting:
                visit(dep)
        visiting.discard(name)
        if name not in placed:
            placed.add(name)
            ordered.append(by_name[name])

    for name in sorted(by_name):
        visit(name)
    return ordered


def py_emit_struct(entry: dict, type_names: set[str]) -> str:
    name = entry["name"]
    nested: list[str] = []
    specs: list[tuple[str, str]] = []
    anon: list[str] = []
    idx = 0
    for f in entry.get("fields", []):
        if "fields" in f:  # inlined anonymous union/struct
            idx += 1
            nname = f"{name}_u{idx}"
            base = "Union" if f.get("kind") == "union" else "Structure"
            nspecs = [(nf.get("name") or f"f{i}", py_field_ctype(nf.get("type", ""), type_names))
                      for i, nf in enumerate(f["fields"])]
            body = ", ".join(f'("{n}", {c})' for n, c in nspecs)
            nested.append(f"class {nname}({base}):\n    pass\n{nname}._fields_ = [{body}]")
            pyname = f.get("name") or f"_anon{idx}"
            specs.append((pyname, nname))
            if not f.get("name"):
                anon.append(pyname)
        else:
            specs.append((f.get("name") or "_pad", py_field_ctype(f.get("type", ""), type_names)))
    out = list(nested)
    out.append(f"class {name}(Structure):\n    pass")
    if anon:
        out.append(f"{name}._anonymous_ = ({', '.join(repr(a) for a in anon)},)")
    body = ", ".join(f'("{n}", {c})' for n, c in specs)
    out.append(f"{name}._fields_ = [{body}]")
    return "\n".join(out)


def py_emit_enum(entry: dict) -> str:
    lines = [f"class {entry['name']}(IntEnum):"]
    vals = entry.get("values", [])
    if not vals:
        lines.append("    pass")
    for v in vals:
        lines.append(f"    {v['name']} = {v['value']}")
    return "\n".join(lines)


def py_emit_types(types: list[dict], out_path: pathlib.Path):
    type_names = {t["name"] for t in types}
    parts = [
        '"""Foundational + shared ABI types — GENERATED, do not edit."""',
        "from __future__ import annotations",
        "from ctypes import (Structure, Union, c_bool, c_char, c_char_p, c_double,",
        "    c_float, c_int, c_int8, c_int16, c_int32, c_int64, c_long, c_size_t,",
        "    c_ssize_t, c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong,",
        "    c_void_p)",
        "from enum import IntEnum",
        "",
    ]
    enums = [t for t in types if t["kind"] == "enum"]
    structs = py_topo([t for t in types if t["kind"] in ("struct", "result")])
    for e in enums:
        parts.append(py_emit_enum(e))
        parts.append("")
    for s in structs:
        parts.append(py_emit_struct(s, type_names))
        parts.append("")
    out_path.write_text("\n".join(parts) + "\n")


def py_method_pyname(slot: str) -> str:
    return slot


def py_emit_module(module: str, model: dict, type_names: set[str], out_path: pathlib.Path):
    methods = {m["slot"]: m for m in model.get("methods", [])}
    parts = [
        f'"""yetty.{module} bindings — GENERATED from model.yaml, do not edit."""',
        "from __future__ import annotations",
        "from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,",
        "    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,",
        "    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)",
        "from .. import runtime as _rt",
        "from . import _types as _t",
        "",
    ]

    def argtypes_expr(args: list[dict]) -> str:
        items = [py_ctype(a["type"], type_names, prefix="_t.") for a in args]
        return "[" + ", ".join(items) + "]"

    def call_args(args: list[dict]) -> list[str]:
        # args[0]=ctx -> None, args[1]=obj -> self._handle, rest -> params
        names = []
        for a in args[2:]:
            names.append(a["name"])
        return names

    def return_expr(return_type: str) -> str:
        cat, tag = classify(return_type)
        rname = (tag or "")
        if rname.endswith("void_result"):
            return "        _rt.check(res)\n        return None"
        # _ptr_result and value results both carry res.value
        return "        _rt.check(res)\n        return res.value"

    for cls in model.get("classes", []):
        if cls.get("type") != "regular":
            continue
        cname = "".join(w.capitalize() for w in cls["name"].split("_"))
        parts.append(f"class {cname}:")
        parts.append(f'    """yclass {cls["domain"]}:{cls["name"]}"""')
        # constructor → generated <module>_<class>_create
        create_sym = f"yetty_{cls['domain']}_{cls['name']}_create"
        parts.append("    def __init__(self, _handle=None):")
        parts.append("        if _handle is None:")
        parts.append(
            f'            _fn = _rt.cfn("{create_sym}", _t.yetty_yclass_object_ptr_result, [c_void_p])')
        parts.append("            res = _fn(None)")
        parts.append("            _rt.check(res)")
        parts.append("            _handle = res.value")
        parts.append("        self._handle = _handle")
        # methods (this class's ops whose slot is owned by this module)
        for op in cls.get("ops", []):
            slot = op["slot"]
            m = methods.get(slot)
            if not m:
                continue  # slot owned by another module; bound there
            args = m["args"]
            params = ", ".join(call_args(args))
            sig = f"self, {params}" if params else "self"
            sym = f"yetty_{m['domain']}_{slot}"
            parts.append(f"    def {py_method_pyname(slot)}({sig}):")
            parts.append(
                f'        _fn = _rt.cfn("{sym}", _t.{classify(m["return_type"])[1]}, {argtypes_expr(args)})')

            def pass_expr(arg):
                # str -> bytes for char* params; everything else passes through.
                if py_ctype(arg["type"], type_names) == "c_char_p":
                    return f"_rt.cstr({arg['name']})"
                return arg["name"]

            passed = ["None", "self._handle"] + [pass_expr(a) for a in args[2:]]
            parts.append(f"        res = _fn({', '.join(passed)})")
            parts.append(return_expr(m["return_type"]))
        parts.append("")

    out_path.write_text("\n".join(parts) + "\n")


def emit_python(models: dict[str, dict]):
    root = REPO / "bindings" / "python" / "yetty"
    gen = root / "generated"
    gen.mkdir(parents=True, exist_ok=True)
    types = global_types(models)
    type_names = {t["name"] for t in types}
    py_emit_types(types, gen / "_types.py")
    module_names = []
    for module, model in models.items():
        if not model.get("classes"):
            continue  # nothing callable to expose
        py_emit_module(module, model, type_names, gen / f"{module}.py")
        module_names.append(module)
    # generated package __init__ re-exports the module classes lazily.
    init = ['"""GENERATED package — do not edit."""']
    for m in sorted(module_names):
        init.append(f"from . import {m} as {m}")
    (gen / "__init__.py").write_text("\n".join(init) + "\n")
    print(f"ffigen: python → {len(module_names)} modules, {len(types)} types "
          f"under bindings/python/yetty/generated/")


# ===========================================================================
# Lua (LuaJIT ffi) emitter — for neovim. cdef takes C declarations verbatim,
# so types/prototypes are reproduced straight from the model's C strings.
# ===========================================================================

def lua_class_name(name: str) -> str:
    return "".join(w.capitalize() for w in name.split("_"))


def lua_c_field(field: dict, indent: str = "  ") -> str:
    if "fields" in field:  # inlined anonymous union/struct
        kind = field.get("kind", "struct")
        inner = "\n".join(lua_c_field(sf, indent + "  ") for sf in field["fields"])
        member = field.get("name") or ""
        tail = f" {member}" if member else ""
        return f"{indent}{kind} {{\n{inner}\n{indent}}}{tail};"
    base, arr = split_array(field["type"])
    # Enums aren't cdef'd (their constants live in the Lua table); a field of
    # `enum X` would have unknown size, so emit it as int (enum's storage type).
    if classify(base)[0] == "enum":
        base = "int"
    n = field["name"] or "_pad"
    sep = "" if base.endswith("*") else " "
    return f"{indent}{base}{sep}{n}{arr};"


def lua_opaque_forward_decls(models: dict, types: list) -> list:
    """`struct X;` forward declarations for pointer-target tags that are never
    defined as a `types` entry (ctx, object, drawable_list, …). LuaJIT tolerates
    incomplete pointers, but declaring them keeps the cdef unambiguous."""
    import re
    type_names = {t["name"] for t in types}
    tags: set[str] = set()

    def scan_type(type_str: str):
        m = re.search(r"struct\s+(\w+)\s*\*", type_str or "")
        if m and m.group(1) not in type_names:
            tags.add(m.group(1))

    for t in types:
        def walk(fields):
            for f in fields:
                if "fields" in f:
                    walk(f["fields"])
                elif "type" in f:
                    scan_type(f["type"])
        if t["kind"] in ("struct", "result"):
            walk(t["fields"])
    for model in models.values():
        for m in model.get("methods", []):
            scan_type(m.get("return_type", ""))
            for a in m.get("args", []):
                scan_type(a.get("type", ""))
    return [f"struct {tag};" for tag in sorted(tags)]


# Scalar types LuaJIT's cdef predefines. Anything else seen in a field (e.g.
# glibc-internal __time_t / __syscall_slong_t from struct timespec) gets a
# `typedef long …;` shim — those are all `long` on LP64, so the struct layout
# stays correct.
LUA_KNOWN_SCALARS = {
    "int", "unsigned int", "unsigned", "char", "signed char", "unsigned char",
    "short", "unsigned short", "long", "unsigned long", "long long",
    "unsigned long long", "float", "double", "bool", "_Bool", "void",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t", "wchar_t",
}


def lua_unknown_scalars(types: list) -> list:
    import re
    found: set[str] = set()

    def walk(fields):
        for f in fields:
            if "fields" in f:
                walk(f["fields"])
            elif "type" in f and classify(f["type"])[0] == "scalar":
                elem, _ = split_array(f["type"])
                base = re.sub(r"^(const|volatile)\s+", "", elem).strip()
                if base and base not in LUA_KNOWN_SCALARS:
                    found.add(base)

    for t in types:
        if t["kind"] in ("struct", "result"):
            walk(t["fields"])
    return sorted(found)


def lua_emit_types(models: dict, types: list, out_path: pathlib.Path):
    cdef = [f"typedef long {name};" for name in lua_unknown_scalars(types)]
    cdef += lua_opaque_forward_decls(models, types)
    for entry in py_topo([t for t in types if t["kind"] in ("struct", "result")]):
        cdef.append(f"struct {entry['name']} {{")
        for f in entry.get("fields", []):
            cdef.append(lua_c_field(f))
        cdef.append("};")
    parts = [
        "-- Foundational + shared ABI types — GENERATED, do not edit.",
        'local ffi = require("ffi")',
        "ffi.cdef[[",
        "\n".join(cdef),
        "]]",
        "local M = {}",
    ]
    for entry in (t for t in types if t["kind"] == "enum"):
        parts.append(f"M.{entry['name']} = {{")
        for v in entry.get("values", []):
            parts.append(f"  {v['name']} = {v['value']},")
        parts.append("}")
    parts.append("return M")
    out_path.write_text("\n".join(parts) + "\n")


def lua_cdef_param(type_str: str) -> str:
    """A function-parameter type for cdef: enum → int (enums aren't cdef'd)."""
    base, arr = split_array(type_str)
    if classify(base)[0] == "enum":
        base = "int"
    return f"{base}{arr}"


def lua_proto(return_type: str, name: str, args: list) -> str:
    params = ", ".join(lua_cdef_param(a["type"]) for a in args) if args else "void"
    sep = "" if return_type.endswith("*") else " "
    return f"{return_type}{sep}{name}({params});"


def lua_emit_module(module: str, model: dict, out_path: pathlib.Path):
    methods = {m["slot"]: m for m in model.get("methods", [])}
    protos: list[str] = []
    for cls in model.get("classes", []):
        if cls.get("type") == "regular":
            protos.append(lua_proto("struct yetty_yclass_object_ptr_result",
                                    f"yetty_{cls['domain']}_{cls['name']}_create",
                                    [{"type": "struct yetty_yclass_ctx *"}]))
    for m in model.get("methods", []):
        protos.append(lua_proto(m["return_type"], f"yetty_{m['domain']}_{m['slot']}", m["args"]))

    parts = [
        f"-- yetty.{module} bindings — GENERATED from model.yaml, do not edit.",
        'local ffi = require("ffi")',
        'local rt = require("yetty.runtime")',
        'require("yetty.generated._types")',
        "ffi.cdef[[",
        "\n".join(protos),
        "]]",
        "local M = {}",
    ]

    def returns_value(return_type: str) -> bool:
        _, tag = classify(return_type)
        return not (tag or "").endswith("void_result")

    for cls in model.get("classes", []):
        if cls.get("type") != "regular":
            continue
        cname = lua_class_name(cls["name"])
        create_sym = f"yetty_{cls['domain']}_{cls['name']}_create"
        parts.append(f"local {cname} = {{}}")
        parts.append(f"{cname}.__index = {cname}")
        parts.append(f"function {cname}.new()")
        parts.append("  local res = rt.C()." + create_sym + "(nil)")
        parts.append("  rt.check(res)")
        parts.append(f"  return setmetatable({{ handle = res.value }}, {cname})")
        parts.append("end")
        for op in cls.get("ops", []):
            m = methods.get(op["slot"])
            if not m:
                continue
            params = [a["name"] for a in m["args"][2:]]
            # `:` already binds self implicitly — do NOT list it in params.
            sig = ", ".join(params)
            sym = f"yetty_{m['domain']}_{op['slot']}"
            call_args = ", ".join(["nil", "self.handle"] + params)
            parts.append(f"function {cname}:{op['slot']}({sig})")
            parts.append(f"  local res = rt.C().{sym}({call_args})")
            parts.append("  rt.check(res)")
            if returns_value(m["return_type"]):
                parts.append("  return res.value")
            parts.append("end")
        parts.append(f"M.{cname} = {cname}")
    parts.append("return M")
    out_path.write_text("\n".join(parts) + "\n")


def emit_lua(models: dict[str, dict]):
    gen = REPO / "bindings" / "lua" / "yetty" / "generated"
    gen.mkdir(parents=True, exist_ok=True)
    types = global_types(models)
    lua_emit_types(models, types, gen / "_types.lua")
    count = 0
    for module, model in models.items():
        if not model.get("classes"):
            continue
        lua_emit_module(module, model, gen / f"{module}.lua")
        count += 1
    print(f"ffigen: lua → {count} modules, {len(types)} types "
          f"under bindings/lua/yetty/generated/")


EMITTERS = {"python": emit_python, "lua": emit_lua}


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in EMITTERS:
        sys.stderr.write(f"usage: ffigen.py <{'|'.join(EMITTERS)}> [module ...]\n")
        sys.exit(2)
    lang = sys.argv[1]
    models = discover_models(sys.argv[2:])
    EMITTERS[lang](models)


if __name__ == "__main__":
    main()
