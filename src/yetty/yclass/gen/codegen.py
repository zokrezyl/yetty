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
    <class>.h          — accessor decl, includes sibling methods.h.
                         GENERATED (replaces any hand-written class header).
    methods.h      — every public stub in this module.
    rpc.h          — every <class>_create() in this module.

  Internal (under <module_src_dir>/):
    <class>.gen.c      — accessor body, included at the foot of <class>.c.
    methods.gen.c      — public stub bodies.
    rpc.gen.c          — skels + accessor/skel lookups + yetty_<module>_register().
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
    if r in ("int", "int32_t"):
        # int holds an int32_t value, so the read accessor reports both
        # through the shared yetty_ycore_int result (the setter still takes
        # the field's own width).
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
        # A few ycore struct result types drop the `yetty_ycore_` namespace
        # in their YETTY_YRESULT_DECLARE name (see ycore/types.h); map them
        # so the generic struct→result rule resolves to the real symbol.
        struct_result_overrides = {
            "yetty_ycore_rectangle": "rectangle",
            "yetty_ycore_pixel_size": "pixel_size",
            "yetty_ycore_pixel_coord": "pixel_coord",
        }
        name = m.group(1)
        return struct_result_overrides.get(name, name)
    return r


def result_type(ret: str) -> str:
    """The full C type — `struct <id>_result`."""
    return f"struct {result_type_id(ret)}_result"


def ret_value_type(rid: str):
    """Underlying value C type for a Result identifier. None for void
    (no payload).

    Mirrors:
      - `YETTY_YRESULT_DECLARE` table in yetty/ycore/result.h
        (yetty_ycore_void / _int / _size)
      - Public scalar entries in yetty/ycore/types.h
        (uint32, float)
      - Module-local convention: `_ptr` suffix → `struct <prefix> *`,
        any other id → `struct <id>` (matches the standard
        `YETTY_YRESULT_DECLARE(<id>, struct <id>)` idiom modules use).

    Result ids declared over scalar typedefs / primitives MUST be
    listed here — otherwise the fallback `struct <id>` produces
    invalid C (e.g. `struct uint32` for `YETTY_YRESULT_DECLARE(uint32,
    uint32_t)`). Add entries as ycore grows."""
    known_scalars = {
        "yetty_ycore_void": None,
        "yetty_ycore_int": "int",
        "yetty_ycore_size": "size_t",
        "uint32": "uint32_t",
        "float": "float",
    }
    if rid in known_scalars:
        return known_scalars[rid]
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
#   local@<DOMAIN>:<SLOT>                             on any fn       — mark the named slot
#                                                                       as local-only; the
#                                                                       public stub is built
#                                                                       without an RPC branch
#                                                                       and the slot is not
#                                                                       added to the skel
#                                                                       table. Use for slots
#                                                                       whose arguments cannot
#                                                                       be wire-marshalled
#                                                                       (raw producer pointers
#                                                                       to local-only carriers
#                                                                       such as emit contexts).
#
# Methods are inferred — every distinct SLOT whose owning domain equals
# the current module becomes a public method. Its C signature is taken
# from the first impl encountered. Cross-domain overrides do NOT
# introduce a new method entry in this module: the slot's public stub
# is owned by the slot's home module and is included from there.

def ast_dump(path: Path, include_dirs: list) -> dict:
    clang = os.environ.get("CLANG", "clang")
    # Tolerate semantic errors in the source — what we need from clang
    # is the parse tree (Decl + annotation nodes), not a clean
    # syntax-check. ygui's annotated sources reference codegen-emitted
    # public-stub symbols (yetty_ygui_widget_paint, yetty_ygui_constructor,
    # etc.) that won't exist until methods.h has been written, and
    # the placeholder methods.h is intentionally empty. We let
    # clang emit "undeclared identifier" errors but still produce JSON
    # AST as long as the file parses syntactically.
    # -DYCLASS_CODEGEN: header-destined content (types, typedefs, enums,
    # vtable structs, result-decls, forward-decls, includes) is written in
    # the .c inside `#ifdef YCLASS_CODEGEN` blocks. Defining it here makes
    # those declarations visible to the parse (so function signatures that
    # use them resolve to the real type, not int via error-recovery); the
    # real build leaves it undefined so the single definition lives in the
    # generated header the .c includes.
    cmd = [clang, "-Xclang", "-ast-dump=json", "-fsyntax-only", "-std=c2x",
           "-DYCLASS_CODEGEN", "-ferror-limit=0", "-Wno-error", "-Wno-everything"]
    for d in include_dirs:
        cmd.append(f"-I{d}")
    extra = os.environ.get("YCLASS_CODEGEN_INCLUDES", "")
    for d in extra.split(":") if extra else []:
        d = d.strip()
        if d:
            cmd.append(f"-I{d}")
    cmd.append(str(path))
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    # Don't abort on returncode != 0 — clang exits non-zero on any
    # semantic error but the AST dump on stdout is still well-formed
    # JSON in those cases.
    if not r.stdout:
        sys.stderr.write(r.stderr)
        sys.exit(1)
    try:
        return json.loads(r.stdout)
    except json.JSONDecodeError:
        # Genuine syntax error — bail with clang's stderr so the caller
        # can see what went wrong.
        sys.stderr.write(r.stderr)
        sys.exit(1)


def _annotate_value(attr_node: dict, file_bytes_cache: dict, current_file: str):
    """Decode an AnnotateAttr's payload string. The byte offsets in the
    range nodes index into the FILE the attribute was declared in —
    which may be a header pulled in transitively, not the .c source
    fed to codegen. The walk-state's `current_file` (updated whenever
    a node carries a `loc.file` indicator) tells us which file to
    slice. `file_bytes_cache` memoises the read of each header. """
    rng = attr_node.get("range", {})
    beg = rng.get("begin", {}); end = rng.get("end", {})
    if "offset" not in beg or "offset" not in end:
        return None
    # The range itself may also carry a `file` override (the attribute
    # could live in a header included only inside one specific decl).
    attr_file = beg.get("file") or current_file
    if not attr_file:
        return None
    blob = file_bytes_cache.get(attr_file)
    if blob is None:
        try:
            blob = Path(attr_file).read_bytes()
        except OSError:
            return None
        file_bytes_cache[attr_file] = blob
    start = beg["offset"]
    stop = end["offset"] + end.get("tokLen", 1)
    text = blob[start:stop].decode("utf-8", errors="replace")
    m = re.search(r'annotate\s*\(\s*"([^"]*)"', text)
    return m.group(1) if m else None


def _collect_annotations(decl: dict, file_bytes_cache: dict, current_file: str) -> list:
    out = []
    for child in decl.get("inner", []):
        if child.get("kind") == "AnnotateAttr":
            v = _annotate_value(child, file_bytes_cache, current_file)
            if v: out.append(v)
    return out


def _field_access(field: dict, file_bytes_cache: dict, current_file: str) -> tuple:
    """Decide which accessors a data member exposes, from its annotations:

        [[clang::annotate("property")]]     read + write  (getter + setter)
        [[clang::annotate("property:ro")]]  read-only      (getter only)
        [[clang::annotate("property:wo")]]  write-only     (setter only)

    A member with no `property` annotation stays private — no accessor is
    generated, so only the owning class (which sees the struct definition)
    can touch it. Returns (emit_getter, emit_setter)."""
    emit_getter = emit_setter = False
    for ann in _collect_annotations(field, file_bytes_cache, current_file):
        parts = [p.strip() for p in ann.split(":")]
        if parts[0] != "property":
            continue
        mode = parts[1] if len(parts) > 1 else "rw"
        if mode == "rw":
            emit_getter = emit_setter = True
        elif mode == "ro":
            emit_getter = True
        elif mode == "wo":
            emit_setter = True
        else:
            sys.stderr.write(
                f"error: member '{field.get('name')}' has property mode "
                f"'{mode}'; expected rw, ro or wo\n")
            sys.exit(1)
    return emit_getter, emit_setter


def _record_fields(decl: dict, file_bytes_cache: dict, current_file: str) -> list:
    """Name, C type and exposed-accessor flags for each member of a data
    struct. The struct definition never leaves the owning .c; this drives the
    generated getter/setter API other classes use instead of touching it."""
    fields = []
    for child in decl.get("inner", []):
        if child.get("kind") == "FieldDecl":
            emit_getter, emit_setter = _field_access(child, file_bytes_cache, current_file)
            fields.append({
                "name": child.get("name") or "",
                "type": child.get("type", {}).get("qualType", ""),
                "get": emit_getter,
                "set": emit_setter,
            })
    return fields


def _fn_args(decl: dict) -> list:
    args = []
    for child in decl.get("inner", []):
        if child.get("kind") == "ParmVarDecl":
            args.append({
                "name": child.get("name") or "",
                "type": child.get("type", {}).get("qualType", ""),
            })
    return args


def _fmt_param(a: dict) -> str:
    """Render one arg as a C parameter `<type> <name>` (or just `<type>`
    for an unnamed param). No space before a pointer star so the output
    reads like hand-written prototypes (`const char *src`, not
    `const char * src`)."""
    t = a["type"]
    n = a["name"]
    if not n:
        return t
    return f"{t}{n}" if t.endswith("*") else f"{t} {n}"


def _fmt_params(args: list) -> str:
    return ", ".join(_fmt_param(a) for a in args) if args else "void"


