#!/usr/bin/env python3
"""FFI Stage-1 extractor.

Walks a public yetty header with libclang and emits the raw type / decl
facts as YAML. No class / method / property inference yet — that lives in a
later stage. Just the bare AST translated into a portable representation.

Run:
    tools/ffi-codegen/.venv/bin/python tools/ffi-codegen/parse/parse.py <module>
    # e.g. ygui, yface, ydraw-core

The module name selects:
  - header   include/yetty/<module>/<module>.h  (override with --header)
  - scope    include/yetty/<module>/             (override with --scope)
  - output   build/ffi/<module>.yaml             (override with --out)
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import yaml
from clang import cindex


REPO = pathlib.Path(__file__).resolve().parents[3]

# Compile flags should mirror the real build. For day-one we hand-pick the
# include roots and the C standard. A future iteration will read these from
# build-*/compile_commands.json so the FFI metadata always tracks the build.
def _resource_dir() -> str:
    """Locate clang's resource directory (where stddef.h, stdint.h ship).

    The libclang Python wheel doesn't include the resource headers, so we
    point at the system clang's tree. If clang isn't on PATH, the user will
    see a clear error from the parser invocation rather than a cryptic
    missing-stddef.h.
    """
    import shutil, subprocess
    clang = (shutil.which("clang-20")
             or shutil.which("clang-19")
             or shutil.which("clang-18"))
    if not clang:
        return ""
    try:
        return subprocess.check_output([clang, "-print-resource-dir"],
                                       text=True).strip()
    except subprocess.CalledProcessError:
        return ""


_RESOURCE_DIR = _resource_dir()

CFLAGS = [
    "-x", "c",
    "-std=c11",
    f"-I{REPO}/include",
    f"-I{REPO}/src",
    "-DYETTY_FFI_PARSE",
]
if _RESOURCE_DIR:
    CFLAGS += [f"-isystem{_RESOURCE_DIR}/include"]


def _record_name(cursor: cindex.Cursor) -> str:
    """Stable name for a struct / union declaration.

    Anonymous records get a synthesised identifier of the form
    `_yetty_anon_<line>_<col>`; this is a valid Python (and Rust / Go)
    identifier so downstream emitters don't need to sanitise names.
    """
    if cursor.is_anonymous():
        loc = cursor.location
        return f"_yetty_anon_{loc.line}_{loc.column}"
    return cursor.spelling


def _enum_name(cursor: cindex.Cursor) -> str:
    if cursor.is_anonymous():
        loc = cursor.location
        return f"_yetty_anon_enum_{loc.line}_{loc.column}"
    return cursor.spelling


def render_type(t: cindex.Type) -> dict:
    """Translate a clang Type into a portable dict.

    The shape is small and stable on purpose; the consumer decides how to
    realise it in the target language. Pointer pointee is rendered nested,
    so a `const char *` becomes {ptr: {builtin: char, const: true}}.
    """
    t = t.get_canonical() if t.kind == cindex.TypeKind.TYPEDEF else t
    spelling = t.spelling

    # Pointer
    if t.kind == cindex.TypeKind.POINTER:
        pointee = t.get_pointee()
        return {
            "ptr": render_type(pointee),
            "const": pointee.is_const_qualified() or None,
        }

    # Constant-size array
    if t.kind == cindex.TypeKind.CONSTANTARRAY:
        return {
            "array": render_type(t.get_array_element_type()),
            "size": t.get_array_size(),
        }

    # Incomplete / FAM array
    if t.kind == cindex.TypeKind.INCOMPLETEARRAY:
        return {"array": render_type(t.get_array_element_type()), "size": 0}

    # Record (struct or union). Include size/align so the emitter can
    # synthesise opaque-with-known-size for types declared outside the
    # module's scope (e.g. struct yetty_ycore_error referenced from a
    # ydraw-core result type but defined in ycore).
    if t.kind == cindex.TypeKind.RECORD:
        decl = t.get_declaration()
        kind = "union" if decl.kind == cindex.CursorKind.UNION_DECL else "struct"
        return {kind: _record_name(decl),
                "size": t.get_size(),
                "align": t.get_align()}

    # Enum
    if t.kind == cindex.TypeKind.ENUM:
        return {"enum": _enum_name(t.get_declaration())}

    # Elaborated wrapper (e.g. `struct foo` written explicitly)
    if t.kind == cindex.TypeKind.ELABORATED:
        return render_type(t.get_named_type())

    # Function pointer (rare — ygui has none currently)
    if t.kind == cindex.TypeKind.FUNCTIONPROTO:
        return {
            "fn": {
                "returns": render_type(t.get_result()),
                "params": [render_type(p) for p in t.argument_types()],
            }
        }

    # Builtin / fallback — record the spelling verbatim. Downstream emitters
    # decide how to translate `int`, `uint32_t`, `size_t`, …
    return {"builtin": spelling}


def visit_struct(cursor: cindex.Cursor, nested_out: list[dict]) -> dict:
    """Emit a struct/union decl. Recurses into nested anonymous records and
    appends them to `nested_out`. Distinguishes the two anonymous-record
    forms in C:

      union { float a; int b; } data;    ← named field; emit FIELD_DECL only
      union { ptr value; error error; }; ← TRANSPARENT; libclang produces
                                            INDIRECT_FIELD_DECL children on
                                            the parent. Synthesise a field
                                            with anonymous=true so emitters
                                            can flatten via ctypes
                                            `_anonymous_` / Rust flat-fields
                                            / etc.
    """
    fields = []
    # Anonymous record decls that have a corresponding INDIRECT_FIELD_DECL
    # are transparent — they need a synthesised field. Anonymous records
    # that DON'T have an indirect field are named (their type is referenced
    # by a regular FIELD_DECL by name); we just emit them and let the
    # FIELD_DECL refer to them.
    transparent_anon_decls: dict[int, cindex.Cursor] = {}
    for c in cursor.get_children():
        if c.kind == cindex.CursorKind.FIELD_DECL:
            # If this field references the type of a sibling anonymous
            # record, that record is NAMED (not transparent).
            pass
    for c in cursor.get_children():
        if c.kind == cindex.CursorKind.STRUCT_DECL or c.kind == cindex.CursorKind.UNION_DECL:
            if c.is_anonymous() and c.is_definition():
                transparent_anon_decls[c.hash] = c
    # Subtract any anonymous record that's referenced by a regular field —
    # those are the `union { … } name;` form.
    for c in cursor.get_children():
        if c.kind == cindex.CursorKind.FIELD_DECL:
            field_decl = c.type.get_declaration()
            if field_decl is not None and field_decl.hash in transparent_anon_decls:
                transparent_anon_decls.pop(field_decl.hash, None)

    for c in cursor.get_children():
        if c.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL) \
                and c.is_anonymous() and c.is_definition():
            sub = visit_struct(c, nested_out)
            sub["kind"] = "union" if c.kind == cindex.CursorKind.UNION_DECL else "struct"
            nested_out.append(sub)
            # Only synthesise a field if this anonymous record is
            # transparent (no FIELD_DECL pulls it in).
            if c.hash in transparent_anon_decls:
                fields.append({
                    "name": _anon_field_name(c),
                    "type": {sub["kind"]: sub["name"]},
                    "offset": 0,  # transparent anon members start at 0
                    "anonymous": True,
                })
            continue
        if c.kind != cindex.CursorKind.FIELD_DECL:
            continue
        ftype = c.type
        fields.append({
            "name": c.spelling,
            "type": render_type(ftype),
            "offset": cursor.type.get_offset(c.spelling),
        })
    return {
        "kind": "struct",
        "name": _record_name(cursor),
        "size": cursor.type.get_size(),
        "align": cursor.type.get_align(),
        "fields": fields,
        "loc": {"file": str(cursor.location.file),
                "line": cursor.location.line},
    }


def _anon_field_name(cursor: cindex.Cursor) -> str:
    """Synthetic field name for a transparent anonymous record embedding."""
    return f"_anon_{cursor.location.line}_{cursor.location.column}"


def visit_enum(cursor: cindex.Cursor) -> dict:
    values = [{"name": c.spelling, "value": c.enum_value}
              for c in cursor.get_children()
              if c.kind == cindex.CursorKind.ENUM_CONSTANT_DECL]
    # Anonymous enums get an empty name — downstream emitters treat the
    # constants as bare module-level values.
    name = "" if cursor.is_anonymous() else cursor.spelling
    return {
        "kind": "enum",
        "name": name,
        "underlying": render_type(cursor.enum_type),
        "values": values,
        "loc": {"file": str(cursor.location.file),
                "line": cursor.location.line},
    }


def visit_typedef(cursor: cindex.Cursor) -> dict:
    return {
        "kind": "typedef",
        "name": cursor.spelling,
        "type": render_type(cursor.underlying_typedef_type),
        "loc": {"file": str(cursor.location.file),
                "line": cursor.location.line},
    }


def visit_function(cursor: cindex.Cursor) -> dict:
    params = []
    for c in cursor.get_children():
        if c.kind != cindex.CursorKind.PARM_DECL:
            continue
        params.append({"name": c.spelling, "type": render_type(c.type)})
    annotations = [c.spelling for c in cursor.get_children()
                   if c.kind == cindex.CursorKind.ANNOTATE_ATTR]
    out = {
        "kind": "function",
        "name": cursor.spelling,
        "returns": render_type(cursor.result_type),
        "params": params,
        "loc": {"file": str(cursor.location.file),
                "line": cursor.location.line},
    }
    if annotations:
        out["annotations"] = annotations
    return out


def _collect_referenced_record_names(decl: dict, out: set[str]) -> None:
    """Walk a decl's types and record any struct/union name referenced."""
    def walk(t: dict | None) -> None:
        if t is None:
            return
        if "ptr" in t:
            walk(t["ptr"])
        elif "array" in t:
            walk(t["array"])
        elif "struct" in t:
            out.add(t["struct"])
        elif "union" in t:
            out.add(t["union"])
        elif "fn" in t:
            walk(t["fn"]["returns"])
            for p in t["fn"]["params"]:
                walk(p)
    if decl["kind"] in ("struct", "union"):
        for f in decl.get("fields", []):
            walk(f["type"])
    elif decl["kind"] == "function":
        walk(decl["returns"])
        for p in decl["params"]:
            walk(p["type"])
    elif decl["kind"] == "typedef":
        walk(decl["type"])


