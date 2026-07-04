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
    tools/ffi-codegen/python/ffigen.py [module ...]   # default: all modules
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


# ---------------------------------------------------------------------------
# Property accessors. A `property`-annotated member of type T gets a generated
# C getter `..._<field>_get(obj)` returning `struct <id>_result` and a setter
# `..._<field>_set(obj, T value)` returning a void Result. The result-type id
# below MUST match codegen's result_type_id() so the symbol names line up.
# ---------------------------------------------------------------------------

def prop_result_id(field_type: str) -> str:
    """The YRESULT_DECLARE id for a property's value type (mirrors codegen)."""
    import re
    r = (field_type or "").strip()
    if r in ("int", "int32_t"):
        return "yetty_ycore_int"
    if r == "size_t":
        return "yetty_ycore_size"
    if r == "uint32_t":
        return "uint32"
    m = re.match(r"^struct\s+(\w+)\s*\*\s*$", r)
    if m:
        return f"{m.group(1)}_ptr"
    m = re.match(r"^struct\s+(\w+)\s*$", r)
    if m:
        overrides = {
            "yetty_ycore_rectangle": "rectangle",
            "yetty_ycore_pixel_size": "pixel_size",
            "yetty_ycore_pixel_coord": "pixel_coord",
        }
        return overrides.get(m.group(1), m.group(1))
    return r


def prop_result_name(field_type: str) -> str:
    return f"{prop_result_id(field_type)}_result"


def synth_result_entry(result_name: str, value_type: str) -> dict:
    """A `kind: result` type entry matching YETTY_YRESULT_DECLARE's layout, so
    a property getter's by-value Result return has the right ABI in _types."""
    return {
        "name": result_name,
        "kind": "result",
        "fields": [
            {"name": "ok", "type": "int"},
            {"kind": "union", "fields": [
                {"name": "value", "type": value_type},
                {"name": "error", "type": "struct yetty_ycore_error"},
            ]},
        ],
    }


def add_property_result_types(models: dict[str, dict], types: list[dict]) -> None:
    """Ensure every property getter's result type exists in the shared type set;
    synthesize the ones codegen didn't already harvest into a module's `types`."""
    present = {t["name"] for t in types}
    for model in models.values():
        for cls in model.get("classes", []) or []:
            for field in cls.get("data_fields", []) or []:
                if not field.get("get"):
                    continue
                name = prop_result_name(field.get("type", ""))
                if name in present:
                    continue
                types.append(synth_result_entry(name, field.get("type", "")))
                present.add(name)


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


PY_ANNOT_PRIM = {
    "int": "int", "unsigned int": "int", "unsigned": "int", "char": "bytes",
    "_Bool": "bool", "bool": "bool", "float": "float", "double": "float",
    "long": "int", "unsigned long": "int", "int8_t": "int", "uint8_t": "int",
    "int16_t": "int", "uint16_t": "int", "int32_t": "int", "uint32_t": "int",
    "int64_t": "int", "uint64_t": "int", "size_t": "int", "ssize_t": "int",
    "intptr_t": "int", "uintptr_t": "int",
}


def py_arg_annot(type_str: str, type_names: set[str]) -> str:
    import re
    if is_char_ptr(type_str):
        return "str | bytes | None"
    category, tag = classify(type_str)
    if category == "ptr":
        return "Any"
    if category in ("struct", "union"):
        return f"_t.{tag}" if tag in type_names else "Any"
    if category == "enum":
        return "int"
    base = re.sub(r"^(const|volatile)\s+", "", (type_str or "").strip()).strip()
    return PY_ANNOT_PRIM.get(base, "Any")


def py_result_value_annot(return_type: str) -> str:
    _, tag = classify(return_type)
    name = tag or ""
    if name.endswith("void_result"):
        return "None"
    if name.endswith("char_ptr_result") or name.endswith("const_char_ptr_result"):
        return "str | None"
    if name.endswith("int_result") or name.endswith("size_result") or name.endswith("uint32_result") or name == "uint32_result":
        return "int"
    if name.endswith("float_result"):
        return "float"
    return "Any"


def py_result_converter(return_type: str) -> str | None:
    _, tag = classify(return_type)
    name = tag or ""
    if name.endswith("char_ptr_result") or name.endswith("const_char_ptr_result"):
        return "_rt.decode_cstr"
    return None


