#!/usr/bin/env python3
"""Stage-2 emitter — Python ctypes bindings.

Reads build/ffi/<module>.yaml (produced by tools/ffi-codegen/parse/parse.py)
and emits a single flat module at bindings/python/<module>.py. No class
inference: every C function is a free function, every struct a
ctypes.Structure (with fields where known, empty for opaque), every enum an
IntEnum.

Run:
    tools/ffi-codegen/.venv/bin/python tools/ffi-codegen/emit/python/generate.py <module>
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import yaml


REPO = pathlib.Path(__file__).resolve().parents[4]


# Builtin spelling → ctypes name. Spellings come from clang's canonical-type
# rendering on Linux x86-64; they're stable for the platforms we target.
BUILTIN_MAP = {
    "void": "None",
    "bool": "c_bool",
    "_Bool": "c_bool",
    "char": "c_char",
    "signed char": "c_byte",
    "unsigned char": "c_ubyte",
    "short": "c_short",
    "unsigned short": "c_ushort",
    "int": "c_int",
    "unsigned int": "c_uint",
    "long": "c_long",
    "unsigned long": "c_ulong",
    "long long": "c_longlong",
    "unsigned long long": "c_ulonglong",
    "float": "c_float",
    "double": "c_double",
    "long double": "c_longdouble",
    # uint8_t etc. canonicalise to the underlying integer type via clang.
}


class Emitter:
    def __init__(self, meta: dict) -> None:
        self.meta = meta
        self.decls: list[dict] = meta["decls"]
        # Index by (kind, name) for quick lookup
        self.by_kind: dict[str, list[dict]] = {}
        for d in self.decls:
            self.by_kind.setdefault(d["kind"], []).append(d)

        # Collect every struct name referenced anywhere — defined ones plus
        # opaque ones that only appear as `{struct: name}` in function/field
        # types. Opaque structs get empty Structure subclasses so functions
        # taking pointers to them are still type-checkable.
        self.struct_names: set[str] = set()
        self.union_names: set[str] = set()
        self.enum_names: set[str] = set()
        self._collect_referenced_types()

        # Defined sets — struct names with full field info
        self.defined_structs = {d["name"] for d in self.by_kind.get("struct", [])}
        self.defined_unions = {d["name"] for d in self.by_kind.get("union", [])}
        self.defined_enums = {d["name"] for d in self.by_kind.get("enum", [])}

    def _walk_type(self, t: dict | None) -> None:
        if t is None:
            return
        if "ptr" in t:
            self._walk_type(t["ptr"])
        elif "array" in t:
            self._walk_type(t["array"])
        elif "struct" in t:
            self.struct_names.add(t["struct"])
        elif "union" in t:
            self.union_names.add(t["union"])
        elif "enum" in t:
            self.enum_names.add(t["enum"])
        elif "fn" in t:
            self._walk_type(t["fn"]["returns"])
            for p in t["fn"]["params"]:
                self._walk_type(p)

    def _collect_referenced_types(self) -> None:
        for d in self.decls:
            if d["kind"] in ("struct", "union"):
                self.struct_names.add(d["name"]) if d["kind"] == "struct" else self.union_names.add(d["name"])
                for f in d.get("fields", []):
                    self._walk_type(f["type"])
            elif d["kind"] == "enum":
                self.enum_names.add(d["name"])
            elif d["kind"] == "typedef":
                self._walk_type(d["type"])
            elif d["kind"] == "function":
                self._walk_type(d["returns"])
                for p in d["params"]:
                    self._walk_type(p["type"])

    # --- type rendering -------------------------------------------------

    def render_type(self, t: dict, *, position: str = "param") -> str:
        """Render a YAML type as a ctypes Python expression.

        position='return' returns 'None' for `void`, otherwise raises.
        position='param' / 'field' return a real ctype.
        """
        if "ptr" in t:
            pointee = t["ptr"]
            # Idiomatic specials
            pb = pointee.get("builtin", "")
            # Strip const/volatile prefixes from the spelling for matching.
            for qual in ("const ", "volatile ", "restrict "):
                if pb.startswith(qual):
                    pb = pb[len(qual):]
            if pb == "char":
                return "c_char_p"
            if pb == "void":
                return "c_void_p"
            # Function pointers in ctypes are CFUNCTYPE(...) on their own —
            # they're already pointer-shaped values. Don't wrap in POINTER.
            if "fn" in pointee:
                return self.render_type(pointee, position="field")
            inner = self.render_type(pointee, position="field")
            return f"POINTER({inner})"
        if "array" in t:
            inner = self.render_type(t["array"], position="field")
            return f"({inner} * {t['size']})"
        if "struct" in t:
            return self._py_name(t["struct"])
        if "union" in t:
            return self._py_name(t["union"])
        if "enum" in t:
            # ctypes treats enums as their underlying int type. We surface
            # the IntEnum class only at the python boundary; the wire-level
            # ctype is c_int.
            return "c_int"
        if "fn" in t:
            ret = self.render_type(t["fn"]["returns"], position="return")
            params = ", ".join(self.render_type(p, position="param")
                               for p in t["fn"]["params"])
            return f"CFUNCTYPE({ret}, {params})"
        if "builtin" in t:
            spelling = t["builtin"]
            # Strip leading 'const ' / 'volatile ' qualifiers — ctypes
            # ignores const-ness on integers/floats anyway.
            for qual in ("const ", "volatile ", "restrict "):
                if spelling.startswith(qual):
                    spelling = spelling[len(qual):]
            ct = BUILTIN_MAP.get(spelling)
            if ct == "None":
                if position != "return":
                    return "c_void_p"  # void in a non-return position is treated as void*
                return "None"
            if ct is None:
                return f"c_int  # TODO: builtin {spelling!r}"
            return ct
        return f"c_void_p  # TODO: unrecognised type {t!r}"

    @staticmethod
    def _py_name(c_name: str) -> str:
        """Pythonised name. We keep the C name verbatim as the class name —
        no PascalCase translation, no class inference. Disambiguating later
        is easier when the C name is preserved."""
        return c_name

    # --- emit blocks ----------------------------------------------------

    def emit(self) -> str:
        out: list[str] = []
        out.append(self._header())
        out.append(self._enum_block())
        out.append(self._struct_forward_decls())
        out.append(self._struct_field_assignments())
        out.append(self._function_block())
        out.append(self._footer())
        return "\n".join(s for s in out if s)

    def _header(self) -> str:
        mod = self.meta.get("module", "?")
        env = f"{mod.upper().replace('-', '_')}_LIB"
        return f'''"""{mod} ctypes bindings — auto-generated.