def make_in_scope(scope: pathlib.Path):
    scope = scope.resolve()
    def in_scope(cursor: cindex.Cursor) -> bool:
        if cursor.location.file is None:
            return False
        p = pathlib.Path(cursor.location.file.name).resolve()
        try:
            p.relative_to(scope)
        except ValueError:
            return False
        return True
    return in_scope


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("module",
                    help="Module name, e.g. 'ygui' or 'yface'")
    ap.add_argument("--header",
                    help="Public header path (default: include/yetty/<module>/<module>.h)")
    ap.add_argument("--scope",
                    help="Directory to count as in-scope for emitted decls "
                         "(default: include/yetty/<module>/)")
    ap.add_argument("--out",
                    help="YAML output path (default: build/ffi/<module>.yaml)")
    args = ap.parse_args()

    header = pathlib.Path(args.header) if args.header \
        else REPO / "include/yetty" / args.module / f"{args.module}.h"
    scope = pathlib.Path(args.scope) if args.scope \
        else REPO / "include/yetty" / args.module
    out = pathlib.Path(args.out) if args.out \
        else REPO / "build/ffi" / f"{args.module}.yaml"

    if not header.exists():
        print(f"missing header: {header}", file=sys.stderr)
        return 1

    index = cindex.Index.create()
    tu = index.parse(str(header), args=CFLAGS,
                     options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
                     | cindex.TranslationUnit.PARSE_INCOMPLETE)

    fatal = [d for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
    if fatal:
        for d in fatal:
            print(f"clang: {d.spelling}  ({d.location.file}:{d.location.line})",
                  file=sys.stderr)
        return 2

    in_scope = make_in_scope(scope)
    decls: list[dict] = []
    seen: set[tuple] = set()

    def push(rec: dict) -> None:
        if rec["kind"] == "enum" and not rec["name"]:
            key = ("enum", rec["loc"]["file"], rec["loc"]["line"])
        else:
            key = (rec["kind"], rec["name"])
        if key in seen:
            return
        seen.add(key)
        decls.append(rec)

    for cursor in tu.cursor.get_children():
        if not in_scope(cursor):
            continue
        kind = cursor.kind
        nested: list[dict] = []
        if kind == cindex.CursorKind.STRUCT_DECL and cursor.is_definition():
            rec = visit_struct(cursor, nested)
        elif kind == cindex.CursorKind.UNION_DECL and cursor.is_definition():
            rec = visit_struct(cursor, nested)
            rec["kind"] = "union"
        elif kind == cindex.CursorKind.ENUM_DECL:
            rec = visit_enum(cursor)
        elif kind == cindex.CursorKind.TYPEDEF_DECL:
            rec = visit_typedef(cursor)
        elif kind == cindex.CursorKind.FUNCTION_DECL:
            rec = visit_function(cursor)
        else:
            continue
        # Nested anonymous records are emitted before the parent so the
        # parent's `_fields_` references resolve in declaration order.
        for sub in nested:
            push(sub)
        push(rec)

    # Pass 2: pull in record decls from outside our scope that are referenced
    # by an in-scope decl. Without this, cross-module result types like
    # yetty_ycore_void_result are opaque in our YAML and downstream emitters
    # can't generate working bindings (libffi rejects struct-returns when
    # the size is unknown). We walk the AST once to find every record decl
    # by spelling, then emit any that are referenced.
    referenced: set[str] = set()
    for d in decls:
        _collect_referenced_record_names(d, referenced)
    emitted = {(d["kind"], d["name"]) for d in decls}

    record_decls: dict[str, cindex.Cursor] = {}
    def collect_records(cursor: cindex.Cursor) -> None:
        if cursor.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL) \
                and cursor.is_definition():
            name = _record_name(cursor)
            record_decls.setdefault(name, cursor)
        for ch in cursor.get_children():
            collect_records(ch)
    collect_records(tu.cursor)

    deferred = list(referenced - {n for k, n in emitted if k in ("struct", "union")})
    while deferred:
        new_refs: set[str] = set()
        for name in sorted(deferred):
            cur = record_decls.get(name)
            if cur is None:
                continue
            sub_nested: list[dict] = []
            rec = visit_struct(cur, sub_nested)
            if cur.kind == cindex.CursorKind.UNION_DECL:
                rec["kind"] = "union"
            for sub in sub_nested:
                if (sub["kind"], sub["name"]) not in emitted:
                    push(sub)
                    emitted.add((sub["kind"], sub["name"]))
                    _collect_referenced_record_names(sub, new_refs)
            if (rec["kind"], rec["name"]) not in emitted:
                push(rec)
                emitted.add((rec["kind"], rec["name"]))
                _collect_referenced_record_names(rec, new_refs)
        deferred = list(new_refs - {n for k, n in emitted if k in ("struct", "union")})

    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w") as f:
        yaml.safe_dump(
            {
                "schema_version": 1,
                "module": args.module,
                "header": str(header.relative_to(REPO)),
                "decls": decls,
            },
            f,
            sort_keys=False,
            default_flow_style=False,
        )

    counts: dict[str, int] = {}
    for d in decls:
        counts[d["kind"]] = counts.get(d["kind"], 0) + 1
    print(f"wrote {out.relative_to(REPO)}  ({sum(counts.values())} decls: "
          + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