def py_is_result(return_type: str) -> bool:
    category, tag = classify(return_type)
    return category == "struct" and bool(tag and tag.endswith("_result"))


def py_return_ctype(return_type: str, type_names: set[str]) -> str:
    if return_type == "void":
        return "None"
    category, tag = classify(return_type)
    if category in ("struct", "union") and tag in type_names:
        return f"_t.{tag}"
    return py_ctype(return_type, type_names, prefix="_t.")


def py_plain_return_annot(return_type: str, type_names: set[str]) -> str:
    import re
    if return_type == "void":
        return "None"
    if is_char_ptr(return_type):
        return "bytes | None"
    category, tag = classify(return_type)
    if category == "ptr":
        return "Any"
    if category in ("struct", "union"):
        return f"_t.{tag}" if tag in type_names else "Any"
    if category == "enum":
        return "int"
    base = re.sub(r"^(const|volatile)\s+", "", (return_type or "").strip()).strip()
    return PY_ANNOT_PRIM.get(base, "Any")


def py_exposed_name(module: str, symbol: str) -> str:
    prefix = f"yetty_{module}_"
    return symbol[len(prefix):] if symbol.startswith(prefix) else symbol


def py_arg_name(arg: dict, index: int = 0) -> str:
    name = arg.get("name")
    if isinstance(name, str) and name.isidentifier() and name not in {"class", "def", "from", "pass"}:
        return name
    return f"arg{index}"


def py_class_name(name: str) -> str:
    return "".join(w.capitalize() for w in name.split("_"))


def py_regular_classes_in_base_order(model: dict) -> list[dict]:
    """Order regular classes so same-module Python bases are defined before
    subclasses. Cross-module bases are imported and do not constrain this file."""
    regular = [c for c in model.get("classes", []) if c.get("type") == "regular"]
    by_name = {c["name"]: c for c in regular}
    ordered: list[dict] = []
    placed: set[str] = set()
    visiting: set[str] = set()

    def visit(cls: dict):
        name = cls["name"]
        if name in placed:
            return
        if name in visiting:
            raise RuntimeError(f"cycle in Python yclass hierarchy near {name}")
        visiting.add(name)
        parent = cls.get("parent")
        if parent and parent.get("domain") == cls.get("domain"):
            pcls = by_name.get(parent.get("name"))
            if pcls:
                visit(pcls)
        visiting.remove(name)
        placed.add(name)
        ordered.append(cls)

    for cls in regular:
        visit(cls)
    return ordered


