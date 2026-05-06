#!/usr/bin/env python3
"""ygui FFI Stage-1 extractor.

Walks include/yetty/ygui/ygui.h with libclang and emits the raw type / decl
facts as YAML. No class / method / property inference yet — that lives in a
later stage. Just the bare AST translated into a portable representation.

Run:
    tools/ffi-codegen/.venv/bin/python tools/ffi-codegen/parse/ygui/parse.py

Output:
    build/ffi/ygui.yaml
"""

from __future__ import annotations

import pathlib
import sys

import yaml
from clang import cindex


REPO = pathlib.Path(__file__).resolve().parents[4]
HEADER = REPO / "include/yetty/ygui/ygui.h"
OUT = REPO / "build/ffi/ygui.yaml"

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
    clang = shutil.which("clang") or shutil.which("clang-18") or shutil.which("clang-19")
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

    # Record (struct or union)
    if t.kind == cindex.TypeKind.RECORD:
        decl = t.get_declaration()
        kind = "union" if decl.kind == cindex.CursorKind.UNION_DECL else "struct"
        return {kind: _record_name(decl)}

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
    appends them to `nested_out` so the emitter sees them before the parent."""
    fields = []
    for c in cursor.get_children():
        # Nested anonymous record types declared inline — emit them first
        # under their synthesised name so the parent's field can reference.
        if c.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL) \
                and c.is_anonymous() and c.is_definition():
            sub = visit_struct(c, nested_out)
            sub["kind"] = "union" if c.kind == cindex.CursorKind.UNION_DECL else "struct"
            nested_out.append(sub)
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


def in_scope(cursor: cindex.Cursor) -> bool:
    """True if the cursor was declared in include/yetty/ygui/."""
    if cursor.location.file is None:
        return False
    p = pathlib.Path(cursor.location.file.name).resolve()
    try:
        p.relative_to(REPO / "include/yetty/ygui")
    except ValueError:
        return False
    return True


def main() -> int:
    if not HEADER.exists():
        print(f"missing header: {HEADER}", file=sys.stderr)
        return 1

    index = cindex.Index.create()
    tu = index.parse(str(HEADER), args=CFLAGS,
                     options=cindex.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD
                     | cindex.TranslationUnit.PARSE_INCOMPLETE)

    fatal = [d for d in tu.diagnostics if d.severity >= cindex.Diagnostic.Error]
    if fatal:
        for d in fatal:
            print(f"clang: {d.spelling}  ({d.location.file}:{d.location.line})",
                  file=sys.stderr)
        return 2

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

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w") as f:
        yaml.safe_dump(
            {
                "schema_version": 1,
                "module": "ygui",
                "header": str(HEADER.relative_to(REPO)),
                "decls": decls,
            },
            f,
            sort_keys=False,
            default_flow_style=False,
        )

    counts: dict[str, int] = {}
    for d in decls:
        counts[d["kind"]] = counts.get(d["kind"], 0) + 1
    print(f"wrote {OUT.relative_to(REPO)}  ({sum(counts.values())} decls: "
          + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