DO NOT EDIT. Regenerate with:
    tools/ffi-codegen/.venv/bin/python tools/ffi-codegen/emit/python/generate.py {mod}

Source: build/ffi/{mod}.yaml
Header: {self.meta.get("header", "?")}

Library loading is deferred — call {mod}.load(path) once at startup, or set
the {env} env var to a shared library path and the module loads it on import.
"""

from __future__ import annotations

import ctypes
import os
from ctypes import (
    CFUNCTYPE, POINTER, Structure, Union,
    c_bool, c_byte, c_char, c_char_p, c_double, c_float, c_int, c_int8,
    c_int16, c_int32, c_int64, c_long, c_longdouble, c_longlong, c_short,
    c_size_t, c_ubyte, c_uint, c_uint8, c_uint16, c_uint32, c_uint64,
    c_ulong, c_ulonglong, c_ushort, c_void_p,
)
from enum import IntEnum

# ---------------------------------------------------------------------------
# Library loading
# ---------------------------------------------------------------------------

_lib: ctypes.CDLL | None = None


def load(path: str) -> ctypes.CDLL:
    """Load the {mod} shared library and bind every function in this module
    to its argtypes / restype. Idempotent."""
    global _lib
    _lib = ctypes.CDLL(path)
    _bind_functions()
    return _lib


def _check_loaded() -> ctypes.CDLL:
    if _lib is None:
        raise RuntimeError(
            "{mod}: library not loaded — call {mod}.load(path) first, "
            "or set {env} to a shared library path."
        )
    return _lib

'''

    def _enum_block(self) -> str:
        lines = ["# ---------------------------------------------------------------------------",
                 "# Enums",
                 "# ---------------------------------------------------------------------------",
                 ""]
        for d in self.by_kind.get("enum", []):
            name = d.get("name") or ""
            if name:
                cls = self._py_name(name)
                lines.append(f"class {cls}(IntEnum):")
                for v in d["values"]:
                    lines.append(f"    {v['name']} = {v['value']}")
                lines.append("")
                # Also export each enum constant at module level so users can
                # write `ygui.YETTY_YGUI_WIDGET_BUTTON` directly without
                # going through the class.
                for v in d["values"]:
                    lines.append(f"{v['name']} = {cls}.{v['name']}")
                lines.append("")
            else:
                # Anonymous enum — emit values as plain module-level constants.
                for v in d["values"]:
                    lines.append(f"{v['name']} = {v['value']}")
                lines.append("")
        return "\n".join(lines)

    def _struct_forward_decls(self) -> str:
        all_names = sorted(self.struct_names | self.union_names)
        if not all_names:
            return ""
        # For cross-module struct refs (no decl in this module's YAML, but
        # referenced by a field, function return, or parameter), preload a
        # size hint so we can pad the forward declaration. Without this,
        # libffi rejects functions that return such a struct by value
        # because the size is unknown.
        external_sizes: dict[str, int] = {}
        for d in self.decls:
            if d["kind"] in ("struct", "union"):
                for f in d.get("fields", []):
                    self._collect_external_sizes(f["type"], external_sizes)
            elif d["kind"] == "function":
                self._collect_external_sizes(d["returns"], external_sizes)
                for p in d["params"]:
                    self._collect_external_sizes(p["type"], external_sizes)
            elif d["kind"] == "typedef":
                self._collect_external_sizes(d["type"], external_sizes)

        lines = ["# ---------------------------------------------------------------------------",
                 "# Struct / union forward declarations",
                 "# (opaque types declared empty; full layouts assigned below)",
                 "# ---------------------------------------------------------------------------",
                 ""]
        for n in all_names:
            base = "Union" if n in self.union_names else "Structure"
            lines.append(f"class {self._py_name(n)}({base}):")
            # If this type has no full decl in our YAML but we know its size
            # from a cross-module field reference, fix the layout to that
            # many bytes so containers using it have the right size.
            has_decl = (n in self.defined_structs or n in self.defined_unions)
            sz = external_sizes.get(n)
            if not has_decl and isinstance(sz, int) and sz > 0:
                # Cross-module type with known size — pad to that size so
                # containers laying it out get the right `sizeof`.
                lines.append(f"    _fields_ = [('_storage', c_uint8 * {sz})]")
            else:
                # Genuinely opaque (incomplete in this TU) — forward decl only.
                lines.append("    pass")
            lines.append("")
        return "\n".join(lines)

    def _topo_sort_by_value(self, defs: list[dict]) -> list[dict]:
        """Order struct/union decls so each one comes AFTER any record it
        embeds by value. References through `ptr` don't count.
        """
        by_name = {d["name"]: d for d in defs}

        def by_value_deps(d: dict) -> set[str]:
            deps: set[str] = set()
            def walk(t: dict | None) -> None:
                if t is None:
                    return
                if "ptr" in t:
                    return  # pointer breaks the dependency
                if "array" in t:
                    walk(t["array"])
                elif "struct" in t and t["struct"] in by_name:
                    deps.add(t["struct"])
                elif "union" in t and t["union"] in by_name:
                    deps.add(t["union"])
            for f in d.get("fields", []):
                walk(f["type"])
            return deps

        out: list[dict] = []
        emitted: set[str] = set()
        # Kahn-style: repeatedly emit any decl whose deps are all emitted.
        # Cycles via by-value would be illegal C, so this terminates.
        remaining = list(defs)
        while remaining:
            progress = False
            still: list[dict] = []
            for d in remaining:
                if by_value_deps(d) <= emitted:
                    out.append(d)
                    emitted.add(d["name"])
                    progress = True
                else:
                    still.append(d)
            remaining = still
            if not progress:
                # Should not happen for valid C; fall back to original order.
                out.extend(remaining)
                break
        return out

    def _collect_external_sizes(self, t: dict | None, out: dict[str, int]) -> None:
        """Walk a type tree and record `{name: size}` for record types that
        carry a size hint."""
        if t is None:
            return
        if "ptr" in t:
            self._collect_external_sizes(t["ptr"], out)
        elif "array" in t:
            self._collect_external_sizes(t["array"], out)
        elif "struct" in t and "size" in t:
            out.setdefault(t["struct"], t["size"])
        elif "union" in t and "size" in t:
            out.setdefault(t["union"], t["size"])

    def _struct_field_assignments(self) -> str:
        defined = [d for d in self.decls if d["kind"] in ("struct", "union") and d.get("fields")]
        if not defined:
            return ""
        # Topological sort by by-value dependencies. ctypes finalises a
        # struct's layout the moment it appears in another struct's _fields_,
        # so a record that references another record by VALUE must come
        # AFTER that record's own _fields_ assignment. By-pointer references
        # are not dependencies (POINTER + forward decl always work).
        defined = self._topo_sort_by_value(defined)
        lines = ["# ---------------------------------------------------------------------------",
                 "# Struct / union field layouts",
                 "# ---------------------------------------------------------------------------",
                 ""]
        for d in defined:
            cls = self._py_name(d["name"])
            anon_fields = [f["name"] for f in d["fields"] if f.get("anonymous")]
            if anon_fields:
                # ctypes flattens union/struct members onto the parent for
                # any field listed in _anonymous_. This matches the C
                # transparent-anonymous-union semantics.
                lines.append(f"{cls}._anonymous_ = {tuple(anon_fields)!r}")
            lines.append(f"{cls}._fields_ = [")
            for f in d["fields"]:
                t = self.render_type(f["type"], position="field")
                lines.append(f"    ({f['name']!r}, {t}),")
            lines.append("]")
            lines.append("")
        return "\n".join(lines)

    def _function_block(self) -> str:
        funcs = self.by_kind.get("function", [])
        if not funcs:
            return ""
        lines = ["# ---------------------------------------------------------------------------",
                 "# Function bindings",
                 "# ---------------------------------------------------------------------------",
                 "",
                 "def _bind_functions() -> None:",
                 "    \"\"\"Wire argtypes/restype on every function. Called by load().\"\"\"",
                 "    if _lib is None:",
                 "        return"]
        for d in funcs:
            name = d["name"]
            ret = self.render_type(d["returns"], position="return")
            argtypes = [self.render_type(p["type"], position="param") for p in d["params"]]
            arg_str = ", ".join(argtypes) if argtypes else ""
            lines.append(f"    _lib.{name}.argtypes = [{arg_str}]")
            lines.append(f"    _lib.{name}.restype = {ret}")
        lines.append("")

        # Module-level callable proxies. Defined as functions so that the
        # module imports cleanly even before load() is called; the proxy
        # raises a clear error on use if the lib isn't bound yet.
        for d in funcs:
            name = d["name"]
            params = ", ".join(p["name"] or f"_arg{i}"
                               for i, p in enumerate(d["params"]))
            call_args = params
            lines.append(f"def {name}({params}):")
            lines.append(f"    return _check_loaded().{name}({call_args})")
            lines.append("")
        return "\n".join(lines)

    def _footer(self) -> str:
        mod = self.meta.get("module", "?")
        env = f"{mod.upper().replace('-', '_')}_LIB"
        return f'''
# Auto-load if {env} is set in the environment. Useful for ad-hoc REPL
# work where the user doesn't want to call load() manually.
if (_p := os.environ.get("{env}")):
    load(_p)
'''


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module",
                    help="Module name (matches build/ffi/<module>.yaml)")
    ap.add_argument("--meta",
                    help="YAML input path (default: build/ffi/<module>.yaml)")
    ap.add_argument("--out",
                    help="Python output path (default: bindings/python/<module>.py)")
    args = ap.parse_args()

    meta_path = pathlib.Path(args.meta) if args.meta \
        else REPO / "build/ffi" / f"{args.module}.yaml"
    out_path = pathlib.Path(args.out) if args.out \
        else REPO / "bindings/python" / f"{args.module}.py"

    if not meta_path.exists():
        print(f"missing metadata: {meta_path}", file=sys.stderr)
        return 1
    with meta_path.open() as f:
        meta = yaml.safe_load(f)

    text = Emitter(meta).emit()

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(text)

    n_funcs = sum(1 for d in meta["decls"] if d["kind"] == "function")
    n_structs = sum(1 for d in meta["decls"] if d["kind"] == "struct")
    n_enums = sum(1 for d in meta["decls"] if d["kind"] == "enum")
    print(f"wrote {out_path.relative_to(REPO)}  "
          f"({n_funcs} fns, {n_structs} structs, {n_enums} enums)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