def py_emit_module(module: str, model: dict, type_names: set[str], out_path: pathlib.Path):
    methods = {m["slot"]: m for m in model.get("methods", [])}
    classes = py_regular_classes_in_base_order(model)
    imports = sorted({
        c["parent"]["domain"]
        for c in classes
        if c.get("parent") and c["parent"].get("domain") != module
    })
    parts = [
        f'"""yetty.{module} bindings — GENERATED from model.yaml, do not edit."""',
        "from __future__ import annotations",
        "from ctypes import (c_bool, c_char, c_char_p, c_double, c_float, c_int,",
        "    c_int8, c_int16, c_int32, c_int64, c_long, c_size_t, c_ssize_t,",
        "    c_uint, c_uint8, c_uint16, c_uint32, c_uint64, c_ulong, c_void_p)",
        "from typing import Any, ClassVar",
        "from .. import runtime as _rt",
        "from . import _types as _t",
    ]
    for parent_module in imports:
        parts.append(f"from . import {parent_module} as _{parent_module}")
    parts.append("")

    def argtypes_expr(args: list[dict]) -> str:
        items = [py_ctype(a["type"], type_names, prefix="_t.") for a in args]
        return "[" + ", ".join(items) + "]"

    def call_params(args: list[dict]) -> list[str]:
        # args[0]=ctx -> None, args[1]=obj -> self._handle, rest -> params
        return [f"{py_arg_name(a, i + 2)}: {py_arg_annot(a['type'], type_names)}" for i, a in enumerate(args[2:])]

    def return_expr(return_type: str) -> str:
        converter = py_result_converter(return_type)
        if converter:
            return f"        return _rt.result_from_c(res, {converter})"
        return "        return _rt.result_from_c(res)"

    def base_expr(cls: dict) -> str:
        parent = cls.get("parent")
        if not parent:
            return "_rt.YClass"
        pname = py_class_name(parent["name"])
        if parent.get("domain") == module:
            return pname
        return f"_{parent['domain']}.{pname}"

    def class_introduces_slot(cls: dict, method: dict) -> bool:
        return (method.get("domain") == cls.get("domain") and
                method.get("owning_class") == cls.get("name"))

    for cls in classes:
        cname = py_class_name(cls["name"])
        parts.append(f"class {cname}({base_expr(cls)}):")
        parts.append(f'    """yclass {cls["domain"]}:{cls["name"]}"""')
        parts.append(f'    __yclass_domain__: ClassVar[str] = {cls["domain"]!r}')
        parts.append(f'    __yclass_name__: ClassVar[str] = {cls["name"]!r}')
        accessor_sym = cls.get("accessor")
        if accessor_sym:
            parts.append("    @classmethod")
            parts.append("    def yclass(cls) -> _rt.Result[Any]:")
            parts.append(f'        _fn = _rt.cfn("{accessor_sym}", _t.yetty_yclass_ptr_result, [])')
            parts.append("        return _rt.result_from_c(_fn())")
        # constructor -> generated <module>_<class>_create
        create_sym = f"yetty_{cls['domain']}_{cls['name']}_create"
        parts.append("    def __init__(self, _handle: Any = None) -> None:")
        parts.append("        if _handle is None:")
        parts.append(
            f'            _fn = _rt.cfn("{create_sym}", _t.yetty_yclass_object_ptr_result, [c_void_p])')
        parts.append("            res = _rt.result_from_c(_fn(None))")
        parts.append("            if not res:")
        parts.append("                _rt.YClass.__init__(self, None, res.error)")
        parts.append("                return")
        parts.append("            _handle = res.value")
        parts.append("        super().__init__(_handle)")
        parts.append("    @classmethod")
        parts.append(f"    def create(cls) -> _rt.Result[{cname!r}]:")
        parts.append("        obj = cls()")
        parts.append("        return obj.init_result")
        # methods introduced by this class. Inherited slots remain inherited
        # Python methods; the C public stub still dispatches dynamically through
        # the yclass vtable using this instance's handle.
        emitted_slots: set[str] = set()
        for op in cls.get("ops", []):
            slot = op["slot"]
            if slot in emitted_slots:
                continue
            m = methods.get(slot)
            if not m or not class_introduces_slot(cls, m):
                continue
            emitted_slots.add(slot)
            args = m["args"]
            params = ", ".join(call_params(args))
            ret_ann = py_result_value_annot(m["return_type"])
            sig = f"self, {params}" if params else "self"
            sym = f"yetty_{m['domain']}_{slot}"
            parts.append(f"    def {py_method_pyname(slot)}({sig}) -> _rt.Result[{ret_ann}]:")
            parts.append(f'        """Call `{sym}`; returns Result, never raises for yclass errors."""')
            parts.append("        if self._handle is None:")
            parts.append("            return self._invalid_result()")
            parts.append(
                f'        _fn = _rt.cfn("{sym}", _t.{classify(m["return_type"])[1]}, {argtypes_expr(args)})')

            def pass_expr(arg):
                # str -> bytes for char* params; everything else passes through.
                if py_ctype(arg["type"], type_names) == "c_char_p":
                    return f"_rt.cstr({py_arg_name(arg)})"
                return py_arg_name(arg)

            passed = ["None", "self._handle"] + [pass_expr(a) for a in args[2:]]
            parts.append(f"        res = _fn({', '.join(passed)})")
            parts.append(return_expr(m["return_type"]))
        # property members → idiomatic @property getters/setters. Unlike
        # methods (which return a Result), a property RAISES _rt.YettyError on
        # failure so it reads as a plain attribute: `obj.rect`, `obj.rect = v`.
        # The C accessors take the object only (getter) or object + value
        # (setter); there is no ctx arg.
        for field in cls.get("data_fields", []) or []:
            fname = field.get("name")
            ftype = field.get("type", "")
            if not fname or not (field.get("get") or field.get("set")):
                continue
            base_sym = f"yetty_{cls['domain']}_{cls['name']}_{fname}"
            has_get = bool(field.get("get"))
            has_set = bool(field.get("set"))
            vann = py_arg_annot(ftype, type_names)
            vct = py_ctype(ftype, type_names, prefix="_t.")
            plain_ct = py_ctype(ftype, type_names)
            if plain_ct == "c_char_p":
                passv = "_rt.cstr(value)"
            elif plain_ct == "c_void_p":
                passv = "_rt.handle(value)"
            else:
                passv = "value"

            def emit_getter():
                rname = prop_result_name(ftype)
                rstr = f"struct {rname}"
                ret_ann = py_result_value_annot(rstr)
                conv = py_result_converter(rstr)
                parts.append("    @property")
                parts.append(f"    def {fname}(self) -> {ret_ann}:")
                parts.append(f'        """Property `{fname}` (raises YettyError on failure)."""')
                parts.append("        if self._handle is None:")
                parts.append('            raise _rt.YettyError("uninitialized yclass handle")')
                parts.append(f'        _fn = _rt.cfn("{base_sym}_get", _t.{rname}, [c_void_p])')
                arg = f"_fn(self._handle), {conv}" if conv else "_fn(self._handle)"
                parts.append(f"        res = _rt.result_from_c({arg})")
                parts.append("        if res.error is not None:")
                parts.append("            raise _rt.YettyError(res.error.message)")
                parts.append("        return res.value")

            def emit_setter(decorator):
                if decorator:
                    parts.append(f"    {decorator}")
                parts.append(f"    def {fname}(self, value: {vann}) -> None:")
                parts.append(f'        """Property `{fname}` (raises YettyError on failure)."""')
                parts.append("        if self._handle is None:")
                parts.append('            raise _rt.YettyError("uninitialized yclass handle")')
                parts.append(
                    f'        _fn = _rt.cfn("{base_sym}_set", _t.yetty_ycore_void_result, [c_void_p, {vct}])')
                parts.append(f"        res = _rt.result_from_c(_fn(self._handle, {passv}))")
                parts.append("        if res.error is not None:")
                parts.append("            raise _rt.YettyError(res.error.message)")

            if has_get:
                emit_getter()
                if has_set:
                    emit_setter(f"@{fname}.setter")
            else:
                # write-only: a plain setter promoted to a setter-only property.
                emit_setter(None)
                parts.append(f"    {fname} = property(None, {fname})")
        parts.append("")

    for fn in model.get("exposed", []) or []:
        if not isinstance(fn, dict) or "name" not in fn or "return_type" not in fn:
            continue
        symbol = fn["name"]
        pyname = py_exposed_name(module, symbol)
        args = fn.get("args", []) or []
        params = ", ".join(f"{py_arg_name(a, i)}: {py_arg_annot(a['type'], type_names)}" for i, a in enumerate(args))
        return_type = fn.get("return_type", "void")
        if py_is_result(return_type):
            ret_ann = f"_rt.Result[{py_result_value_annot(return_type)}]"
        else:
            ret_ann = py_plain_return_annot(return_type, type_names)
        parts.append(f"def {pyname}({params}) -> {ret_ann}:")
        parts.append(f'    """Call `{symbol}`."""')
        restype = py_return_ctype(return_type, type_names)
        parts.append(f'    _fn = _rt.cfn("{symbol}", {restype}, {argtypes_expr(args)})')

        def exposed_pass_expr(arg):
            ctype = py_ctype(arg["type"], type_names)
            if ctype == "c_char_p":
                return f"_rt.cstr({py_arg_name(arg)})"
            if ctype == "c_void_p":
                return f"_rt.handle({py_arg_name(arg)})"
            return py_arg_name(arg)

        passed = ", ".join(exposed_pass_expr(a) for a in args)
        call = f"_fn({passed})" if passed else "_fn()"
        if py_is_result(return_type):
            parts.append(f"    res = {call}")
            converter = py_result_converter(return_type)
            if converter:
                parts.append(f"    return _rt.result_from_c(res, {converter})")
            else:
                parts.append("    return _rt.result_from_c(res)")
        elif return_type == "void":
            parts.append(f"    {call}")
            parts.append("    return None")
        else:
            parts.append(f"    return {call}")
        parts.append("")

    out_path.write_text("\n".join(parts) + "\n")

