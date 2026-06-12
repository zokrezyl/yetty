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
    tools/ffi-codegen/lua/ffigen.py [module ...]   # default: all modules
"""

from __future__ import annotations

import pathlib
import sys

import yaml

REPO = pathlib.Path(__file__).resolve().parents[3]
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




def type_deps(entry: dict, type_names: set[str]) -> set[str]:
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


def topo_types(types: list[dict]) -> list[dict]:
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
        for dep in sorted(type_deps(by_name[name], names)):
            if dep not in visiting:
                visit(dep)
        visiting.discard(name)
        if name not in placed:
            placed.add(name)
            ordered.append(by_name[name])

    for name in sorted(by_name):
        visit(name)
    return ordered

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
    for entry in topo_types([t for t in types if t["kind"] in ("struct", "result")]):
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



def main():
    models = discover_models(sys.argv[1:])
    emit_lua(models)


if __name__ == "__main__":
    main()