def _fmt_proto(return_type: str, name: str, args: list) -> str:
    """A function prototype line. No space between a pointer return type
    and the name (`const char *foo(...)`, not `const char * foo(...)`)."""
    sep = "" if return_type.endswith("*") else " "
    return f"{return_type}{sep}{name}({_fmt_params(args)});"


def _scan_codegen_blocks(text: str) -> list:
    """Bodies of `#ifdef YCLASS_CODEGEN ... #endif` blocks in a source
    file. This is the header-destined content the real build skips
    (types, typedefs, enums, vtable structs, result-decls, forward-decls,
    includes); codegen copies each block verbatim into the generated
    header. Matches the closing #endif with proper nesting so a block may
    itself contain feature `#if`/#ifdef/#ifndef ... #endif pairs."""
    out = []
    lines = text.split("\n")
    i = 0
    open_re = re.compile(r'^[ \t]*#[ \t]*(if|ifdef|ifndef)\b')
    endif_re = re.compile(r'^[ \t]*#[ \t]*endif\b')
    start_re = re.compile(r'^[ \t]*#[ \t]*ifdef[ \t]+YCLASS_CODEGEN[ \t]*$')
    while i < len(lines):
        if start_re.match(lines[i]):
            depth = 1
            j = i + 1
            body = []
            while j < len(lines) and depth > 0:
                if open_re.match(lines[j]):
                    depth += 1
                elif endif_re.match(lines[j]):
                    depth -= 1
                    if depth == 0:
                        break
                body.append(lines[j])
                j += 1
            chunk = "\n".join(body).strip("\n")
            if chunk.strip():
                out.append(chunk)
            i = j + 1
        else:
            i += 1
    return out


def _parse_return_type(qual_type: str) -> str:
    m = re.match(r"^(.*?)\s*\((.*)\)$", qual_type.strip())
    return m.group(1).strip() if m else qual_type


def _walk_decls(node: dict, current_file: str = None):
    """Yield FunctionDecl/RecordDecl nodes paired with the file they
    were declared in.

    clang's JSON AST uses a STATEFUL "current file" model: a location
    only carries `file` when it differs from the immediately preceding
    location in document order. So the file tracker is global to the
    whole walk, not lexically scoped to a parent's subtree — a sibling
    that crosses into a header changes the state for later siblings
    too. We thread the tracker through via a mutable single-element
    list so updates anywhere in the recursion are visible everywhere
    after that point. """
    state = [current_file]

    def visit(n):
        # Recurse into BEGIN-locations too: clang puts the file
        # indicator on the begin/end of a range when no top-level
        # `loc.file` exists. Without this, attribute-only nodes
        # (which carry `range` but no `loc`) wouldn't update state.
        loc = n.get("loc")
        if isinstance(loc, dict) and "file" in loc:
            state[0] = loc["file"]
        rng = n.get("range")
        if isinstance(rng, dict):
            beg = rng.get("begin", {})
            if isinstance(beg, dict) and "file" in beg:
                state[0] = beg["file"]
        if n.get("kind") in ("FunctionDecl", "RecordDecl"):
            yield n, state[0]
        for child in n.get("inner", []) or []:
            yield from visit(child)

    yield from visit(node)


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


# ---------------------------------------------------------------------------
# Type harvesting — emit concrete layouts (struct fields, enum values) for
# every by-value type the module's signatures reference, so model.yaml is
# self-describing and the FFI generator never needs to re-parse C. Pointers
# stay opaque (the binding treats them as handles), scalars/typedefs are known
# to the per-language runtime, so only NON-pointer struct / enum types are
# emitted, resolved transitively through struct fields.
# ---------------------------------------------------------------------------

def _record_field_list(decl: dict) -> list:
    """Field list for a RecordDecl, inlining anonymous nested struct/union
    members. An anonymous member is emitted as {name, kind: struct|union,
    fields: [...]} instead of {name, type} — so a Result's anonymous
    {error,value} union is captured structurally rather than as clang's
    path-bearing `(anonymous at …)` type string (which would also be
    non-deterministic across machines)."""
    fields = []
    pending_anon = None  # (kind, fields) from an immediately-preceding anon record
    for child in decl.get("inner", []) or []:
        ckind = child.get("kind")
        if ckind == "RecordDecl":
            if not child.get("name"):
                pending_anon = (child.get("tagUsed", "struct"), _record_field_list(child))
            continue  # named nested records are captured as their own entry
        if ckind != "FieldDecl":
            continue
        name = child.get("name") or ""
        qual = child.get("type", {}).get("qualType", "")
        if ("(anonymous" in qual or "(unnamed" in qual) and pending_anon is not None:
            anon_kind, anon_fields = pending_anon
            fields.append({"name": name, "kind": anon_kind, "fields": anon_fields})
        else:
            fields.append({"name": name, "type": qual})
        pending_anon = None
    return fields


def _collect_type_decls(node: dict, record_reg: dict, enum_reg: dict):
    """Index every named struct/union definition (tag -> field list) and enum
    (tag -> value list) reachable in the AST. Forward declarations (no fields)
    never overwrite a real definition."""
    if not isinstance(node, dict):
        return
    kind = node.get("kind")
    if kind == "RecordDecl" and node.get("name"):
        fields = _record_field_list(node)
        if fields:
            record_reg[node["name"]] = fields
    elif kind == "EnumDecl" and node.get("name"):
        values = _enum_values(node)
        if values:
            enum_reg[node["name"]] = values
    for child in node.get("inner", []) or []:
        _collect_type_decls(child, record_reg, enum_reg)


def _first_int_value(node: dict):
    """Best-effort: the first decimal integer literal under an enumerator's
    initialiser subtree, or None for an implicit (auto-incremented) value."""
    for child in node.get("inner", []) or []:
        v = child.get("value")
        if isinstance(v, str) and re.fullmatch(r"-?\d+", v):
            return int(v)
        sub = _first_int_value(child)
        if sub is not None:
            return sub
    return None


def _enum_values(decl: dict) -> list:
    values = []
    counter = 0
    for child in decl.get("inner", []) or []:
        if child.get("kind") != "EnumConstantDecl":
            continue
        explicit = _first_int_value(child)
        if explicit is not None:
            counter = explicit
        values.append({"name": child.get("name") or "", "value": counter})
        counter += 1
    return values


def _classify_type(type_str: str):
    """Map a C type string to (category, tag). category is one of
    'ptr' (opaque handle), 'struct', 'union', 'enum', or 'scalar'
    (primitive / typedef the runtime already knows). tag is the bare
    record/enum name for struct/union/enum, else None."""
    t = type_str.strip()
    if "*" in t:
        return ("ptr", None)
    t = re.sub(r"^(const|volatile)\s+", "", t).strip()
    # Strip a trailing array extent (`T[4]`, `T[4][2]`) so an array-of-struct
    # field resolves to its element type for harvesting + layout.
    t = re.sub(r"(\s*\[[0-9]+\])+$", "", t).strip()
    m = re.match(r"^struct\s+(\w+)$", t)
    if m:
        return ("struct", m.group(1))
    m = re.match(r"^union\s+(\w+)$", t)
    if m:
        return ("union", m.group(1))
    m = re.match(r"^enum\s+(\w+)$", t)
    if m:
        return ("enum", m.group(1))
    return ("scalar", None)


def _harvest_types(model: dict, record_reg: dict, enum_reg: dict) -> list:
    """Transitive closure of by-value struct/enum types referenced by the
    module's methods, class data fields, and exposed functions. Result-named
    structs are tagged kind 'result' so the FFI generator can unwrap them."""
    seen: set = set()
    queue: list = []

    def consider(type_str: str):
        category, tag = _classify_type(type_str or "")
        if category in ("struct", "union") and tag in record_reg and tag not in seen:
            seen.add(tag)
            queue.append(("struct", tag))
        elif category == "enum" and tag in enum_reg and tag not in seen:
            seen.add(tag)
            queue.append(("enum", tag))

    def consider_fields(fields: list):
        """Walk a field list (including inlined anonymous nested records) so
        every referenced by-value type is pulled into the closure."""
        for field in fields:
            if "fields" in field:
                consider_fields(field["fields"])
            elif "type" in field:
                consider(field["type"])

    for method in model.get("methods", []):
        consider(method.get("return_type", ""))
        for arg in method.get("args", []):
            consider(arg.get("type", ""))
    for cls in model.get("classes", []):
        for field in cls.get("data_fields", []):
            consider(field.get("type", ""))
    for item in model.get("exposed", []):
        if "return_type" in item:
            consider(item["return_type"])
        for arg in item.get("args", []):
            consider(arg.get("type", ""))
    # Every regular class gets a generated `<class>_create` returning
    # yetty_yclass_object_ptr_result (see rpc.h). No method signature names
    # it, so seed it explicitly — the FFI needs its layout to type create().
    if any(cls.get("type") == "regular" for cls in model.get("classes", [])):
        consider("struct yetty_yclass_object_ptr_result")

    types: list = []
    cursor = 0
    while cursor < len(queue):
        category, tag = queue[cursor]
        cursor += 1
        if category == "struct":
            fields = record_reg[tag]
            consider_fields(fields)  # transitive: grows the queue
            kind = "result" if tag.endswith("_result") else "struct"
            types.append({"name": tag, "kind": kind, "fields": fields})
        else:
            types.append({"name": tag, "kind": "enum", "values": enum_reg[tag]})

    types.sort(key=lambda entry: entry["name"])
    return types