# ---------------------------------------------------------------------------
# Connection facade (#439). The reactor-seam contract every binding rides —
# fd()/pump()/channels over yetty_ywire_connection — is a fixed plain-C surface
# below the yclass model, identical for every consumer, so it is emitted as a
# template rather than derived per-module. It lands in generated/ next to the
# model-driven files: same lifecycle (re-emitted on every run, never edited).
# ---------------------------------------------------------------------------

PY_CONNECTION_TEMPLATE = r'''"""GENERATED reactor-seam facade — do not edit.

The multiplexed wire connection (yetty_ywire_connection over
yetty_yclass_transport_pty): the sole owner of the terminal byte stream,
demuxing rpc / input / raw / dynamic channels. All the dangerous byte handling
— framing, raw mode, flow control, chunking — lives in C; this facade only
registers the connection fd with a loop and calls pump() on readiness. No
frame parsing on the Python side.

Two drivers, per the endpoint contract:
  - sync:   Connection.run_forever(...) — a selectors loop for loop-less apps.
  - async:  await Connection.run_async(...) — add_reader/add_writer on the
            running asyncio loop; the host loop owns the fd.
"""

from __future__ import annotations

import ctypes as C
import sys
from typing import Callable

from .. import runtime as _rt
from . import _types as _t

# Well-known channel ids + protocol constants (include/yetty/ywire/channel.h).
CHANNEL_RPC = 1
CHANNEL_INPUT = 2
CHANNEL_RAW = 3
CHANNEL_DYNAMIC_BASE = 16
WINDOW_DEFAULT = 256 * 1024
CHUNK_MAX = 16 * 1024

# enum yetty_ywire_channel_event
EVENT_REMOTE_EOF = 1
EVENT_CLOSED = 2

ENVELOPE_SINK = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_void_p, C.c_size_t,
                            C.c_void_p, C.c_size_t)
RAW_SINK = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_size_t)
RESIZE_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_int, C.c_int, C.c_int, C.c_int)
EVENT_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_void_p, C.c_int)
ACCEPT_CB = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)


class _PtrResultUnion(C.Union):
    _fields_ = [("value", C.c_void_p), ("error", _t.yetty_ycore_error)]


class _PtrResult(C.Structure):
    _anonymous_ = ("_anon",)
    _fields_ = [("ok", C.c_int), ("_anon", _PtrResultUnion)]


class _Reactor(C.Structure):
    """struct yetty_yclass_transport_reactor — {userdata, ops}, by value."""

    _fields_ = [("userdata", C.c_void_p), ("ops", C.c_void_p)]


def _cresult(name, restype, argtypes, *args, convert=None):
    return _rt.result_from_c(_rt.cfn(name, restype, list(argtypes))(*args), convert)


def _cvoid(name, argtypes, *args):
    return _cresult(name, _t.yetty_ycore_void_result, argtypes, *args)


def _raw(name, restype, argtypes, *args):
    return _rt.cfn(name, restype, list(argtypes))(*args)


class Channel:
    """One logical lane of a Connection (SSH's "channel"): an independent,
    ordered byte sub-stream. Sinks fire during the connection's pump."""

    def __init__(self, connection: "Connection", pointer):
        self._connection = connection
        self._pointer = pointer
        self._env_trampoline = None
        self._raw_trampoline = None
        self._event_trampoline = None

    @property
    def pointer(self):
        return self._pointer

    def id(self) -> int:
        return _raw("yetty_ywire_channel_id", C.c_uint, [C.c_void_p], self._pointer)

    def write(self, data: bytes) -> _rt.Result:
        buf = C.create_string_buffer(data, len(data))
        return _cresult("yetty_ywire_channel_write", _t.yetty_ycore_size_result,
                        (C.c_void_p, C.c_void_p, C.c_size_t), self._pointer, buf, len(data))

    def flush(self) -> _rt.Result:
        return _cvoid("yetty_ywire_channel_flush", (C.c_void_p,), self._pointer)

    def read(self, max_bytes: int = 65536) -> _rt.Result:
        buf = C.create_string_buffer(max_bytes)
        res = _cresult("yetty_ywire_channel_read", _t.yetty_ycore_size_result,
                       (C.c_void_p, C.c_void_p, C.c_size_t), self._pointer, buf, max_bytes)
        if res.error is not None:
            return res
        return _rt.Result(value=buf.raw[: res.value or 0])

    def recv_blocking(self, max_bytes: int = 65536) -> _rt.Result:
        buf = C.create_string_buffer(max_bytes)
        res = _cresult("yetty_ywire_channel_recv_blocking", _t.yetty_ycore_size_result,
                       (C.c_void_p, C.c_void_p, C.c_size_t), self._pointer, buf, max_bytes)
        if res.error is not None:
            return res
        return _rt.Result(value=buf.raw[: res.value or 0])

    def transport(self) -> _rt.Result:
        """A heap yetty_yclass_transport riding this channel — hand it to an
        RPC session / framework attach, which takes ownership."""
        return _cresult("yetty_ywire_channel_transport", _PtrResult, (C.c_void_p,),
                        self._pointer)

    def set_envelope_sink(self, sink: Callable[[int, bytes, bytes], None]) -> _rt.Result:
        def trampoline(_user, wire_code, args, args_len, payload, payload_len):
            args_bytes = C.string_at(args, args_len) if args and args_len else b""
            payload_bytes = C.string_at(payload, payload_len) if payload and payload_len else b""
            sink(wire_code, args_bytes, payload_bytes)

        self._env_trampoline = ENVELOPE_SINK(trampoline)
        return _cvoid("yetty_ywire_channel_set_envelope_sink",
                      (C.c_void_p, ENVELOPE_SINK, C.c_void_p),
                      self._pointer, self._env_trampoline, None)

    def set_raw_sink(self, sink: Callable[[bytes], None]) -> _rt.Result:
        def trampoline(_user, data, n):
            sink(C.string_at(data, n) if data and n else b"")

        self._raw_trampoline = RAW_SINK(trampoline)
        return _cvoid("yetty_ywire_channel_set_raw_sink", (C.c_void_p, RAW_SINK, C.c_void_p),
                      self._pointer, self._raw_trampoline, None)

    def set_event_cb(self, callback: Callable[["Channel", int], None]) -> _rt.Result:
        def trampoline(_user, _channel_ptr, event):
            callback(self, event)

        self._event_trampoline = EVENT_CB(trampoline)
        return _cvoid("yetty_ywire_channel_set_event_cb", (C.c_void_p, EVENT_CB, C.c_void_p),
                      self._pointer, self._event_trampoline, None)

    def send_eof(self) -> _rt.Result:
        return _cvoid("yetty_ywire_channel_send_eof", (C.c_void_p,), self._pointer)

    def close(self) -> _rt.Result:
        return _cvoid("yetty_ywire_channel_close", (C.c_void_p,), self._pointer)

    def send_window(self) -> int:
        return _raw("yetty_ywire_channel_send_window", C.c_int64, [C.c_void_p], self._pointer)

    def remote_eof(self) -> bool:
        return bool(_raw("yetty_ywire_channel_remote_eof", C.c_int, [C.c_void_p], self._pointer))

    def pending_out(self) -> int:
        return _raw("yetty_ywire_channel_pending_out", C.c_size_t, [C.c_void_p], self._pointer)


class Connection:
    """Sole owner of the terminal byte stream and the channel mux over it.

    Owns a yetty_yclass_transport_pty (raw mode, ordered non-blocking writer)
    — or the side-channel fd pair when YETTY_YWIRE_SIDE_CHANNEL is set — and a
    yetty_ywire_connection on top. Register fd() with any loop and call
    pump_readable()/pump_writable() on readiness, or use the bundled
    run_forever()/run_async() drivers.
    """

    def __init__(self, in_fd: int | None = None, out_fd: int | None = None, *,
                 compressed: bool = True, raw_mode: bool = True,
                 side_channel_env: bool = True):
        self._transport = None
        self._pointer = None
        self._channels: dict[int, Channel] = {}
        self._resize_trampoline = None
        self._accept_trampoline = None
        self._closed = False

        in_fd = in_fd if in_fd is not None else sys.stdin.fileno()
        out_fd = out_fd if out_fd is not None else sys.stdout.fileno()
        creator = ("yetty_yclass_transport_pty_create_from_env" if side_channel_env
                   else "yetty_yclass_transport_pty_create")
        try:
            transport_res = _cresult(creator, _PtrResult, (C.c_int, C.c_int), in_fd, out_fd)
            if transport_res.error is not None:
                raise _rt.YettyError(transport_res.error.message)
            self._transport = transport_res.value
            if raw_mode:
                raw_res = _cvoid("yetty_yclass_transport_pty_enable_raw_mode", (C.c_void_p,),
                                 self._transport)
                if raw_res.error is not None:
                    raise _rt.YettyError(raw_res.error.message)
            reactor = _raw("yetty_yclass_transport_pty_reactor", _Reactor, [C.c_void_p],
                           self._transport)
            conn_res = _cresult("yetty_ywire_connection_create", _PtrResult,
                                (_Reactor, C.c_int), reactor, 1 if compressed else 0)
            if conn_res.error is not None:
                raise _rt.YettyError(conn_res.error.message)
            self._pointer = conn_res.value
        except BaseException:
            # Raw mode may already be applied — restore by tearing down.
            self.close()
            raise

    # --- channels ------------------------------------------------------------

    def channel(self, channel_id: int) -> Channel | None:
        pointer = _raw("yetty_ywire_connection_channel", C.c_void_p, [C.c_void_p, C.c_uint],
                       self._pointer, channel_id)
        if not pointer:
            self._channels.pop(channel_id, None)
            return None
        cached = self._channels.get(channel_id)
        if cached is None or cached.pointer != pointer:
            cached = Channel(self, pointer)
            self._channels[channel_id] = cached
        return cached

    @property
    def rpc(self) -> Channel:
        return self.channel(CHANNEL_RPC)

    @property
    def input(self) -> Channel:
        return self.channel(CHANNEL_INPUT)

    @property
    def raw(self) -> Channel:
        return self.channel(CHANNEL_RAW)

    def open_channel(self, initial_recv_window: int = 0) -> _rt.Result:
        res = _cresult("yetty_ywire_connection_open_channel", _PtrResult,
                       (C.c_void_p, C.c_uint), self._pointer, initial_recv_window)
        if res.error is not None:
            return res
        channel = Channel(self, res.value)
        self._channels[channel.id()] = channel
        return _rt.Result(value=channel)

    def set_role(self, acceptor: bool) -> _rt.Result:
        return _cvoid("yetty_ywire_connection_set_role", (C.c_void_p, C.c_int),
                      self._pointer, 1 if acceptor else 0)

    def set_accept_cb(self, callback: Callable[[Channel], bool]) -> _rt.Result:
        def trampoline(_user, channel_ptr):
            channel = Channel(self, channel_ptr)
            accepted = bool(callback(channel))
            if accepted:
                self._channels[channel.id()] = channel
            return 1 if accepted else 0

        self._accept_trampoline = ACCEPT_CB(trampoline)
        return _cvoid("yetty_ywire_connection_set_accept_cb",
                      (C.c_void_p, ACCEPT_CB, C.c_void_p),
                      self._pointer, self._accept_trampoline, None)

    # --- reactor seam ----------------------------------------------------------

    def fd(self) -> int:
        return _raw("yetty_ywire_connection_fd", C.c_int, [C.c_void_p], self._pointer)

    def out_fd(self) -> int:
        return _raw("yetty_ywire_connection_out_fd", C.c_int, [C.c_void_p], self._pointer)

    def want_write(self) -> bool:
        return bool(_raw("yetty_ywire_connection_want_write", C.c_int, [C.c_void_p],
                         self._pointer))

    def is_eof(self) -> bool:
        return bool(_raw("yetty_ywire_connection_is_eof", C.c_int, [C.c_void_p], self._pointer))

    def pump_readable(self) -> _rt.Result:
        return _cresult("yetty_ywire_connection_pump_readable", _t.yetty_ycore_size_result,
                        (C.c_void_p,), self._pointer)

    def pump_writable(self) -> _rt.Result:
        return _cresult("yetty_ywire_connection_pump_writable", _t.yetty_ycore_size_result,
                        (C.c_void_p,), self._pointer)

    def set_resize_cb(self, callback: Callable[[int, int, int, int], None]) -> _rt.Result:
        def trampoline(_user, width_px, height_px, cols, rows):
            callback(width_px, height_px, cols, rows)

        self._resize_trampoline = RESIZE_CB(trampoline)
        return _cvoid("yetty_ywire_connection_set_resize_cb",
                      (C.c_void_p, RESIZE_CB, C.c_void_p),
                      self._pointer, self._resize_trampoline, None)

    def pickup_winsize(self) -> _rt.Result:
        return _cvoid("yetty_ywire_connection_pickup_winsize", (C.c_void_p,), self._pointer)

    # --- drivers ---------------------------------------------------------------

    def run_forever(self, on_tick: Callable[[], None] | None = None,
                    should_stop: Callable[[], bool] | None = None,
                    tick: float = 0.033) -> int:
        """Sync facade for loop-less callers: a selectors loop over fd() plus a
        periodic tick (ship dirty frames there)."""
        import selectors

        selector = selectors.DefaultSelector()
        selector.register(self.fd(), selectors.EVENT_READ)
        try:
            while True:
                for _key, _mask in selector.select(timeout=tick):
                    self.pump_readable()
                if on_tick is not None:
                    on_tick()
                self.pump_writable()
                if self.is_eof() or (should_stop is not None and should_stop()):
                    return 0
        except KeyboardInterrupt:
            return 0
        finally:
            selector.close()

    async def run_async(self, on_tick: Callable[[], None] | None = None,
                        should_stop: Callable[[], bool] | None = None,
                        tick: float = 0.033) -> int:
        """Async facade: the running asyncio loop owns fd(); pump on readiness,
        tick periodically. The C connection owns the bytes."""
        import asyncio

        loop = asyncio.get_running_loop()
        fd = self.fd()
        done: asyncio.Future = loop.create_future()

        def finish() -> None:
            if not done.done():
                done.set_result(0)

        def on_readable() -> None:
            self.pump_readable()
            if on_tick is not None:
                on_tick()
            self.pump_writable()
            if self.is_eof() or (should_stop is not None and should_stop()):
                finish()

        async def ticker() -> None:
            try:
                while not done.done():
                    if on_tick is not None:
                        on_tick()
                    self.pump_writable()
                    if self.is_eof() or (should_stop is not None and should_stop()):
                        finish()
                        break
                    await asyncio.sleep(tick)
            except asyncio.CancelledError:
                pass

        loop.add_reader(fd, on_readable)
        ticker_task = loop.create_task(ticker())
        try:
            await done
        finally:
            loop.remove_reader(fd)
            ticker_task.cancel()
        return 0

    # --- lifecycle ---------------------------------------------------------------

    def close(self) -> None:
        """Destroy connection then transport (restores raw mode). Idempotent."""
        if self._closed:
            return
        self._closed = True
        if self._pointer:
            _cvoid("yetty_ywire_connection_destroy", (C.c_void_p,), self._pointer)
            self._pointer = None
        if self._transport:
            _cvoid("yetty_yclass_transport_pty_flush_blocking", (C.c_void_p,), self._transport)
            _cvoid("yetty_yclass_transport_pty_destroy", (C.c_void_p,), self._transport)
            self._transport = None
        self._channels.clear()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False
'''


def py_emit_connection(out_path: pathlib.Path):
    out_path.write_text(PY_CONNECTION_TEMPLATE)


def emit_python(models: dict[str, dict]):
    root = REPO / "bindings" / "python" / "yetty"
    gen = root / "generated"
    gen.mkdir(parents=True, exist_ok=True)
    types = global_types(models)
    add_property_result_types(models, types)
    type_names = {t["name"] for t in types}
    py_emit_types(types, gen / "_types.py")
    py_emit_connection(gen / "connection.py")
    module_names = []
    for module, model in models.items():
        if not model.get("classes"):
            continue  # nothing callable to expose
        py_emit_module(module, model, type_names, gen / f"{module}.py")
        module_names.append(module)
    # generated package __init__ re-exports the module classes lazily.
    init = ['"""GENERATED package — do not edit."""']
    init.append("from . import connection as connection")
    for m in sorted(module_names):
        init.append(f"from . import {m} as {m}")
    (gen / "__init__.py").write_text("\n".join(init) + "\n")
    print(f"ffigen: python → {len(module_names)} modules + connection facade, "
          f"{len(types)} types under bindings/python/yetty/generated/")




def main():
    models = discover_models(sys.argv[1:])
    emit_python(models)


if __name__ == "__main__":
    main()