def parse_sources(include_dirs: list, sources: list, module: str) -> dict:
    """Walk annotated sources, build the model. `module` is the
    codegen's CLI argument; the `<domain>` segment in every class/
    mixin/override annotation MUST equal it. parent/uses annotations
    may name a different domain (cross-module reference). Override
    signatures are NOT checked here — the generator emits a C-side
    typedef assignment per override that the compiler type-checks."""
    methods: dict = {}
    classes: dict = {}
    local_slots: set = set()
    exposed: list = []
    # Type registries for `types:` harvesting — every named struct/enum
    # definition seen across the parsed TUs, keyed by tag.
    record_reg: dict = {}
    enum_reg: dict = {}

    def bucket(name: str) -> dict:
        if name not in classes:
            classes[name] = {
                "name": name, "domain": None, "accessor": None, "type": None,
                "source_file": None, "parent": None, "mixins": [],
                "data": None, "data_fields": [], "ops": [],
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

    # File-bytes cache shared across every source — headers are
    # frequently included by multiple .c files, no need to re-read
    # them per source.
    file_bytes_cache: dict = {}
    for path in sources:
        # Seed the cache so the .c's own annotations resolve regardless
        # of whether clang emits a `loc.file` on its top-level decls
        # (it usually does, but seeding here removes the dependency).
        file_bytes_cache[str(path)] = path.read_bytes()
        # Header-destined content authored in `#ifdef YCLASS_CODEGEN` blocks
        # is copied verbatim into this file's header (before its exposed
        # function prototypes, since types must precede the functions
        # that use them).
        for block in _scan_codegen_blocks(
                file_bytes_cache[str(path)].decode("utf-8", errors="replace")):
            exposed.append({"source_file": str(path), "verbatim": block})
        tu = ast_dump(path, include_dirs)
        # Index every struct/enum definition this TU sees, for `types:`.
        _collect_type_decls(tu, record_reg, enum_reg)
        # The .c being processed is the default "current file" for the
        # top-level translation unit — clang annotates the file change
        # only on the FIRST declaration that moves into a header.
        for decl, decl_file in _walk_decls(tu, current_file=str(path)):
            anns = _collect_annotations(decl, file_bytes_cache, decl_file)
            if not anns: continue
            kind = decl.get("kind")

            # `expose` on a function — emit its prototype into the generated
            # header for the file it is DEFINED in (not one seen via an
            # #include).
            if "expose" in anns and decl_file == str(path) and kind == "FunctionDecl":
                qt = decl.get("type", {}).get("qualType", "")
                exposed.append({
                    "source_file": str(path),
                    "name": decl["name"],
                    "return_type": _parse_return_type(qt),
                    "args": _fn_args(decl),
                })
                continue

            # `expose` on a struct — public type. A definition (has fields)
            # emits the full `struct X { … };`; a bare forward declaration
            # emits `struct X;`. Same single-source model as functions: the
            # type is authored ONCE in the owning .c and reproduced into its
            # header, so there is no `#ifdef YCLASS_CODEGEN` verbatim block to
            # keep in sync. (The owning .c must NOT include its own generated
            # header, or its definition would clash with the emitted copy.)
            if "expose" in anns and decl_file == str(path) and kind == "RecordDecl":
                name = decl.get("name")
                if name:
                    fields = _record_fields(decl, file_bytes_cache, decl_file)
                    if fields:
                        body = "\n".join(f"    {fld['type']} {fld['name']};" for fld in fields)
                        type_text = f"struct {name} {{\n{body}\n}};"
                    else:
                        type_text = f"struct {name};"
                    exposed.append({"source_file": str(path), "type_text": type_text})
                continue

            if kind == "FunctionDecl":
                for ann in anns:
                    if "@" not in ann:
                        continue  # expose/property handled elsewhere
                    role, args = _split_ann(ann)
                    if role == "local":
                        # `local@<DOMAIN>:<SLOT>` — every public stub
                        # built for <SLOT> drops its RPC branch and
                        # the skel table excludes it. The function the
                        # annotation rides on is irrelevant; we just
                        # use it as an anchor for the slot-attribute.
                        require_segments(role, args, 2, "local@<DOMAIN>:<SLOT>")
                        slot_dom, slot_name = args[0], args[1]
                        if slot_dom != module:
                            # Foreign-module slot attribute — that
                            # module's own codegen owns the slot's
                            # locality; ignore here so a header
                            # inclusion can't silently flip a remote
                            # module's wire contract.
                            continue
                        local_slots.add(slot_name)
                        continue
                    if role == "virtual":
                        # `virtual@<DOMAIN>:<CLASS>:<SLOT>` — INTRODUCE a virtual
                        # method. The annotated function is its default impl,
                        # and <CLASS> OWNS the slot (its stub + `_fn` typedef are
                        # emitted into <CLASS>'s header; every subclass reaches
                        # them through the parent header it includes). This is
                        # the only annotation that *creates* a slot — `override@`
                        # merely supplies another class's impl for it.
                        require_segments(role, args, 3, "virtual@<DOMAIN>:<CLASS>:<SLOT>")
                        impl_dom, cls, slot = args
                        if impl_dom != module:
                            continue
                        slot_dom = impl_dom
                        b = bucket(cls)
                        b["ops"].append({
                            "slot": slot,
                            "slot_domain": slot_dom,
                            "impl": decl["name"],
                        })
                        # Authoritative owner. Overwrite any provisional entry an
                        # earlier override@ created so owning_class is the
                        # introducer regardless of source-processing order.
                        qt = decl.get("type", {}).get("qualType", "")
                        methods[slot] = {
                            "slot": slot, "domain": slot_dom,
                            "owning_class": cls,
                            "return_type": _parse_return_type(qt),
                            "args": _fn_args(decl),
                        }
                        continue
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
                        # Skip override impls that belong to a foreign
                        # module — they'd only reach us via a header
                        # inclusion of someone else's source, which
                        # shouldn't happen but is harmless to ignore.
                        if impl_dom != module:
                            continue
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
                    if "@" not in ann:
                        continue  # expose/property handled elsewhere
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
                    # Foreign-module class/mixin annotations reach us
                    # via header inclusion (e.g. ygrid's grid.c pulls
                    # in `<yetty/yfigure/figure.h>` which carries the
                    # `mixin@yfigure:figure` annotation on the struct).
                    # The owning module generates that class's accessor
                    # — we just ignore the annotation here. Same goes
                    # for any parent/uses annotations attached to the
                    # foreign class; they describe the foreign class's
                    # relationships, not ours.
                    if primary_dom != module:
                        continue
                    b = bucket(primary)
                    # Set descriptive fields ONLY on first sighting.
                    # The same annotation reaches every TU that pulls
                    # the defining header in transitively — overwriting
                    # `source_file` each time would point it at the
                    # last-seen including .c, not the file the struct
                    # actually lives in. The first .c we process and
                    # see the annotation through is the closest stand-
                    # in we have for the canonical owner.
                    if not b["source_file"]:
                        b["domain"] = primary_dom
                        b["type"] = kind2
                        b["source_file"] = str(path)
                        suffix = "_mixin_get" if kind2 == "mixin" else "_class_get"
                        b["accessor"] = f"yetty_{primary_dom}_{primary}{suffix}"
                        tag = decl.get("name")
                        if tag:
                            b["data"] = f"struct {tag}"
                            # Member accessors come from `property` annotations
                            # on the struct's fields (only visible where the
                            # struct is defined, i.e. the owning .c).
                            b["data_fields"] = _record_fields(decl, file_bytes_cache, decl_file)
                        for ann in anns:
                            if "@" not in ann:
                                continue  # expose/property handled elsewhere
                            role, args = _split_ann(ann)
                            if role == "parent":
                                require_segments(role, args, 2, "parent@<DOMAIN>:<CLASS>")
                                b["parent"] = {"domain": args[0], "name": args[1]}
                            elif role == "uses":
                                require_segments(role, args, 2, "uses@<DOMAIN>:<MIXIN>")
                                b["mixins"].append({"domain": args[0], "name": args[1]})

    # Every bucket created by an `override` annotation MUST end up
    # with a matching `class@...` or `mixin@...` record by the end of
    # the walk — otherwise the override is silently dropped from
    # registration (no accessor → filtered out of classes[]) while
    # potentially still emitting a public method stub referencing a
    # non-existent owning_class. Fail loudly instead.
    orphans = [name for name, c in classes.items() if not c["accessor"]]
    if orphans:
        sys.stderr.write(
            f"error: override annotations target undeclared class(es) "
            f"{orphans} — every override@<DOMAIN>:<CLASS>:<SLOT> needs a "
            f"matching class@<DOMAIN>:<CLASS> or mixin@<DOMAIN>:<CLASS> "
            f"record on the data struct.\n")
        sys.exit(1)
    # Slots flagged via `local@` are not wire-marshalled — every public
    # stub for them is emitted without an RPC branch and the skel table
    # excludes them. Tag each method here so downstream emitters
    # (emit_dispatch_body, emit_lookup_tables) can read off `m["local"]`.
    for m in methods.values():
        m["local"] = m["slot"] in local_slots
    # A `local@` annotation that names a slot we have no impl for is a
    # programmer mistake — without an impl the slot also produces no
    # public stub, so the annotation is dead weight. Surface it loudly.
    impl_slots = {m["slot"] for m in methods.values()}
    dangling = [s for s in local_slots if s not in impl_slots]
    if dangling:
        sys.stderr.write(
            f"error: local@{module}:<slot> annotations name slots with no "
            f"impl in this module: {dangling}. Each local-marked slot must "
            f"have at least one override@ in the same module.\n")
        sys.exit(1)
    model = {
        "methods": list(methods.values()),
        "classes": [c for c in classes.values() if c["accessor"]],
    }
    # Only carry `exposed` when something used it — keeps an empty
    # `exposed: []` out of every other module's model.yaml.
    if exposed:
        model["exposed"] = exposed
    # Concrete layouts for by-value struct/enum types referenced by the
    # module's signatures (Result family, POD structs, enums) — transitively
    # resolved so the FFI generator is fully model-driven.
    harvested = _harvest_types(model, record_reg, enum_reg)
    if harvested:
        model["types"] = harvested
    return model


# Signature validation for cross-domain (and same-domain) overrides is
# done in *C*, not here. Each public stub gets a per-slot function-
# pointer typedef in methods.h (see emit_methods_h). Each override
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


# Codegen's one blessed value-typed blob arg shape. `struct yetty_ycore_buffer`
# (by value, NOT pointer) is recognised as a (data, size, capacity) carrier;
# the wire encodes it as a u32 size field in the packed scalar header plus
# the bytes appended verbatim after. Any other value-typed struct would be
# memcpy'd as raw bytes — meaningless across processes if it carries
# pointers internally — so we still refuse those at validate_method time.
def is_buffer(t: str) -> bool:
    return bool(re.match(r"^\s*(const\s+)?struct\s+yetty_ycore_buffer\s*$", t))


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


def validate_class(c: dict):
    """A class's data struct tag must be exactly `yetty_<domain>_<class>` —
    the same identity its `class@<domain>:<class>` annotation declares. The
    redundancy is intentional (the tag reads naturally in the .c), but the two
    MUST agree so every generated public name (the `_from` downcast, the
    `_ptr` result, property accessors) derives unambiguously from one identity.
    A short private tag like `button_data` is rejected so the drift can't hide.

    Returns an error string for a non-conforming class, or None when it is
    fine — the caller collects every offender and reports them together rather
    than aborting on the first."""
    data = (c.get("data") or "").strip()
    if not data:
        return None  # marker class with no data slice — nothing to name
    tag = re.sub(r"^struct\s+", "", data).strip()
    expected = f"yetty_{c['domain']}_{c['name']}"
    if tag != expected:
        kind = "mixin" if c.get("type") == "mixin" else "class"
        return (
            f"error: {kind}@{c['domain']}:{c['name']} data struct is named "
            f"'{tag}', but it must be '{expected}' (yetty_<domain>_<class>).\n"
            f"  Rename the struct tag in {c.get('source_file')} so it matches\n"
            f"  the annotation — every generated name derives from that identity.\n")
    return None


def validate_method(m: dict):
    args = m["args"]
    # Enforce the project Result-return contract on every slot impl.
    # Without this, raw returns (void/int/size_t/struct X/struct X *)
    # get silently mapped into Result ids by result_type_id() —
    # generating a public stub typed as `struct <id>_result` while
    # the actual impl returns the raw type, which the compile-time
    # signature check then trips on at compile time. Reject up front
    # so the diagnostic points at the annotated source, not at the
    # generated typedef-assignment line.
    rt = m["return_type"].strip()
    if not re.match(r"^struct\s+\w+_result\s*$", rt):
        sys.stderr.write(
            f"error: method {m['slot']}: return type '{rt}' is not a Result.\n"
            f"  All slot impls must return a `struct <id>_result` declared via\n"
            f"  YETTY_YRESULT_DECLARE. Use `struct yetty_ycore_void_result` for\n"
            f"  methods that have no success value.\n")
        sys.exit(1)
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
    # Local-only slots are never marshalled, so the wire-shape rules
    # below don't apply: they may freely take raw pointers to producer-
    # owned carriers (emit context, draw list, …) that have no wire
    # representation. Skip the pointer/return shape checks entirely.
    if m.get("local"):
        return
    # Pointer args after obj are accepted ONLY when they have the
    # shape `struct X *` — those are wire-marshalled as yclass object
    # handles, same way obj itself is. Every other pointer flavour
    # (char *, void *, int *, struct yetty_ycore_buffer *, …) is
    # rejected: marshalling a raw address would write the producer's
    # local heap pointer onto the wire, meaningless on the peer.
    for a in args[2:]:
        if is_buffer(a["type"]):
            continue  # length-prefixed payload after the packed header
        if is_struct_ptr(a["type"]):
            continue  # extra object-handle arg, treated like obj
        if "*" in a["type"]:
            # `struct yetty_ycore_buffer *` (pointer to buffer) is
            # also rejected — buffer is taken by value so the wire
            # layer owns the marshalling.
            sys.stderr.write(
                f"error: method {m['slot']}: arg '{a['name']}' has pointer "
                f"type ({a['type']}). Only `struct X *` (yclass object handle), "
                f"`struct yetty_ycore_buffer` (by value), and scalars cross the "
                f"wire. Other pointer types have no meaning on the peer.\n")
            sys.exit(1)
    # Same reasoning for return values — a `_ptr` Result id would
    # memcpy the server's pointer bytes into the wire response and
    # the client would wrap that meaningless address in YETTY_OK.
    # Reject. Return a value or a yclass object handle (uint64_t)
    # instead.
    rid = result_type_id(m["return_type"])
    if rid.endswith("_ptr"):
        sys.stderr.write(
            f"error: method {m['slot']}: return type '{m['return_type']}' "
            f"maps to Result id '{rid}' (pointer payload). Pointer returns "
            f"can't cross process / RPC boundaries. Return a value or a "
            f"yclass object handle (uint64_t).\n")
        sys.exit(1)
    # `struct yetty_ycore_buffer` as a Result payload would silently
    # memcpy (data ptr + size + capacity) onto the wire — meaningless on
    # the peer. Buffer is an INPUT shape only; if a slot needs to return
    # bytes, redesign as either (a) a yclass object handle to a buffer-
    # owning class, or (b) a follow-up call from peer back to producer.
    if rid == "yetty_ycore_buffer":
        sys.stderr.write(
            f"error: method {m['slot']}: return type '{m['return_type']}' "
            f"is a buffer — wire-marshalling a buffer back to the caller "
            f"is not supported. Buffer is an input-only shape.\n")
        sys.exit(1)


def wire_args(m: dict) -> list:
    """All args except ctx."""
    return m["args"][1:]


def args_struct_body(args: list, indent: str) -> str:
    if not args:
        return f"{indent}char _empty;\n"
    return "".join(f"{indent}{wire_type(a['type'])} {wire_name(a)};\n" for a in args)


# ---------------- methods.h (public) ------------------------------------

def emit_methods_h(model: dict, module: str, out_path: Path):
    """Public method-declaration header. Lives at
    include/yetty/<module>/methods.h (clean name, no .gen.h — it's a
    public-API header, callers don't care that codegen produced it).
    Per-class public headers in the same directory `#include
    "methods.h"` (sibling) so a caller that includes a class header
    gets the module's full method API.

    Contains ONLY public function declarations — the actual stubs
    users call (`yetty_yfigure_add_child(ctx, obj, …)`). The
    `_fn` impl-side typedefs are an internal codegen detail used
    only by the .gen.c override registrations, so they live in
    src/yetty/<module>/methods.gen.h (see emit_methods_gen_h).

    Pulls the module-owned <module>/types.h ahead of any stub decl so
    YETTY_YRESULT_DECLARE for module-specific return types is in scope
    when the generated `struct <id>_result` references it. Modules
    that have no custom result types still get an (empty / guard-only)
    types.h scaffolded by main()."""
    guard = f"YETTY_YCLASSGEN_{module.upper()}_METHODS_H"
    parts = [HEADER]
    parts.append(f"#ifndef {guard}\n#define {guard}\n\n")
    parts.append('#include <yetty/yclass/class.h>\n')
    # ycore/types.h carries the buffer struct codegen recognises as a
    # blob arg. Public-stub signatures use it by value, so the full
    # definition must be in scope — a forward-decl isn't enough.
    parts.append('#include <yetty/ycore/types.h>\n')
    parts.append(f'#include "yetty/{module}/types.h"\n\n')

    structs = set()
    for m in model["methods"]:
        structs |= struct_names_in(m["return_type"])
        for a in m["args"]:
            structs |= struct_names_in(a["type"])
    # Already declared by the includes above — don't double-up with a
    # redundant forward-decl.
    for known in ("yetty_yclass_ctx", "yetty_yclass_object", "yetty_yclass",
                  "yetty_ycore_buffer"):
        structs.discard(known)
    for s in sorted(structs):
        parts.append(f"struct {s};\n")
    parts.append("\n")

    for m in model["methods"]:
        params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
        rt = result_type(m["return_type"])
        # Public-stub declaration only. The matching `<slot>_fn`
        # typedef is internal and lives in the sibling methods.gen.h
        # under src/yetty/<module>/.
        parts.append(f"{rt} {qualified_slot(m)}({params});\n")

    parts.append("\n#endif\n")
    out_path.write_text("".join(parts))


# ---------------- methods.gen.h (internal) ------------------------------

def emit_methods_gen_h(model: dict, module: str, out_path: Path):
    """Internal companion header. Lives at src/yetty/<module>/methods.gen.h
    — keeps the `.gen.h` suffix because it's NOT part of the public API
    surface; the `_fn` impl typedefs are useful only inside the module
    (.gen.c files use them to type-check the impl pointer when registering
    it on the ops vtable, and methods.gen.c casts the dispatch target to
    them).

    SELF-CONTAINED ON PURPOSE — it must NOT `#include` the per-class
    public headers (`<class>.h`). This file is pulled into every
    hand-written `<stem>.c`'s translation unit (the `<stem>.gen.c`
    appended at the foot of the .c includes it). A migrated `<stem>.c`
    declares its OWN `YETTY_YRESULT_DECLARE(<class>_ptr, …)`; the per-class
    public header owns the matching definition, so importing that header
    here would land a SECOND definition of `struct <class>_ptr_result` in
    the same TU — a redefinition error. Instead we bring in only the
    foundational types, forward-declare every struct named in a method
    signature (pointers and result wrappers alike — a forward decl
    coexists with the .c's later full definition), and emit the public
    stub prototypes ourselves from the model. The ops tables only take a
    stub's address as a method-id sentinel, so an incomplete result type
    in those prototypes is fine; methods.gen.c, which actually defines the
    stubs, pulls the per-class headers directly for the complete types."""
    guard = f"YETTY_YCLASSGEN_{module.upper()}_METHODS_GEN_H"
    parts = [HEADER]
    parts.append(f"#ifndef {guard}\n#define {guard}\n\n")
    parts.append('#include <yetty/yclass/class.h>\n')
    # ycore/types.h carries the buffer struct codegen recognises as a blob
    # arg; public-stub signatures use it by value, so its full definition
    # must be in scope (a forward-decl isn't enough). No module-local
    # types.h here: it is not always scaffolded, and the prototypes only
    # need (incomplete) forward-decls of their result wrappers, emitted
    # below.
    parts.append('#include <yetty/ycore/result.h>\n')
    parts.append('#include <yetty/ycore/types.h>\n\n')

    structs = set()
    for m in model["methods"]:
        structs |= struct_names_in(m["return_type"])
        # The wrapped result struct (e.g. yetty_yfigure_figure_ptr_result)
        # is named indirectly via result_type_id — add it explicitly so the
        # prototypes reference a declared (if incomplete) tag.
        structs.add(f"{result_type_id(m['return_type'])}_result")
        for a in m["args"]:
            structs |= struct_names_in(a["type"])
    # Already pulled in fully by the includes above — don't double up with a
    # redundant forward-decl (harmless, but noisy).
    for known in ("yetty_yclass_ctx", "yetty_yclass_object", "yetty_yclass",
                  "yetty_ycore_buffer"):
        structs.discard(known)
    for s in sorted(structs):
        parts.append(f"struct {s};\n")
    parts.append("\n")

    # Public stub prototypes — the per-class .gen.c ops tables reference
    # these by name as method-id sentinels.
    for m in model["methods"]:
        params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
        rt = result_type(m["return_type"])
        parts.append(f"{rt} {qualified_slot(m)}({params});\n")
    parts.append("\n")

    for m in model["methods"]:
        type_only = ", ".join(a["type"] for a in m["args"])
        rt = result_type(m["return_type"])
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


def emit_methods_c(model: dict, module: str, out_path: Path):
    # This TU DEFINES every public slot stub and casts the resolved impl to
    # the slot's `<slot>_fn`. It is a STANDALONE .gen.c — never `#include`d
    # into a hand-written .c — so it can pull in every per-class header
    # without the "redefine an expose'd struct" hazard that the *appended*
    # <stem>.gen.c has. And it needs them: the `<slot>_fn` typedefs it casts
    # to, plus the COMPLETE result types its stubs return by value (some are
    # foreign, e.g. a ydraw result), live in (or are reachable through) those
    # headers. There is no module-wide methods header anymore.
    parts = [HEADER]
    seen_headers = []
    for c in model.get("classes", []):
        h = class_header_for(c, module)
        if h not in seen_headers:
            seen_headers.append(h)
    for h in seen_headers:
        parts.append(f'#include "{h}"\n')
    parts += ['#include <yetty/yclass/rpc.h>\n',
              '#include <yetty/ycore/result.h>\n',
              '#include <yetty/ycore/types.h>  /* container_of */\n',
              '#include <yetty/ytrace/ytrace.h>\n',
              '#include <stdint.h>\n'
              '#include <stdlib.h>  /* malloc/free for buffer-arg marshalling */\n'
              '#include <string.h>\n\n']
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
    # variable of the slot's function-pointer typedef forces the compiler
    # to verify return + parameter types. The `<slot>_fn` typedefs these
    # use are emitted at the head of this same .gen.c (emit_class_gen_c) for
    # same-module slots, and come from an included parent/foreign header for
    # cross-module overrides — so the check needs no module-wide header.
    #
    # The check variable name MUST include slot_domain — a class that
    # overrides the same local slot name from two different domains would
    # otherwise collide on the static variable name.
    qcls = qualified_class(cls)
    typecheck_lines = [
        f"[[maybe_unused]]\n"
        f"static {op_c_name(op)}_fn {qcls}_{op_c_name(op)}_check = {op['impl']};"
        for op in cls["ops"]
    ]
    typecheck_block = "\n".join(typecheck_lines)
    if typecheck_block:
        typecheck_block += "\n\n"

    # Each op references its slot by (slot_domain, local_name). The
    # domain string is the YETTY-prefixed module name — one canonical
    # convention shared by:
    #   - slot table registration (yetty_yclass_method_slot_register)
    #   - dispatch lookup        (yetty_yclass_method_slot_get)
    #   - wire qname             (qname = "yetty_<module>_<localname>")
    #   - skel rows              (name-keyed lookup in <module>_skel_lookup)
    # Mixing prefixed and unprefixed forms silently breaks server-side
    # CALL dispatch (method_slot_name returns one form, skel_rows
    # contains the other) — so every site uses the yetty_-prefixed
    # form and string compares match.
    op_lines = [
        f'        {{"yetty_{op["slot_domain"]}", "{op["slot"]}", '
        f"(yetty_yclass_method_id_t){op_c_name(op)}, "
        f"(yetty_yclass_impl_t){op['impl']}}},"
        for op in cls["ops"]
    ]
    ops_block = "\n".join(op_lines)
    # A class may legitimately add zero ops (e.g. a marker subclass that
    # only exists so callers can name it in yetty_ygui_add). C rejects an
    # empty array initializer, so emit no `ops[]` and pass NULL/0.
    if cls["ops"]:
        ops_decl = ("    static const struct yetty_yclass_op ops[] = {\n"
                    + ops_block + "\n    };\n")
        ops_args = "ops, sizeof(ops) / sizeof(ops[0])"
    else:
        ops_decl = ""
        ops_args = "NULL, 0"

    qname = qualified_class(cls)

    parent = cls.get("parent")
    if parent:
        parent_accessor = f"yetty_{parent['domain']}_{parent['name']}_class_get"
        parent_block = (
            f"    struct yetty_yclass_ptr_result parent_class_r = {parent_accessor}();\n"
            f"    if (YETTY_IS_ERR(parent_class_r)) {{\n"
            f"        yerror(\"{qname}_class_get: parent accessor failed: %s\", "
            f"parent_class_r.error.msg);\n"
            f"        return YETTY_ERR(yetty_yclass_ptr, "
            f"\"{qname}_class_get: parent accessor failed\", parent_class_r);\n"
            f"    }}\n"
        )
        parent_expr = "parent_class_r.value"
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
                f"    struct yetty_yclass_ptr_result mixin_class_r_{i} = {mixin_accessor}();\n"
                f"    if (YETTY_IS_ERR(mixin_class_r_{i})) {{\n"
                f"        yerror(\"{qname}_class_get: mixin{i} accessor failed: %s\", "
                f"mixin_class_r_{i}.error.msg);\n"
                f"        return YETTY_ERR(yetty_yclass_ptr, "
                f"\"{qname}_class_get: mixin{i} accessor failed\", mixin_class_r_{i});\n"
                f"    }}\n"
            )
            mixin_values.append(f"mixin_class_r_{i}.value")
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

    # `property`-annotated members → standardized data handle + per-member
    # getters/setters, all keyed on the yclass object. The data struct stays
    # private to the owning .c; every other class reaches members only through
    # these. Built on the core yclass slice model (yetty_yclass_object_data),
    # so this works for any class whose body is a yclass data slice.
    property_block = ""
    property_fields = [f for f in cls.get("data_fields", []) if f.get("get") or f.get("set")]
    # Every class or mixin with a data slice gets the obj->typed-slice
    # accessor `<class>_from` (the downcast built on yetty_yclass_object_data
    # — it returns a Result because the object may not carry this slice).
    # Mixins are downcastable the same way: their slice is resolved from the
    # object's class chain. Per-field getters/setters are added only for
    # `property`-annotated members.
    if cls.get("data"):
        data_ptr_rid = f"{qcls}_ptr"
        parts = [
            f"\nstruct {data_ptr_rid}_result {qcls}_from(struct yetty_yclass_object *obj)\n"
            f"{{\n"
            f"    struct yetty_yclass_ptr_result class_r = {accessor}();\n"
            f"    if (YETTY_IS_ERR(class_r))\n"
            f'        return YETTY_ERR({data_ptr_rid}, "{qcls}_from: class accessor", class_r);\n'
            f"    struct yetty_yclass_void_ptr_result slice_r =\n"
            f"        yetty_yclass_object_data(obj, class_r.value);\n"
            f"    if (YETTY_IS_ERR(slice_r))\n"
            f'        return YETTY_ERR({data_ptr_rid}, "{qcls}_from: object_data", slice_r);\n'
            f"    return YETTY_OK({data_ptr_rid}, ({data} *)slice_r.value);\n"
            f"}}\n"
        ]
        # Inverse of `_from`: recover the owning yclass object from a pointer
        # to this class's data slice. Only meaningful for regular classes —
        # their slice offset is invariant across leaves under the root-down
        # layout (the offset depends only on ancestors). A mixin's slice
        # offset varies per using class, so there is no single offset to
        # subtract; `_to` would be ill-defined and is not emitted.
        if cls.get("type") != "mixin":
            parts.append(
                f"\nstruct yetty_yclass_object *{qcls}_to({data} *data)\n"
                f"{{\n"
                f"    if (!data)\n"
                f"        return NULL;\n"
                f"    struct yetty_yclass_ptr_result class_r = {accessor}();\n"
                f"    if (YETTY_IS_ERR(class_r)) {{\n"
                f"        yetty_ycore_error_destroy(class_r.error);\n"
                f"        return NULL;\n"
                f"    }}\n"
                f"    struct yetty_ycore_size_result offset_r =\n"
                f"        yetty_yclass_object_data_offset(class_r.value, class_r.value);\n"
                f"    if (YETTY_IS_ERR(offset_r)) {{\n"
                f"        yetty_ycore_error_destroy(offset_r.error);\n"
                f"        return NULL;\n"
                f"    }}\n"
                f"    return (struct yetty_yclass_object *)((char *)data - offset_r.value);\n"
                f"}}\n"
            )
        for field in property_fields:
            fname = field["name"]
            ftype = field["type"]
            base = f"{qcls}_{fname}"
            field_rid = result_type_id(ftype)
            if field.get("get"):
                parts.append(
                    f"\n{result_type(ftype)} {base}_get(struct yetty_yclass_object *obj)\n"
                    f"{{\n"
                    f"    struct {data_ptr_rid}_result data = {qcls}_from(obj);\n"
                    f"    if (YETTY_IS_ERR(data))\n"
                    f'        return YETTY_ERR({field_rid}, "{base}_get: data block", data);\n'
                    f"    return YETTY_OK({field_rid}, data.value->{fname});\n"
                    f"}}\n"
                )
            if field.get("set"):
                parts.append(
                    f"\nstruct yetty_ycore_void_result {base}_set(struct yetty_yclass_object *obj, "
                    f"{ftype} value)\n"
                    f"{{\n"
                    f"    struct {data_ptr_rid}_result data = {qcls}_from(obj);\n"
                    f"    if (YETTY_IS_ERR(data))\n"
                    f'        return YETTY_ERR(yetty_ycore_void, "{base}_set: data block", data);\n'
                    f"    data.value->{fname} = value;\n"
                    f"    return YETTY_OK_VOID();\n"
                    f"}}\n"
                )
        property_block = "".join(parts)

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
        .data_align = _Alignof({data}),
    }};
{ops_decl}\
{parent_block}\
{mixin_block}\
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, {ops_args},
                              {parent_expr}, {mixin_arg}, {mixin_count});
    if (YETTY_IS_ERR(register_class_r)) {{
        yerror("{qname}_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "{qname}_class_get: class_register failed", register_class_r);
    }}
    cls = register_class_r.value;
    return register_class_r;
}}
{property_block}"""




def emit_class_public_headers(model: dict, module: str, include_module_dir: Path):
    """One generated public header per class. Output path mirrors the
    source's subdir structure under `include/yetty/<module>/` so a
    widget at `src/yetty/<module>/widgets/foo.c` lands at
    `include/yetty/<module>/widgets/foo.h` (not `.gen.h` — the file
    IS the public interface; consumers don't care that it's
    generated).

    The header is 100% generated — there is no hand-written section.
    Function APIs come from `expose` annotations; any other header-
    destined content (types, typedefs, enums, vtable structs,
    result-decls, forward-decls, includes) is authored in the source's
    `#ifdef YCLASS_CODEGEN` blocks and copied verbatim here."""
    # Group classes by source file: each output header is named after
    # the source's stem (so `widgets/button.c` → `widgets/button.h`,
    # `primitive-widget.c` → `primitive-widget.h` — preserving the
    # exact file naming the rest of the tree #include's already).
    groups: dict = {}
    for cls in model.get("classes", []):
        groups.setdefault(cls["source_file"], []).append(cls)
    for src_file, classes in groups.items():
        src_path = Path(src_file).resolve()
        stem = src_path.stem
        # Mirror the source's subdir under include/yetty/<module>/.
        anchor_parts = ("src", "yetty", module)
        parts = src_path.parts
        rel_subdir = Path(".")
        for i in range(len(parts) - len(anchor_parts) + 1):
            if parts[i:i + len(anchor_parts)] == anchor_parts:
                rel_subdir = Path(*parts[i + len(anchor_parts):]).parent
                break
        out_dir = include_module_dir / rel_subdir
        out_dir.mkdir(parents=True, exist_ok=True)
        header_path = out_dir / f"{stem}.h"
        # Distinct guard per file path under the module — prevents
        # collisions between widgets/button.h and any other button.h
        # that might exist in the tree.
        guard_path = "_".join(rel_subdir.parts + (stem,)) if rel_subdir != Path(".") else stem
        guard = (f"YETTY_YCLASSGEN_{module.upper()}_"
                 f"{guard_path.upper().replace('-', '_')}_H")
        decls = "\n".join(
            f"struct yetty_yclass_ptr_result {qualified_class(c)}"
            f"{'_mixin_get' if c['type'] == 'mixin' else '_class_get'}"
            f"(void);"
            for c in classes
        )
        # `property`-annotated members → opaque data-block handle + per-member
        # getters/setters, keyed on the yclass object. The struct body never
        # leaves its owning .c; only a forward decl + the accessors cross here.
        data_decls = ""
        property_decls = ""
        prop_blocks = []
        for c in classes:
            if not c.get("data"):
                continue
            pfields = [f for f in c.get("data_fields", []) if f.get("get") or f.get("set")]
            q = qualified_class(c)
            rid = f"{q}_ptr"
            lines = [
                "/* Data-block handle — opaque outside the owning .c. The struct\n"
                " * stays private; only its pointer crosses here, in a Result so a\n"
                " * bad object surfaces rather than corrupting. Reach members\n"
                " * through the per-property getters/setters below. */",
                f"{c['data']};",
                f"YETTY_YRESULT_DECLARE({rid}, {c['data']} *);",
                f"struct {rid}_result {q}_from(struct yetty_yclass_object *obj);",
            ]
            # Inverse accessor — recover the owning object from a data-slice
            # pointer. Regular classes only (a mixin slice has no invariant
            # offset); see the matching guard in emit_class_accessor.
            if c.get("type") != "mixin":
                lines.append(
                    f"struct yetty_yclass_object *{q}_to({c['data']} *data);")
            for field in pfields:
                base = f"{q}_{field['name']}"
                if field.get("get"):
                    lines.append(
                        f"{result_type(field['type'])} {base}_get(struct yetty_yclass_object *obj);")
                if field.get("set"):
                    lines.append(
                        f"struct yetty_ycore_void_result {base}_set("
                        f"struct yetty_yclass_object *obj, {field['type']} value);")
            prop_blocks.append("\n".join(lines))
        if prop_blocks:
            property_decls = "\n\n" + "\n\n".join(prop_blocks)
        # `expose`d functions defined in this source file — concrete,
        # non-slot public API. Emit their prototypes here so they live in
        # the generated section instead of the hand-maintained MANUAL block.
        exposed_protos = []
        exposed_type_names = set()
        proto_struct_types = set()
        for e in model.get("exposed", []):
            if e["source_file"] != src_file:
                continue
            if "verbatim" in e:
                exposed_protos.append(e["verbatim"])
            elif "type_text" in e:
                exposed_protos.append(e["type_text"])
                for m in re.finditer(r"struct\s+(\w+)", e["type_text"]):
                    exposed_type_names.add(m.group(1))
            else:
                exposed_protos.append(_fmt_proto(e["return_type"], e["name"], e["args"]))
                for typ in [e["return_type"]] + [a["type"] for a in e["args"]]:
                    for m in re.finditer(r"struct\s+(\w+)", typ):
                        proto_struct_types.add(m.group(1))
        # Forward declarations for the struct types named in the exposed
        # prototypes. A forward declaration is sufficient even for by-value
        # parameters here — completeness is only required at the call site and
        # the definition, both of which include the full type. This is what
        # lets the public types be authored once (in the owning .c, with the
        # `expose` annotation) and reproduced into the header with no `#ifdef
        # YCLASS_CODEGEN` verbatim block. Skip result structs and yclass core
        # types (already provided by the always-included class.h) and anything
        # this header fully defines itself (an exposed type like a hit struct).
        proto_struct_types -= exposed_type_names
        fwd_decls = "".join(
            f"struct {n};\n" for n in sorted(proto_struct_types)
            if not n.endswith("_result") and not n.startswith("yetty_yclass"))
        method_decls = ""
        if fwd_decls:
            method_decls += "\n\n" + fwd_decls.rstrip()
        if exposed_protos:
            method_decls += "\n\n" + "\n".join(exposed_protos)

        # Public method-dispatch stubs whose owning class is defined in THIS
        # source. Folded in so the source's single header is its complete
        # public interface — there is no module-wide methods.h.
        group_class_names = {c["name"] for c in classes}
        group_methods = [m for m in model["methods"]
                         if m.get("owning_class") in group_class_names]
        stub_struct_names = set()
        for m in group_methods:
            stub_struct_names |= struct_names_in(m["return_type"])
            for a in m["args"]:
                stub_struct_names |= struct_names_in(a["type"])
        for known in ("yetty_yclass_ctx", "yetty_yclass_object", "yetty_yclass",
                      "yetty_ycore_buffer"):
            stub_struct_names.discard(known)
        stub_struct_names -= exposed_type_names
        stub_fwd = "".join(f"struct {n};\n" for n in sorted(stub_struct_names))
        stub_decls = "".join(
            f"{result_type(m['return_type'])} {qualified_slot(m)}("
            + ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
            + ");\n"
            for m in group_methods)
        # Slot function-pointer typedefs — a subclass that overrides one of
        # these slots includes this header and needs `<slot>_fn` to type-check
        # its impl. Public, alongside the stubs (replaces the old methods.gen.h).
        stub_typedefs = "".join(
            f"typedef {result_type(m['return_type'])} (*{qualified_slot(m)}_fn)("
            + ", ".join(a["type"] for a in m["args"])
            + ");\n"
            for m in group_methods)
        # create() for each concrete (non-mixin) class in this source, plus
        # the module's RPC-discovery installer. register() is module-level;
        # an identical prototype in every source header is legal C and keeps
        # each header self-sufficient (no module-wide rpc.h).
        create_decls = "".join(
            f"struct yetty_yclass_object_ptr_result {qualified_class(c)}_create("
            f"struct yetty_yclass_ctx *ctx);\n"
            for c in classes if c.get("type") != "mixin")
        register_decl = (f"struct yetty_ycore_void_result "
                         f"yetty_{module}_register(void);\n")
        rpc_decls = ""
        if stub_fwd:
            rpc_decls += "\n\n" + stub_fwd.rstrip()
        if stub_decls:
            rpc_decls += "\n\n" + stub_decls.rstrip()
        if stub_typedefs:
            rpc_decls += "\n\n" + stub_typedefs.rstrip()
        if create_decls:
            rpc_decls += "\n\n" + create_decls.rstrip()
        rpc_decls += "\n\n" + register_decl.rstrip()

        kind = "mixin" if all(c['type'] == "mixin" for c in classes) else "regular class"
        # Class-name list for the file header banner.
        name_list = ", ".join(c["name"] for c in classes)
        body = (
            HEADER
            + f"/* Public interface for {kind}(es) `{name_list}` "
            + f"(module: {module}).\n"
            + " * Fully generated from the source .c — do not edit. This single\n"
            + " * header is the source's complete public interface: class\n"
            + " * accessors, method stubs, create()/register(), and any\n"
            + " * `expose`d API. Public types come from `expose` annotations. */\n"
            + f"#ifndef {guard}\n#define {guard}\n\n"
            + '#include <yetty/yclass/class.h>\n'
            + '#include <yetty/yclass/rpc.h>\n'
            + '#include <yetty/ycore/result.h>\n'
            + '#include <yetty/ycore/types.h>\n\n'
            + decls + data_decls + property_decls + rpc_decls + method_decls + "\n\n"
            + "#endif\n"
        )
        header_path.write_text(body)

    # An `expose`d function only reaches a header through a source file
    # that also declares at least one class (that's what produces a
    # header). A file with `expose` but no class@/mixin@ would silently
    # drop the prototype — fail loudly instead.
    covered = set(groups.keys())
    uncovered = sorted({e["source_file"] for e in model.get("exposed", [])
                        if e["source_file"] not in covered})
    if uncovered:
        sys.stderr.write(
            f"error: 'expose' used in file(s) with no class@/mixin@, so no "
            f"header is generated for them: {uncovered}. Add a class to the "
            f"file or drop the expose annotation.\n")
        sys.exit(1)


def emit_class_gen_c(model: dict, module: str, module_dir: Path):
    """One `<class>.gen.c` per annotated source. `#include`d at the foot
    of the matching hand-written `<class>.c` — so it must sit in the
    SAME directory as that .c, not at the module root. A widget at
    `src/yetty/ygui/widgets/panel.c` therefore gets its `panel.gen.c`
    next to it at `src/yetty/ygui/widgets/panel.gen.c`; a top-level
    module source at `src/yetty/ygui/widget.c` gets `widget.gen.c`
    right alongside.

    The generator pulls every header the accessor body needs (the
    class's own .h for the prototype, any parent and mixin headers
    for their accessor calls) so the hand-written .c stays minimal —
    typically only `#include <yetty/yclass/class.h>`."""
    groups: dict = {}
    for c in model["classes"]:
        groups.setdefault(c["source_file"], []).append(c)
    for src_path, classes in groups.items():
        # Place the .gen.c in the SAME directory as its source.
        src_path_p = Path(src_path)
        inc_path = src_path_p.with_name(src_path_p.stem + ".gen.c")

        # Collect headers needed for this .gen.c: the (possibly cross-domain)
        # parent + mixin headers, which carry the parent/foreign slot stubs +
        # `_fn` typedefs an override references. Same-module slot stubs +
        # typedefs are emitted locally below (slot_block) — there is no
        # module-wide methods header.
        needed = set()
        for c in classes:
            # NOT the class's own header: this .gen.c is `#include`d at the
            # foot of the hand-written .c, which already provides the class's
            # struct + impls above the include (and its own public types). Re-
            # including the self-header here would re-introduce any `expose`d
            # type the .c defines (e.g. a hit struct), redefining it.
            p = c.get("parent")
            if p:
                # Parent path can't be derived from this class's
                # source — use the foreign module's <name>.h directly.
                # Cross-module parents land under `yetty/<dom>/<name>.h`;
                # within-module parents likewise — codegen owns those
                # files too.
                needed.add(_class_header_lookup(model, p, fallback_dom=p['domain']))
            for mx in c.get("mixins", []):
                needed.add(_class_header_lookup(model, mx, fallback_dom=mx['domain']))
        include_block = "".join(f'#include "{h}"\n' for h in sorted(needed))
        # The accessor body emits ydebug() and YETTY_OK / YETTY_ERR
        # which expand to ycore + ytrace primitives. Pull those in
        # explicitly so a per-class .gen.c is self-sufficient when
        # included from a hand-written .c that hasn't imported them
        # yet (the hand-written .c may include only its module-local
        # header for backward compat).
        include_block += (
            '#include <yetty/ycore/result.h>\n'
            '#include <yetty/ytrace/ytrace.h>\n'
        )

        # Same-module slot stubs this .gen.c's ops reference, emitted locally:
        # the bare stub prototype (the ops-table method-id sentinel) + the
        # `<slot>_fn` typedef (the impl-pointer signature check). The old
        # module-wide methods.gen.h carried these; now each .gen.c declares
        # only what it overrides. Function + typedef declarations are
        # duplicate-safe, so re-declaring what the public <stem>.h also
        # publishes is fine. Cross-module slots come from the parent/mixin
        # headers included above.
        method_by_slot = {(m["domain"], m["slot"]): m for m in model.get("methods", [])}
        slot_keys = []
        for c in classes:
            for op in c.get("ops", []):
                key = (op["slot_domain"], op["slot"])
                if op["slot_domain"] == module and key not in slot_keys:
                    slot_keys.append(key)
        proto_structs = set()
        proto_lines = []
        typedef_lines = []
        for key in slot_keys:
            m = method_by_slot.get(key)
            if not m:
                continue
            proto_structs |= struct_names_in(m["return_type"])
            proto_structs.add(f"{result_type_id(m['return_type'])}_result")
            for a in m["args"]:
                proto_structs |= struct_names_in(a["type"])
            params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
            type_only = ", ".join(a["type"] for a in m["args"])
            rt = result_type(m["return_type"])
            proto_lines.append(f"{rt} {qualified_slot(m)}({params});")
            typedef_lines.append(f"typedef {rt} (*{qualified_slot(m)}_fn)({type_only});")
        for known in ("yetty_yclass_ctx", "yetty_yclass_object", "yetty_yclass",
                      "yetty_ycore_buffer"):
            proto_structs.discard(known)
        slot_block = ""
        if proto_lines:
            slot_block = (
                "".join(f"struct {s};\n" for s in sorted(proto_structs))
                + "".join(line + "\n" for line in proto_lines)
                + "".join(line + "\n" for line in typedef_lines)
                + "\n"
            )

        body = HEADER + include_block + "\n" + slot_block \
             + "\n".join(emit_class_accessor(c) for c in classes)
        inc_path.write_text(body)


# ---------------- rpc.gen.{h,c} ------------------------------------------

def regular_classes(model: dict) -> list:
    return [c for c in model.get("classes", []) if c.get("type") == "regular"]


def _class_header_lookup(model: dict, ref: dict, fallback_dom: str) -> str:
    """Resolve a `{domain, name}` parent/mixin reference to its
    include-path. For same-module refs we use the model to find the
    source's subdir; for cross-module refs we fall back to the bare
    `yetty/<dom>/<name>.h` path (the foreign module's codegen owns
    that file's actual layout)."""
    dom = ref.get("domain", fallback_dom)
    name = ref["name"]
    for c in model.get("classes", []):
        if c["domain"] == dom and c["name"] == name:
            return class_header_for(c, dom)
    return f"yetty/{dom}/{name}.h"


def class_header_for(cls: dict, module: str) -> str:
    """Public header path for a class. The file is named after the
    annotated source's stem (so the public-include path matches
    whatever name the source already uses — widgets/button.c →
    yetty/<module>/widgets/button.h, primitive-widget.c →
    yetty/<module>/primitive-widget.h). Codegen owns the file; hand-
    written helper declarations are preserved across regenerations
    via the MANUAL markers in emit_class_public_headers."""
    anchor = ("src", "yetty", module)
    src_path = Path(cls["source_file"]).resolve()
    parts = src_path.parts
    rel_subdir = ""
    for i in range(len(parts) - len(anchor) + 1):
        if parts[i:i + len(anchor)] == anchor:
            tail = Path(*parts[i + len(anchor):]).parent
            rel_subdir = str(tail) + "/" if str(tail) != "." else ""
            break
    return f"yetty/{module}/{rel_subdir}{src_path.stem}.h"


def emit_skel(m: dict) -> str:
    """Unpack the wire body, resolve obj handle, re-enter the public stub
    with a local ctx so the right override fires on the actual class.
    The wire response carries a status byte (0=OK, 1=ERR); for value
    slots the OK payload is the raw value bytes that follow.

    The skel itself returns size_t per the rpc_skel_fn contract
    (the RPC engine calls it as a fn-pointer); Result-shaped failures
    surfaced from handle_resolve / the user impl are encoded as the
    1-byte status=1 wire response and the skel returns 1."""
    slot = qualified_slot(m)
    rid = result_type_id(m["return_type"])
    vt = ret_value_type(rid)
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
    # Local-only slots have no skel either (no wire surface), so filter
    # them out before building the row table.
    wire_methods = [m for m in methods if not m.get("local")]
    if not wire_methods:
        skel_install = ""
        skel_section = ""
    else:
        skel_rows = ",\n".join(
            f'    {{"{qualified_slot(m)}", {qualified_slot(m)}_skel}}'
            for m in wire_methods
        )
        skel_section = f"""\

/* ---- {module}: slot → skel, name-keyed static data --------------- */

struct yetty_{module}_skel_row {{ const char *name; yetty_yclass_rpc_skel_fn fn; }};

static const struct yetty_{module}_skel_row yetty_{module}_skel_rows[] = {{
{skel_rows}
}};

/* Signature is dictated by the skel-lookup hook contract (registered as a
 * fn-pointer via yetty_yclass_rpc_add_skel_lookup); a slot-name lookup
 * failure is absorbed into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_{module}_skel_lookup(yetty_yclass_method_slot slot)
{{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {{ yetty_ycore_error_destroy(slot_name_r.error); return NULL; }}
    const char *name = slot_name_r.value;
    for (size_t i = 0;
         i < sizeof(yetty_{module}_skel_rows) / sizeof(yetty_{module}_skel_rows[0]); ++i)
        if (strcmp(yetty_{module}_skel_rows[i].name, name) == 0)
            return yetty_{module}_skel_rows[i].fn;
    return NULL;
}}
"""
        skel_install = (
            f"    {{\n"
            f"        struct yetty_ycore_void_result add_skel_r =\n"
            f"            yetty_yclass_rpc_add_skel_lookup(yetty_{module}_skel_lookup);\n"
            f"        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,\n"
            f'                            "yetty_{module}_register: rpc_add_skel_lookup");\n'
            f"    }}\n"
        )

    return f"""\
{accessor_section}\
{skel_section}\

/* ---- {module}: explicit yclass-RPC hook registration ------------- */

/* Installs this module's server-side discovery hooks: the accessor
 * lookup feeds yetty_yclass_by_name()'s registry-miss path, and (when
 * the module exposes wire methods) the skel lookup feeds RPC skeleton
 * dispatch. Call once when the yclass RPC / remote-object server is
 * brought up — idempotent, so repeated calls (several hosts, re-init)
 * are no-ops. This replaces the former load-time installer: a module
 * merely being linked no longer mutates global state before main(),
 * and there is no abort() path on a constructor. */
struct yetty_ycore_void_result yetty_{module}_register(void)
{{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_{module}_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_{module}_register: add_accessor_lookup");
{skel_install}\
    registered = true;
    return YETTY_OK_VOID();
}}
"""


def emit_rpc_h(model: dict, module: str, out_path: Path):
    guard = f"YETTY_YCLASSGEN_{module.upper()}_RPC_H"
    decls = "\n".join(
        f"struct yetty_yclass_object_ptr_result "
        f"{qualified_class(c)}_create(struct yetty_yclass_ctx *ctx);"
        for c in regular_classes(model)
    )
    out_path.write_text(
        HEADER
        + f"#ifndef {guard}\n#define {guard}\n\n"
        + '#include <yetty/yclass/rpc.h>\n'
        + '#include <yetty/ycore/result.h>\n\n'
        + (decls + "\n\n" if decls else "")
        + "/* Installs this module's yclass-RPC server-side discovery hooks\n"
          " * (accessor lookup feeding yetty_yclass_by_name; skel lookup\n"
          " * feeding RPC dispatch when the module exposes wire methods).\n"
          " * Call once when the yclass RPC / remote-object server is brought\n"
          " * up; idempotent, so repeated calls are no-ops. Replaces the\n"
          " * former load-time __attribute__((constructor)) installer. */\n"
        + f"struct yetty_ycore_void_result yetty_{module}_register(void);\n\n"
        + "#endif\n"
    )


def emit_rpc_c(model: dict, module: str, out_path: Path):
    class_includes = "".join(
        f'#include "{class_header_for(c, module)}"\n'
        for c in model.get("classes", [])
    )
    parts = [HEADER,
             '#include <yetty/yclass/rpc.h>\n',
             '#include <yetty/ycore/result.h>\n',
             '#include <yetty/ytrace/ytrace.h>\n',
             '#include <yetty/yclass/class.h>\n',
             class_includes,
             '#include <stdbool.h>\n#include <stdint.h>\n#include <stdio.h>\n'
             '#include <stdlib.h>\n#include <string.h>\n\n']
    for m in model["methods"]:
        # Local-only slots never cross the wire and have no skel — the
        # public stub is dispatch-only, and emit_lookup_tables omits
        # them from skel_rows.
        if m.get("local"):
            continue
        parts.append(emit_skel(m))
        parts.append("\n")
    for c in regular_classes(model):
        parts.append(emit_create_fn(c, model, module))
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
    # headers seed with `#include <yetty/yclass/class.h>` so the runtime structs
    # (ctx, object, class) are visible during AST parsing.
    placeholder_class_h = '#include <yetty/yclass/class.h>\n'
    for s in sources:
        if s.suffix == ".c":
            # Place the per-source .gen.c next to its annotated source
            # (matches emit_class_gen_c). Widget files live under
            # `widgets/` so their .gen.c also lands there — the
            # hand-written .c's `#include "<stem>.gen.c"` then resolves
            # via the same-directory search.
            inc = s.with_name(s.stem + ".gen.c")
            if not inc.exists():
                inc.write_text("")
            # Pre-touch the per-class header at the source-mirrored
            # subdir. We don't know the class name (and a source can
            # define more than one) at this point, so we use the file
            # stem as a best-effort placeholder for first parse.
            # emit_class_public_headers may pick a different name
            # later; the orphan stub is harmless.
            anchor = ("src", "yetty", module)
            parts = s.resolve().parts
            rel_subdir = Path(".")
            for i in range(len(parts) - len(anchor) + 1):
                if parts[i:i + len(anchor)] == anchor:
                    rel_subdir = Path(*parts[i + len(anchor):]).parent
                    break
            hdr_dir = include_module / rel_subdir
            hdr_dir.mkdir(parents=True, exist_ok=True)
            hdr = hdr_dir / (s.stem + ".h")
            if not hdr.exists():
                hdr.write_text(placeholder_class_h)
    # Remove stale module-wide method headers — slot stubs + `_fn` typedefs
    # now live in each source's own <stem>.h (and inline in methods.gen.c).
    for stale in (module_src / "methods.gen.h", include_module / "methods.h"):
        if stale.exists():
            stale.unlink()

    # Clang search path: shared include/, the module's own generated-
    # header dir (include/<module>/), and the module src dir (for
    # any neighbour .h that might still exist transiently).
    model = parse_sources([include_base, include_module, module_src], sources, module)
    # Collect every non-conforming class and report them all at once, rather
    # than aborting on the first — a module like ygui has dozens to rename.
    class_errors = [e for e in (validate_class(c) for c in model["classes"]) if e]
    if class_errors:
        for e in class_errors:
            sys.stderr.write(e)
        sys.stderr.write(
            f"error: {len(class_errors)} non-conforming data struct tag(s) in "
            f"module '{module}'.\n")
        sys.exit(1)
    for m in model["methods"]:
        validate_method(m)

    emit_class_public_headers(model, module, include_module)
    emit_methods_c(model, module, module_src / "methods.gen.c")
    emit_class_gen_c(model, module, module_src)
    emit_rpc_c(model, module, module_src / "rpc.gen.c")

    (module_src / "model.yaml").write_text(yaml_dump(model) + "\n")


if __name__ == "__main__":
    main()
