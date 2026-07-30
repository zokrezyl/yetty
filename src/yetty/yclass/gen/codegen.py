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
    <class>.h          — accessor decl + the public stubs and `<slot>_fn`
                         typedefs for the slots this class introduces.
                         GENERATED (replaces any hand-written class header).
                         A subclass includes its parent's <stem>.h for the
                         inherited slots; there is no module-wide methods.h.
    rpc.h          — every <class>_create() in this module.

  Internal (under <module_src_dir>/):
    <class>.gen.c      — accessor body + public stubs + server skels + the
                         class factory, included at the foot of <class>.c.
    rpc.gen.c          — accessor/skel lookups + yetty_<module>_register()
                         (one per submodule; the module-root chains them).
    model.yaml         — informational dump.

Symbol naming:
  Every generated C identifier visible at link time is prefixed
  `yetty_<module>_<class>` / `yetty_<module>_<slot>`. The same
  `yetty_<module>_<localname>` form is also the slot_table key and the
  wire label — one canonical name, three uses.

Method signature contract:
  RetT slot(struct yetty_yclass_object *obj, <rest...>);

  Methods take no ctx — the RPC session is linked onto the object at create
  time (obj->session). The public stub branches on obj->session:
    NULL → local: vtable dispatch via obj->klass.
    set  → remote: look up remote_id via xlat (batched per-class via
           yetty_yclass_rpc_session_translate_class, lazy fallback via
           yetty_yclass_rpc_session_ensure_remote_id), then
           yetty_yclass_rpc_call(YETTY_YCLASS_RPC_OP_CALL, rid).

Object creation:
  <class>_create(ctx) — the ONLY entry point that takes a ctx; it links
  ctx->session onto the object. Local (ctx/session NULL):
  yetty_yclass_object_alloc; remote: per-class translate handshake, then
  yetty_yclass_rpc_call(CREATE, "<name>"), wraps the returned handle in a
  proxy whose header.session = ctx->session.

Usage:
  ./codegen.py <module> <include_base> <module_src_dir> [--headers-local] <source.c>...

  <include_base>      shared include root, e.g. "include". Generated
                      public headers land in <include_base>/<module>/.
  <module_src_dir>    where the annotated .c files live. Generated
                      internal artefacts (.gen.c, model.yaml) land here.
  --headers-local     write each generated public header next to its source
                      (same dir as the .c) instead of under <include_base>/,
                      for modules whose sources live outside the include tree.
"""

import json
import os
import re
import shlex
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


def result_payload_type(return_type: str, types_by_name: dict):
    """The wire payload C type a Result return carries — the type of the
    `value` member inside its union, read straight from the parsed struct (no
    hand-maintained type table). None for the void Result, which carries no
    success payload. `types_by_name` maps a struct tag to its harvested entry.

    Used only to stamp `m['return_payload_type']` at model-build time so the
    RPC/dispatch marshalling — the one place that needs the success value's
    layout — reads it off the method instead of recomputing type knowledge."""
    rt = return_type.strip()
    if rt == "struct yetty_ycore_void_result":
        return None
    m = re.match(r"^struct\s+(\w+)\s*$", rt)
    if not m:
        return None
    entry = types_by_name.get(m.group(1))
    if not entry:
        return None
    for field in entry.get("fields", []):
        if field.get("kind") == "union":
            for sub in field.get("fields", []):
                if sub.get("name") == "value":
                    return sub.get("type")
    return None


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
#   override@<BASE_DOMAIN>:<BASE_CLASS>:<SLOT>        on impl fn      — the ONLY form.
#                                                                       Names ONLY the base
#                                                                       slot being overridden
#                                                                       (the parent/mixin that
#                                                                       declared <SLOT>). The
#                                                                       overriding class is
#                                                                       NEVER written — it is
#                                                                       the class this
#                                                                       annotation sits on,
#                                                                       resolved from context.
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

def _clang_format(content: str, path: Path) -> str:
    """Return `content` clang-formatted as if it were `path` — `-assume-filename`
    gives clang-format the real name so it picks the C/C++ language AND locates
    the repo's .clang-format by walking up from that path. Reads stdin, writes
    stdout, so the raw text never touches disk; only the formatted result does."""
    clang_format = os.environ.get("CLANG_FORMAT", "clang-format")
    result = subprocess.run([clang_format, f"-assume-filename={path}"],
                            input=content, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(f"codegen: clang-format failed on {path}:\n{result.stderr}")
        sys.exit(1)
    return result.stdout


def _write_atomic(path, content: str):
    """Write `content` to `path` atomically (temp file + os.replace rename).
    Codegen modules run in PARALLEL: a consumer module's clang parse reads a
    producer module's freshly-regenerated header, so a plain truncate-then-write
    could be read mid-write. With the rename a concurrent reader always sees
    either the complete old file or the complete new one — never a torn one.
    The temp name carries the pid so two processes never collide on it.

    A generated C/C++ file is clang-formatted HERE, before the write, so the
    file that lands at `path` is always the formatted one — codegen never emits
    raw C that a separate format pass has to clean up. Non-C outputs (model.yaml)
    just get their trailing edge normalized (single newline, no trailing blank
    line); empty pre-touch placeholders are left untouched."""
    if content.strip():
        content = content.rstrip() + "\n"
        if path.suffix in (".c", ".h"):
            content = _clang_format(content, path)
    # Content-identical writes are SKIPPED so the file's mtime moves only on
    # a real change. The build graph leans on this: ninja/pass-2 staleness is
    # judged by generated-header mtimes, so an unchanged tree is a no-op and
    # a one-module edit doesn't cascade re-parses through every consumer.
    try:
        with open(path) as existing:
            if existing.read() == content:
                return
    except OSError:
        pass
    tmp = path.with_name(f"{path.name}.tmp.{os.getpid()}")
    with open(tmp, "w") as handle:
        handle.write(content)
    os.replace(tmp, path)


# The codegen parse must see EXACTLY the include roots and defines the real
# build uses — third-party headers (webgpu from the Dawn fetch, yyjson, libvterm)
# are provided at CONFIGURE time, not committed, so their paths live only in the
# build's compile_commands.json. We read the per-source flags from there instead
# of hand-maintaining include lists or leaning on a committed copy: an unresolved
# type would otherwise degrade to `int` in the model. Cached across all sources.
_COMPILE_DB = None


def _extract_parse_flags(command: str) -> list:
    """Pull just the preprocessor/include flags out of a full compile command —
    everything the AST parse needs to resolve headers and macros, and nothing
    that would fight the -fsyntax-only/-std=gnu2x parse (drop -c/-o/-M/-O/-g/-W
    and the source/compiler tokens). Handles both attached (-Ifoo, -Dfoo) and
    separated (-isystem foo, -include foo) spellings."""
    flags: list = []
    tokens = shlex.split(command)
    two_arg = {"-isystem", "-iquote", "-idirafter", "-include", "-imacros"}
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token in two_arg and index + 1 < len(tokens):
            flags += [token, tokens[index + 1]]
            index += 2
            continue
        if token in ("-I", "-D") and index + 1 < len(tokens):
            flags += [token, tokens[index + 1]]
            index += 2
            continue
        if token.startswith(("-I", "-D")):
            flags.append(token)
        index += 1
    return flags


def _load_compile_db() -> dict:
    global _COMPILE_DB
    if _COMPILE_DB is not None:
        return _COMPILE_DB
    _COMPILE_DB = {}
    db_path = os.environ.get("YCLASS_COMPILE_DB", "compile_commands.json")
    if not os.path.exists(db_path):
        return _COMPILE_DB
    for entry in json.load(open(db_path)):
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        _COMPILE_DB[os.path.abspath(entry["file"])] = _extract_parse_flags(command)
    return _COMPILE_DB


def _compile_flags_for(path: Path):
    """The build's include/define flags for this source, or None if the compile
    database has no usable entry. A source not compiled in THIS build config
    (yplatform's android/ios/webasm variants on a desktop build) borrows a
    same-directory sibling's flags — same module include needs, and its
    platform-specific includes are behind #ifdefs this parse never enters."""
    db = _load_compile_db()
    if not db:
        return None
    absolute = os.path.abspath(str(path))
    if absolute in db:
        return db[absolute]
    directory = os.path.dirname(absolute)
    for known, flags in db.items():
        if os.path.dirname(known) == directory:
            return flags
    return None


def ast_dump(path: Path, include_dirs: list) -> dict:
    clang = os.environ.get("CLANG", "clang")
    # Tolerate semantic errors in the source — what we need from clang
    # is the parse tree (Decl + annotation nodes), not a clean
    # syntax-check. ygui's annotated sources reference codegen-emitted
    # public-stub symbols (yetty_ygui_widget_paint, yetty_ygui_constructor,
    # etc.) that won't exist until the generated per-class <stem>.h files
    # have been written. We let clang emit "undeclared identifier" errors
    # but still produce JSON AST as long as the file parses syntactically.
    # Public types the signatures use (structs, enums, typedefs) are authored
    # plainly in the .c — visible to this parse and to the real build alike —
    # and codegen reproduces the needed ones into the generated header, so no
    # special define is required for them to resolve here.
    cmd = [clang, "-Xclang", "-ast-dump=json", "-fsyntax-only", "-std=gnu2x",
           "-ferror-limit=0", "-Wno-error", "-Wno-everything"]
    # Primary: parse with the SAME include roots + defines the real build uses,
    # read from compile_commands.json. This is how third-party headers resolve
    # — <webgpu/webgpu.h> from the Dawn fetch, <yyjson.h>, <vterm.h> — without
    # committing a copy or hand-maintaining paths, and it carries the feature
    # #ifdef defines (YETTY_<TOOL>_HAS_STANDALONE, WEBGPU_BACKEND_DAWN, …) that
    # make annotated declarations visible. Codegen is a post-configure step;
    # if there is no compile database, fall back to the caller-supplied roots
    # and the YCLASS_DEFINES/YCLASS_INCLUDE_DIRS env (legacy path).
    build_flags = _compile_flags_for(path)
    if build_flags is not None:
        cmd += build_flags
    else:
        for macro in os.environ.get("YCLASS_DEFINES", "").split():
            cmd.append(f"-D{macro}")
        for extra_include_dir in os.environ.get("YCLASS_INCLUDE_DIRS", "").split():
            cmd.append(f"-I{extra_include_dir}")
        for d in include_dirs:
            cmd.append(f"-I{d}")
    cmd.append(str(path))
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    # clang still dumps a well-formed AST on stdout even when it reports
    # semantic errors, and we deliberately consume it for ONE tolerated class:
    # a reference to a codegen-emitted stub symbol (yetty_<...>) that does not
    # exist until the generated glue is written. Those are undeclared
    # *identifiers* (functions) and never affect a recorded TYPE.
    #
    # An error in the HAND-WRITTEN source is fatal, because it corrupts the
    # model: a missing #include or an unresolved type makes clang substitute
    # `int` for the real type, so a field/arg silently lands in model.yaml as
    # `int`/`int *` (e.g. VTerm* → int* when libvterm's include root is missing).
    # The model is the FFI contract; a degraded parse must fail, not ship `int`.
    #
    # Errors that concern a GENERATED artifact under src/yetty/gen/ are tolerated
    # — that tree is codegen OUTPUT the hand-written .c only foot-#includes for
    # compilation (its own impl glue; a parent's generated header/`_fn` typedef).
    # It is produced FROM the model, never read INTO it: the class annotations
    # are parsed before the foot include, so a missing or momentarily-incomplete
    # generated file (they race during the parallel two-pass regeneration) never
    # changes a recorded type.
    fatal = []
    for line in r.stderr.splitlines():
        if " error:" not in line:  # matches "error:" and "fatal error:"
            continue
        if "use of undeclared identifier 'yetty_" in line:
            continue  # forward reference to a not-yet-generated stub — expected
        if "file not found" in line and "yetty/gen/" in line:
            continue  # foot-included generated artifact absent during the parse
        location = re.match(r"^(\S.*?):\d+:\d+:", line)
        if location and "/yetty/gen/" in location.group(1):
            continue  # error located INSIDE a generated artifact — not a model input
        fatal.append(line)
    if fatal:
        sys.stderr.write(
            f"codegen: unresolved symbol(s) parsing {path} — the model would "
            f"record degraded (int) types. Add the module's real include roots "
            f"via YCLASS_INCLUDE_DIRS_<module> (or defines via YCLASS_DEFINES_"
            f"<module>) so every type resolves:\n")
        sys.stderr.write("\n".join(fatal) + "\n")
        sys.exit(1)
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
    beg = rng.get("begin", {})
    # The payload string is the text written at the USE site. yclass annotations
    # are spelled YETTY_ANNOTATE("...") (a function-like macro whose body is just
    # `annotate(annotation)` — the value is the call-site argument), so read the
    # EXPANSION location, which points at the macro invocation in the .c source.
    # A raw [[clang::annotate("...")]] has no expansion and carries a flat offset;
    # fall back to the spelling for anything odd. The expansion range spans only
    # the macro-name token, so slice a generous window from its start and take
    # the first annotate("...") — each AnnotateAttr points at its own invocation,
    # so the first match is this annotation's payload.
    loc = beg.get("expansionLoc") or beg
    if "offset" not in loc:
        loc = beg.get("spellingLoc", {})
    if "offset" not in loc:
        return None
    attr_file = loc.get("file") or current_file
    if not attr_file:
        return None
    blob = file_bytes_cache.get(attr_file)
    if blob is None:
        try:
            blob = Path(attr_file).read_bytes()
        except OSError:
            return None
        file_bytes_cache[attr_file] = blob
    start = loc["offset"]
    text = blob[start:start + 256].decode("utf-8", errors="replace")
    m = re.search(r'annotate\s*\(\s*"([^"]*)"', text, re.IGNORECASE)
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


def _fmt_field(type_str: str, name: str) -> str:
    """Render one struct member as C. clang hands array members back as
    `elem[N]` (e.g. `uint32_t[5]`), but C syntax puts the dimensions after
    the NAME — so `uint32_t[5]` + `marks` becomes `uint32_t marks[5]`.
    Non-array types keep the original `<type> <name>` spelling verbatim."""
    match = re.match(r"^(.*?)\s*((?:\[\d*\])+)$", type_str.strip())
    if match:
        return f"{match.group(1).strip()} {name}{match.group(2)}"
    return f"{type_str} {name}"


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

    def file_of(loc_like):
        # A location names its file either flat (`file`) or — for macro
        # expansions — split into spellingLoc/expansionLoc. Document-order
        # file tracking must follow the EXPANSION side: that is where the
        # token sits in the including file. Without this, a macro-expanded
        # decl (e.g. a YETTY_YRESULT_DECLARE right after the #include
        # block) swallows the file-change marker and every later
        # annotation in the source gets sliced from the wrong file.
        if not isinstance(loc_like, dict):
            return None
        if "file" in loc_like:
            return loc_like["file"]
        expansion = loc_like.get("expansionLoc")
        if isinstance(expansion, dict) and "file" in expansion:
            return expansion["file"]
        return None

    def visit(n):
        # Recurse into BEGIN-locations too: clang puts the file
        # indicator on the begin/end of a range when no top-level
        # `loc.file` exists. Without this, attribute-only nodes
        # (which carry `range` but no `loc`) wouldn't update state.
        loc_file = file_of(n.get("loc"))
        if loc_file:
            state[0] = loc_file
        rng = n.get("range")
        if isinstance(rng, dict):
            begin_file = file_of(rng.get("begin"))
            if begin_file:
                state[0] = begin_file
        if n.get("kind") in ("FunctionDecl", "RecordDecl", "EnumDecl", "TypedefDecl"):
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

def _typedef_text(decl: dict) -> str:
    """Reconstruct a `typedef …;` line from a TypedefDecl AST node. Function-
    pointer typedefs carry their underlying type as `RET (*)(ARGS)`; splice the
    name into the `(*)`. Plain typedefs become `typedef <underlying> <name>;`."""
    name = decl.get("name") or ""
    underlying = decl.get("type", {}).get("qualType", "")
    if "(*)" in underlying:
        return "typedef " + underlying.replace("(*)", "(*" + name + ")", 1) + ";"
    sep = "" if underlying.endswith("*") else " "
    return f"typedef {underlying}{sep}{name};"


def _typedef_defined_names(text: str) -> set:
    """The set of typedef names DEFINED by `typedef …;` statements in a text
    blob. Used to keep the local-typedef reproduction from re-emitting a typedef
    another emission block already defines (e.g. an override `_fn` type the stub
    machinery emits) — a second identical typedef in the same header is at best
    redundant and trips -Werror on older toolchains."""
    names: set = set()
    # Function-pointer typedef: `typedef RET (*NAME)(ARGS);` — name inside (* ).
    names |= set(re.findall(r"\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(", text or ""))
    # Plain/object typedef: `typedef <underlying> NAME;` — last identifier
    # before the terminating `;` (skip the function-pointer form handled above).
    for stmt in re.findall(r"\btypedef\b[^;{}]*;", text or ""):
        if "(*" in stmt:
            continue
        match = re.search(r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*;\s*$", stmt)
        if match:
            names.add(match.group(1))
    return names


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


def _harvest_comments(blob: bytes) -> list:
    """Every comment span in source order as (start, end, verbatim_text),
    byte offsets matching clang's `range` offsets. Runs of adjacent `//`
    line comments are merged into one span; comments inside string/char
    literals are skipped. Used to recover the doc comment above a decl —
    clang's own FullComment attachment drops the comment whenever the decl
    carries a `[[clang::annotate]]` attribute (i.e. every yclass struct), so
    we read the source directly instead."""
    SLASH, STAR, DQUOTE, SQUOTE, BACKSLASH, NEWLINE = 0x2F, 0x2A, 0x22, 0x27, 0x5C, 0x0A
    spans = []
    index, length = 0, len(blob)
    while index < length:
        byte = blob[index]
        if byte in (DQUOTE, SQUOTE):
            quote = byte
            index += 1
            while index < length:
                if blob[index] == BACKSLASH:
                    index += 2
                    continue
                if blob[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if byte == SLASH and index + 1 < length and blob[index + 1] == STAR:
            start = index
            index += 2
            while index + 1 < length and not (blob[index] == STAR and blob[index + 1] == SLASH):
                index += 1
            index = min(index + 2, length)
            spans.append([start, index])
            continue
        if byte == SLASH and index + 1 < length and blob[index + 1] == SLASH:
            start = index
            while index < length and blob[index] != NEWLINE:
                index += 1
            spans.append([start, index])
            continue
        index += 1
    merged = []
    for span in spans:
        if merged:
            prev = merged[-1]
            between = blob[prev[1]:span[0]]
            if (blob[prev[0]:prev[0] + 2] == b"//" and blob[span[0]:span[0] + 2] == b"//"
                    and between.strip() == b"" and between.count(b"\n") <= 1):
                prev[1] = span[1]
                continue
        merged.append(span)
    return [(a, b, blob[a:b].decode("utf-8", "replace")) for a, b in merged]


def _doc_above(begin_offset, comments: list, blob: bytes):
    """The verbatim comment documenting the decl that starts at
    `begin_offset`, or None. It is the nearest comment ending before the
    decl, accepted ONLY when it is directly attached: between comment and
    decl there may be `[[…]]` attribute blocks and single newlines, but no
    blank line and no other token. The blank-line rule keeps section-banner
    comments (`/*==== … ====*/` followed by a blank line) from being
    mistaken for a declaration's documentation."""
    if begin_offset is None:
        return None
    # A `/* clang-format off|on */` directive is tooling, never documentation —
    # they commonly wrap the YETTY_ANNOTATE block above a decl. Skip them as
    # candidates and treat them as transparent in the gap below.
    directive = re.compile(r"^\s*/\*\s*clang-format\s+(?:on|off)\s*\*/\s*$")
    candidate = None
    for span in comments:
        if span[1] <= begin_offset:
            if directive.match(span[2]):
                continue
            candidate = span
        else:
            break
    if candidate is None:
        return None
    gap = blob[candidate[1]:begin_offset].decode("utf-8", "replace")
    # Only whitespace, attribute blocks, and clang-format directives may separate
    # doc from decl. Attributes are written either raw ([[...]]) or via the
    # YETTY_ANNOTATE("...") macro.
    attr = (r"\[\[.*?\]\]|YETTY_ANNOTATE\s*\([^)]*\)"
            r"|/\*\s*clang-format\s+(?:on|off)\s*\*/")
    if re.sub(attr, "", gap, flags=re.S).strip() != "":
        return None
    # Reject when a blank line intervenes — replace attributes with a marker
    # first so an attribute on its own line isn't read as a blank line.
    if re.search(r"\n[ \t]*\n", re.sub(attr, "X", gap, flags=re.S)):
        return None
    return candidate[2].rstrip("\n")


def _doc_prefix(doc) -> str:
    """A captured doc comment formatted as a prefix to emit above a generated
    declaration (verbatim, trailing newline so the decl sits right below)."""
    return (doc.rstrip("\n") + "\n") if doc else ""


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
    # Slots flagged `oneway@<DOMAIN>:<SLOT>` — their public stub marshals over
    # RPC fire-and-forget (no response read; server writes none). Void slots
    # only. See the post-parse application + emit_dispatch_body.
    oneway_slots: set = set()
    # Slots INTRODUCED via virtual@ in this module (their declaring class is
    # the authoritative owner). Every same-module slot must appear here —
    # see the post-parse enforcement below.
    virtual_slots: set = set()
    # slot -> (class, impl-fn) of its virtual@ introduction, to reject a
    # SECOND virtual@ for the same slot (a slot must be introduced exactly
    # once; a subclass re-introducing it instead of overriding is a bug).
    virtual_owner: dict = {}
    exposed: list = []
    # Type registries for `types:` harvesting — every named struct/enum
    # definition seen across the parsed TUs, keyed by tag.
    record_reg: dict = {}
    enum_reg: dict = {}
    # Authoritative struct/enum bodies harvested from the module .c that DEFINES
    # each type (has a body, decl_file == the .c). `_collect_type_decls` fills
    # record_reg/enum_reg last-write-wins across every parsed TU, so a sibling
    # source that #includes the (possibly stale) generated header would clobber
    # the freshly-edited definition — freezing the reproduced type at its
    # pre-edit shape and making the generated header impossible to update. These
    # are re-applied over record_reg/enum_reg after all TUs parse so the owning
    # .c always wins, regardless of parse order or header staleness.
    authoritative_records: dict = {}
    authoritative_enums: dict = {}
    # Tag -> the module .c that DEFINES it (has a body). Only types authored
    # in the module's own sources are safe to reproduce as a full definition
    # in a generated header; types from #include'd third-party/ycore headers
    # are reached through those includes, never re-defined.
    local_type_files: dict = {}
    # Typedef name -> {source_file, text}. Locally-authored typedefs (callback
    # function pointers, …) reproduced into the header when a public signature
    # or type references them.
    local_typedefs: dict = {}
    # Source file -> ordered list of header-destined #include paths, harvested
    # from `include@<path>` annotations (foreign by-value types the generated
    # header must pull in).
    includes_by_file: dict = {}
    # Source file -> the current module's class declared in that file. This
    # supports the app-style override spelling where the annotation names the
    # virtual method being overridden (`override@yapp:app:run`) and the
    # implementing class is the source file's class.
    impl_class_by_file: dict = {}

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
    # Harvested comment spans, memoised per file (parallel to file_bytes_cache).
    comments_by_file: dict = {}

    def doc_for(decl, decl_file):
        """Verbatim doc comment authored above `decl` in `decl_file`, or None."""
        blob = file_bytes_cache.get(decl_file)
        if blob is None:
            try:
                blob = Path(decl_file).read_bytes()
            except OSError:
                return None
            file_bytes_cache[decl_file] = blob
        comments = comments_by_file.get(decl_file)
        if comments is None:
            comments = _harvest_comments(blob)
            comments_by_file[decl_file] = comments
        # When the decl is prefixed by a YETTY_ANNOTATE("...") macro, its
        # range.begin is the macro location split into spelling/expansion rather
        # than a flat offset. The expansion is the macro use-site in this .c, the
        # offset the doc-gap scan needs; fall back to spelling, then flat.
        begin_node = decl.get("range", {}).get("begin", {})
        begin = (begin_node.get("offset")
                 or begin_node.get("expansionLoc", {}).get("offset")
                 or begin_node.get("spellingLoc", {}).get("offset"))
        return _doc_above(begin, comments, blob)

    for path in sources:
        # Seed the cache so the .c's own annotations resolve regardless
        # of whether clang emits a `loc.file` on its top-level decls
        # (it usually does, but seeding here removes the dependency).
        file_bytes_cache[str(path)] = path.read_bytes()
        tu = ast_dump(path, include_dirs)
        # Index every struct/enum definition this TU sees, for `types:`.
        _collect_type_decls(tu, record_reg, enum_reg)
        # The .c being processed is the default "current file" for the
        # top-level translation unit — clang annotates the file change
        # only on the FIRST declaration that moves into a header.
        for decl, decl_file in _walk_decls(tu, current_file=str(path)):
            kind = decl.get("kind")
            # Record named types so the header emitter can reproduce a type a
            # public signature uses (by value for a struct/enum, by name for a
            # typedef) — without the type carrying an `expose` annotation.
            #
            # Structs/enums are recorded only when DEFINED in this .c (the owning
            # source re-asserts its authoritative body). Typedefs are recorded
            # from ANY file in the translation unit: a public signature in this
            # module may use a typedef defined in a sibling file (e.g. a mixin's
            # callback type like yetty_ygui_click_cb, used by a widget). Every
            # typedef in the TU lands here keyed by name; the ones no signature
            # references are dropped before the model is written (see below).
            if decl.get("name") and kind == "TypedefDecl":
                if decl["name"] not in local_typedefs:
                    local_typedefs[decl["name"]] = {
                        "source_file": decl_file, "text": _typedef_text(decl)}
            elif decl_file == str(path) and decl.get("name"):
                if kind in ("RecordDecl", "EnumDecl"):
                    record_body = _record_field_list(decl) if kind == "RecordDecl" else None
                    enum_body = _enum_values(decl) if kind == "EnumDecl" else None
                    has_body = bool(record_body) if kind == "RecordDecl" else bool(enum_body)
                    if has_body:
                        local_type_files.setdefault(decl["name"], str(path))
                        # This .c owns the type — record its own body so it can
                        # be re-asserted over any stale header copy after parse.
                        if kind == "RecordDecl":
                            authoritative_records[decl["name"]] = record_body
                        else:
                            authoritative_enums[decl["name"]] = enum_body
            anns = _collect_annotations(decl, file_bytes_cache, decl_file)
            # Header-destined #include directives: `include@<path>` pulls a
            # foreign header into the generated <stem>.h (for a by-value type
            # the public signatures need complete).
            for ann in anns:
                if ann.startswith("include@") and decl_file == str(path):
                    inc = ann[len("include@"):].strip()
                    if inc:
                        bucket_list = includes_by_file.setdefault(str(path), [])
                        if inc not in bucket_list:
                            bucket_list.append(inc)
            if not anns: continue

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
                    "doc": doc_for(decl, decl_file),
                })
                continue

            # `expose` on a struct — public type. A definition (has fields)
            # emits the full `struct X { … };`; a bare forward declaration
            # emits `struct X;`. Same single-source model as functions: the
            # type is authored ONCE in the owning .c and reproduced into its
            # header, so there is no separate header-destined copy to keep in
            # sync. (The owning .c must NOT include its own generated header,
            # or its definition would clash with the emitted copy.)
            if "expose" in anns and decl_file == str(path) and kind == "RecordDecl":
                name = decl.get("name")
                if name:
                    fields = _record_fields(decl, file_bytes_cache, decl_file)
                    if fields:
                        body = "\n".join(f"    {_fmt_field(fld['type'], fld['name'])};"
                                         for fld in fields)
                        type_text = f"struct {name} {{\n{body}\n}};"
                    else:
                        type_text = f"struct {name};"
                    exposed.append({"source_file": str(path), "type_text": type_text,
                                    "doc": doc_for(decl, decl_file)})
                continue

            # `expose` on an enum — public type. Force its full definition into
            # the header even when no signature uses it by value (e.g. public
            # flag/constant enums consumers reference directly).
            if "expose" in anns and decl_file == str(path) and kind == "EnumDecl":
                name = decl.get("name")
                if name:
                    values = _enum_values(decl)
                    body = "\n".join(f"    {v['name']} = {v['value']}," for v in values)
                    type_text = f"enum {name} {{\n{body}\n}};"
                    exposed.append({"source_file": str(path), "type_text": type_text,
                                    "doc": doc_for(decl, decl_file)})
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
                    if role == "oneway":
                        # `oneway@<DOMAIN>:<SLOT>` — the public stub marshals
                        # fire-and-forget (RPC_OP_CALL_ONEWAY): no response is
                        # read and the server writes none. Void slots only
                        # (enforced post-parse). Like local@, the function it
                        # rides on is just an anchor for the slot attribute.
                        require_segments(role, args, 2, "oneway@<DOMAIN>:<SLOT>")
                        slot_dom, slot_name = args[0], args[1]
                        if slot_dom != module:
                            # Foreign-module slot attribute — that module's own
                            # codegen owns its wire contract; ignore here.
                            continue
                        oneway_slots.add(slot_name)
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
                        # Reject a duplicate introduction. A given (class, fn)
                        # may be re-seen across TUs (header inclusion) — that's
                        # the same annotation and fine; a DIFFERENT class or fn
                        # declaring the same slot virtual@ is two introductions,
                        # which is the bug we must catch.
                        prev = virtual_owner.get(slot)
                        if prev is not None and prev != (cls, decl["name"]):
                            sys.stderr.write(
                                f"error: module '{module}': slot '{slot}' is "
                                f"introduced with virtual@ more than once — by "
                                f"'{prev[0]}' ({prev[1]}) and '{cls}' "
                                f"({decl['name']}). A virtual method must be "
                                f"declared exactly once (by the base class that "
                                f"owns it); every other class must use "
                                f"override@{module}:<class>:{slot}.\n")
                            sys.exit(1)
                        virtual_owner[slot] = (cls, decl["name"])
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
                            "doc": doc_for(decl, decl_file),
                        }
                        virtual_slots.add(slot)
                        continue
                    if role == "override":
                        # ONE accepted shape — 3 segments naming only the base
                        # slot: override@<BASE_DOMAIN>:<BASE_CLASS>:<SLOT>. When
                        # BASE_DOMAIN == this module it is a same-module override;
                        # when foreign, it names another module's virtual method
                        # (e.g. override@yapp:app:run) and the impl class is the
                        # current source's class@. The overriding class is never
                        # written — it is resolved from context. The 4-segment
                        # form (restating the overriding class) is rejected below.
                        if len(args) == 3:
                            ann_dom, ann_cls, slot = args
                            if ann_dom == module:
                                impl_dom, cls, slot_dom = ann_dom, ann_cls, ann_dom
                            else:
                                cls = impl_class_by_file.get(str(path))
                                if not cls:
                                    sys.stderr.write(
                                        f"error: 'override@{':'.join(args)}' names a foreign "
                                        "virtual method, but no local class@ has been seen in "
                                        f"{path}; declare the class before its overrides.\n")
                                    sys.exit(1)
                                impl_dom, slot_dom = module, ann_dom
                        else:
                            sys.stderr.write(
                                f"error: 'override@{':'.join(args)}' — the only accepted form is "
                                "override@<BASE_DOMAIN>:<BASE_CLASS>:<SLOT>, naming ONLY the base "
                                "slot being overridden. Never name the overriding class — it is "
                                "the class this annotation sits on, resolved from context. The "
                                "4-part form is removed.\n")
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
                                "doc": doc_for(decl, decl_file),
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
                    impl_class_by_file.setdefault(str(path), primary)
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
                        b["doc"] = doc_for(decl, decl_file)
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
                            elif role == "platform":
                                # platform@<token> — the class belongs to one
                                # build platform. Its REGISTRATION entry in
                                # rpc.gen.c is wrapped in #ifdef YETTY_PLATFORM_
                                # <TOKEN> so only the active platform's classes
                                # register; the class .c itself is compiled only
                                # on that platform by CMake.
                                require_segments(role, args, 1, "platform@<PLATFORM>")
                                b["platform"] = args[0]

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
        m["oneway"] = m["slot"] in oneway_slots
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
    # oneway@ only makes sense for wire-marshalled void slots: a one-way call
    # reads no response, so a non-void return or a local-only slot is a bug.
    dangling_oneway = [s for s in oneway_slots if s not in impl_slots]
    if dangling_oneway:
        sys.stderr.write(
            f"error: oneway@{module}:<slot> annotations name slots with no "
            f"impl in this module: {dangling_oneway}.\n")
        sys.exit(1)
    for m in methods.values():
        if m["oneway"] and m["local"]:
            sys.stderr.write(
                f"error: oneway@{module}:{m['slot']} is also local@ — a local "
                f"slot has no RPC branch to make one-way.\n")
            sys.exit(1)
    # The void-return check needs return_payload_type, stamped further below.
    # Every slot owned by THIS module must be INTRODUCED exactly once with
    # virtual@. A slot that only ever appears in override@ has no declared
    # owner — its header home would be guessed from processing order, and a
    # typo'd slot name would silently create a phantom slot. Reject it: this
    # is the codegen-time analog of C++ refusing `override` on a non-virtual
    # method. The base class that declares the method must use virtual@.
    missing_virtual = sorted(m["slot"] for m in methods.values()
                             if m["slot"] not in virtual_slots)
    if missing_virtual:
        sys.stderr.write(
            f"error: module '{module}': slot(s) {missing_virtual} are implemented/"
            f"overridden but never INTRODUCED with "
            f"virtual@{module}:<class>:<slot>. Declare each one once (with its "
            f"default impl) in its base class using virtual@; override@ may only "
            f"supply an impl for an already-declared virtual slot.\n")
        sys.exit(1)
    model = {
        "methods": list(methods.values()),
        "classes": [c for c in classes.values() if c["accessor"]],
    }
    # Only carry `exposed` when something used it — keeps an empty
    # `exposed: []` out of every other module's model.yaml.
    if exposed:
        model["exposed"] = exposed
    # Re-assert every type authored in this module's own .c over whatever a
    # sibling TU last wrote into the registries via an #include of the (possibly
    # stale) generated header. Without this, editing a reproduced struct/enum
    # never propagates: the pre-edit shape pulled in through the header wins by
    # parse order and the generated header can't converge on the new definition.
    record_reg.update(authoritative_records)
    enum_reg.update(authoritative_enums)
    # Concrete layouts for by-value struct/enum types referenced by the
    # module's signatures (Result family, POD structs, enums) — transitively
    # resolved so the FFI generator is fully model-driven.
    harvested = _harvest_types(model, record_reg, enum_reg)
    if harvested:
        model["types"] = harvested
    # Stamp each method's success-payload C type (read from its Result's union
    # in the harvested types) so the RPC/dispatch marshalling reads it off the
    # method — the value-type knowledge stays out of the emitters.
    payload_types = {t["name"]: t for t in harvested}
    for method in model["methods"]:
        method["return_payload_type"] = result_payload_type(
            method["return_type"], payload_types)
    # one-way is fire-and-forget: it reads no response, so only void slots
    # qualify (a value return would have nothing to deliver back).
    for method in model["methods"]:
        if method.get("oneway") and method["return_payload_type"] is not None:
            sys.stderr.write(
                f"error: oneway@{module}:{method['slot']} returns a value — "
                f"one-way calls read no response, so only void slots may be "
                f"one-way.\n")
            sys.exit(1)
    # Map of module-locally-defined type tags -> defining source file, so the
    # header emitter knows which by-value types it may reproduce in full (and
    # in which stem's header) versus which arrive via an #include.
    if local_type_files:
        model["local_types"] = local_type_files
    if local_typedefs:
        # Parse recorded EVERY typedef in each TU. Keep only those OWNED by this
        # module — a typedef whose defining file lives under this module's own
        # include/src subtree. Ownership is the right filter: a header must be
        # able to reproduce a callback type authored in a sibling file of THIS
        # module (e.g. yetty_ygui_click_cb in ygui/mixins/clickable.h, used by a
        # widget's stub; yetty_yrdawn_emit_osc_fn in yrdawn/figure.c, used in a
        # factory-args struct), but must NOT reproduce system typedefs (uint32_t,
        # size_t — those arrive via <stdint.h>/<stddef.h>) or another module's
        # typedefs (they arrive via that module's public header #include). The
        # owned set is small and bounded, so it does not bloat the model; the
        # per-header emitter reproduces only the owned typedefs its own content
        # actually names (a regex scan over the emitted structs/protos/stubs).
        owned_marker = f"/yetty/{module_include_subpath(module)}/"
        owned_typedefs = {
            name: info
            for name, info in local_typedefs.items()
            if owned_marker in ("/" + info.get("source_file", ""))}
        if owned_typedefs:
            model["local_typedefs"] = owned_typedefs
    if includes_by_file:
        model["includes"] = includes_by_file
    return model


# Signature validation for cross-domain (and same-domain) overrides is
# done in *C*, not here. Each slot gets a per-slot function-pointer
# typedef (`<slot>_fn`) emitted into the introducing class's public
# `<stem>.h`. Each override emits a file-scope
# `static <slot>_fn _check_… = <impl>;` line in the class's .gen.c
# (see emit_class_accessor). If the impl signature doesn't match the
# slot's, the C compiler errors at the assignment.


# ============== yaml writer (informational dump) ========================

def _yaml_scalar(v) -> str:
    if v is None: return "null"
    if isinstance(v, bool): return "true" if v else "false"
    if isinstance(v, (int, float)): return str(v)
    s = str(v)
    if s == "":
        # Quote the empty string explicitly — a bare `key:` would emit a
        # trailing space after the colon (an anonymous union member's empty
        # name did exactly that) and would re-parse as null, not "".
        return '""'
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
    if len(args) < 1:
        sys.stderr.write(
            f"error: method {m['slot']} needs (obj*, ...). got {len(args)}\n")
        sys.exit(1)
    # First arg is the target object — methods no longer take a ctx; the
    # session is linked onto the object at create time and read from there.
    if not is_specific_struct_ptr(args[0]["type"], "yetty_yclass_object"):
        sys.stderr.write(
            f"error: method {m['slot']}: first arg must be 'struct yetty_yclass_object *' "
            f"(got '{args[0]['type']}')\n")
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
    for a in args[1:]:
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
    # Reject — with ONE blessed exception: `struct
    # yetty_yclass_object_ptr_result` is an OBJECT REFERENCE return
    # (graph navigation). The skel registers the returned object and
    # ships its u64 handle; the client stub wraps the handle in a proxy
    # bound to the same session; a local call returns the real object.
    rid = result_type_id(m["return_type"])
    if rid.endswith("_ptr") and rid != "yetty_yclass_object_ptr":
        sys.stderr.write(
            f"error: method {m['slot']}: return type '{m['return_type']}' "
            f"maps to Result id '{rid}' (pointer payload). Pointer returns "
            f"can't cross process / RPC boundaries. Return a value, a yclass "
            f"object reference (struct yetty_yclass_object_ptr_result), or a "
            f"handle (uint64_t).\n")
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
    """The args that cross the wire: every method arg. The first is the target
    object (marshalled as its handle); the rest follow it. Methods no longer
    take a ctx parameter — the session is read from the object — so there is
    nothing to skip."""
    return m["args"]


def args_struct_body(args: list, indent: str) -> str:
    if not args:
        return f"{indent}char _empty;\n"
    return "".join(f"{indent}{wire_type(a['type'])} {wire_name(a)};\n" for a in args)


# ---------------- methods.gen.c — unified public stub --------------------

def emit_dispatch_body(m: dict, split: bool = False) -> str:
    args = m["args"]
    rid = result_type_id(m["return_type"])
    vt = m["return_payload_type"]
    slot_fn = f"{qualified_slot(m)}_fn"
    obj_name = args[0]["name"]
    call_args = ", ".join(a["name"] for a in args)
    qs = qualified_slot(m)

    # Local-only slot: the public stub is dispatch + NULL-check, no
    # session/RPC branch. Args that don't survive marshalling (raw
    # producer pointers to local carriers) stay in-process by
    # construction.
    #
    # Split mode additionally forces the `constructor` lifecycle slot
    # local: construction is initiated only by the sanctioned creation
    # paths, never as a remote call, so its stub carries no marshalling
    # branch (and no skel is generated for it).
    if m.get("local") or (split and m["slot"] == "constructor"):
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

    # Wire response: status byte (0=OK, 1=ERR) + optional payload. On OK the
    # value bytes (if any) follow; on ERR a serialized error chain follows,
    # which is variable-length — so the response is received into a heap buffer
    # sized to fit (yetty_yclass_rpc_call_alloc) rather than a fixed stack
    # buffer. A remote error is rebuilt with yetty_ycore_error_deserialize and
    # returned as the cause of a locally-raised head, symmetric with the value.
    if vt is None and m.get("oneway"):
        # Fire-and-forget: marshal the args and return without reading a
        # response. The caller cannot observe a remote error — matching the
        # legacy one-way figure wire — so an interactive producer never blocks
        # its input loop on a reply.
        remote_call = f"""\
{body_setup}\
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_oneway(
            {obj_name}->session, remote_id, {body_arg});
{body_cleanup}\
        YETTY_RETURN_IF_ERR({rid}, rpc_call_r, "{qs}: one-way RPC call failed");
        return YETTY_OK_VOID();
"""
    elif vt is None and split:
        # Void wire call through the runtime: pipelined on an async
        # (event-loop) transport — send, return, failures reach the
        # session's error sink with this slot's name; lockstep with the
        # full remote-error decode on a synchronous transport. Blocking
        # response reads never happen inside an embedder's event loop.
        remote_call = f"""\
{body_setup}\
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            {obj_name}->session, remote_id, "{qs}", {body_arg});
{body_cleanup}\
        YETTY_RETURN_IF_ERR({rid}, rpc_call_r, "{qs}: RPC call failed");
        return YETTY_OK_VOID();
"""
    elif vt is None:
        remote_call = f"""\
{body_setup}\
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_alloc(
            {obj_name}->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, {body_arg},
            &resp_buf, &response_len);
{body_cleanup}\
        YETTY_RETURN_IF_ERR({rid}, rpc_call_r, "{qs}: RPC call failed");
        if (response_len < 1) {{
            free(resp_buf);
            return YETTY_ERR({rid}, "{qs}: short RPC response");
        }}
        if (resp_buf[0] != 0) {{
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct {rid}_result remote_error =
                YETTY_ERR({rid}, "{qs}: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }}
        free(resp_buf);
        return YETTY_OK_VOID();
"""
    elif rid == "yetty_yclass_object_ptr":
        # OBJECT REFERENCE return (graph navigation): the wire carries the
        # peer's u64 handle (0 = NULL object); the stub wraps a non-zero
        # handle in a proxy bound to this session. The local branch below
        # returns the real object — same signature, same call site.
        remote_call = f"""\
{body_setup}\
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_alloc(
            {obj_name}->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, {body_arg},
            &resp_buf, &response_len);
{body_cleanup}\
        YETTY_RETURN_IF_ERR({rid}, rpc_call_r, "{qs}: RPC call failed");
        if (response_len < 1) {{
            free(resp_buf);
            return YETTY_ERR({rid}, "{qs}: short RPC response");
        }}
        if (resp_buf[0] != 0) {{
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct {rid}_result remote_error =
                YETTY_ERR({rid}, "{qs}: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }}
        if (response_len != 1 + sizeof(uint64_t)) {{
            free(resp_buf);
            return YETTY_ERR({rid}, "{qs}: truncated RPC payload");
        }}
        uint64_t remote_handle;
        memcpy(&remote_handle, resp_buf + 1, sizeof(remote_handle));
        free(resp_buf);
        if (remote_handle == 0) {{
            return YETTY_OK({rid}, NULL);
        }}
        return yetty_yclass_object_proxy_create({obj_name}->session, remote_handle, NULL);
"""
    else:
        remote_call = f"""\
{body_setup}\
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_alloc(
            {obj_name}->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, {body_arg},
            &resp_buf, &response_len);
{body_cleanup}\
        YETTY_RETURN_IF_ERR({rid}, rpc_call_r, "{qs}: RPC call failed");
        if (response_len < 1) {{
            free(resp_buf);
            return YETTY_ERR({rid}, "{qs}: short RPC response");
        }}
        if (resp_buf[0] != 0) {{
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct {rid}_result remote_error =
                YETTY_ERR({rid}, "{qs}: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }}
        if (response_len != 1 + sizeof({vt})) {{
            free(resp_buf);
            return YETTY_ERR({rid}, "{qs}: truncated RPC payload");
        }}
        {vt} return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK({rid}, return_value);
"""

    if split:
        # Split-mode stub: session test FIRST. The remote branch resolves
        # the slot by its canonical wire name (a compile-time string), so a
        # pure remote client needs NO local slot table — no class
        # registration, no method_slot_get. Only the local branch touches
        # the slot machinery, and only when a real local object exists
        # (whose class registration already populated the table).
        return f"""\
    if (!{obj_name}) return YETTY_ERR({rid}, "{qs}: NULL object");

    if ({obj_name}->session) {{
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name({obj_name}->session, "{qs}");
        YETTY_RETURN_IF_ERR({rid}, remote_id_r, "{qs}: ensure_remote_id_by_name failed");
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
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {{
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_{m['domain']}",
                                             (yetty_yclass_method_id_t){qs});
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR({rid}, "{qs}: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }}
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class({obj_name});
        YETTY_RETURN_IF_ERR({rid}, object_class_r, "{qs}: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR({rid}, dispatch_impl_r, "{qs}: dispatch_lookup failed");
        return (({slot_fn})dispatch_impl_r.value)({call_args});
    }}
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

    if ({obj_name}->session) {{
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id({obj_name}->session, method_slot);
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


def emit_stub_def(m: dict, split: bool = False) -> str:
    """The public slot stub DEFINITION: branches on obj->session (local
    vtable dispatch vs. remote RPC). Emitted into the owning class's
    <stem>.gen.c — or, in split mode, into the standalone
    <stem>.call.gen.c so a pure client links stubs without the
    implementation TU."""
    params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
    rt = result_type(m["return_type"])
    return f"{rt} {qualified_slot(m)}({params})\n{{\n{emit_dispatch_body(m, split)}}}\n\n"


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
        f"YETTY_MAYBE_UNUSED\n"
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
                f"\nstruct yetty_yclass_object_ptr_result {qcls}_to({data} *data)\n"
                f"{{\n"
                f"    if (!data)\n"
                f"        return YETTY_OK(yetty_yclass_object_ptr, NULL);\n"
                f"    struct yetty_yclass_ptr_result class_r = {accessor}();\n"
                f'    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "{qcls}_to: class accessor");\n'
                f"    struct yetty_ycore_size_result offset_r =\n"
                f"        yetty_yclass_object_data_offset(class_r.value, class_r.value);\n"
                f'    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "{qcls}_to: data offset");\n'
                f"    return YETTY_OK(yetty_yclass_object_ptr,\n"
                f"                    (struct yetty_yclass_object *)((char *)data - offset_r.value));\n"
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


def _field_value_types(field: dict) -> list:
    """The C type string(s) a harvested field contributes for by-value
    dependency tracking. A named member yields its one type; an inlined
    anonymous record yields its members' types recursively."""
    if "fields" in field:
        out = []
        for sub in field["fields"]:
            out.extend(_field_value_types(sub))
        return out
    t = field.get("type")
    return [t] if t else []


def _render_field(field: dict, indent: str = "    ") -> str:
    """Render one harvested field back to a C member declaration, mirroring
    the no-space-before-`*` style of the hand-written prototypes."""
    if "fields" in field:
        nested_kind = field.get("kind", "struct")
        inner = "\n".join(_render_field(f, indent + "    ") for f in field["fields"])
        return f"{indent}{nested_kind} {{\n{inner}\n{indent}}} {field.get('name', '')};"
    type_str = field.get("type", "")
    name = field.get("name", "")
    if not name:
        return f"{indent}{type_str};"
    sep = "" if type_str.endswith("*") else " "
    return f"{indent}{type_str}{sep}{name};"


def _render_type_def(entry: dict) -> str:
    """Render a harvested `types:` entry as its full C definition. Every struct
    — including a Result struct, which is just `{ int ok; union { value;
    error; } }` — emits its complete `struct name { … };`; enums emit their
    values. No type is treated specially."""
    name = entry["name"]
    if entry.get("kind") == "enum":
        body = "\n".join(f"    {v['name']} = {v['value']}," for v in entry.get("values", []))
        return f"enum {name} {{\n{body}\n}};"
    body = "\n".join(_render_field(f) for f in entry.get("fields", []))
    return f"struct {name} {{\n{body}\n}};"


def _local_byvalue_type_defs(model: dict, src_file: str, byval_tags: set,
                             exclude: set = None):
    """Full C definitions for the module-local struct/enum types used BY VALUE
    by an exposed function in `src_file`, transitively closed over their own
    by-value members and ordered so a dependency precedes the type that embeds
    it. Returns (list_of_definition_strings, set_of_defined_tag_names).

    The same rule covers every kind of type a signature uses by value —
    including Result structs: a `*_result` used by value is reproduced via its
    `YETTY_YRESULT_DECLARE` just like a plain struct is reproduced via its full
    body. Only types DEFINED in this stem's own .c (`model['local_types']`) are
    reproduced — types reached via an #include (ycore/yclass results, foreign
    structs) stay where they are."""
    types_by_name = {t["name"]: t for t in model.get("types", [])}
    local_types = model.get("local_types", {})
    exclude = exclude or set()

    def emittable(tag: str) -> bool:
        return (tag in types_by_name
                and tag not in exclude  # already emitted (expose / data-block)
                and local_types.get(tag) == src_file  # excludes ycore/foreign
                and not tag.startswith("yetty_yclass"))  # from class.h / rpc.h

    selected: list = []
    seen: set = set()

    def visit(tag: str):
        if tag in seen or not emittable(tag):
            return
        seen.add(tag)
        entry = types_by_name[tag]
        # Follow every by-value member to a dependency (a Result's union `value`
        # member is reached the same way as any other field — no special case).
        if entry.get("kind") != "enum":
            for field in entry.get("fields", []):
                for field_type in _field_value_types(field):
                    category, dep = _classify_type(field_type)
                    if category in ("struct", "union", "enum") and dep:
                        visit(dep)  # dependency-first: appended before this tag
        selected.append(tag)

    for tag in sorted(byval_tags):
        visit(tag)
    return [_render_type_def(types_by_name[t]) for t in selected], set(selected)


def _guard_type_def(text: str) -> str:
    """Wrap a full struct/enum/union definition in a per-type include guard
    so the object-API header and the impl-facing header (which reproduce
    the same by-value types) can be included from one TU. Function and
    typedef declarations are redeclaration-safe and need no guard."""
    m = re.search(r"\b(?:struct|enum|union)\s+(\w+)\s*\{", text)
    if not m:
        return text
    tag = m.group(1).upper()
    return (f"#ifndef YETTY_YCLASSGEN_TYPE_{tag}\n"
            f"#define YETTY_YCLASSGEN_TYPE_{tag}\n{text}\n#endif")


def emit_class_public_headers(model: dict, module: str, include_module_dir: Path,
                              module_src: Path, headers_local: bool = False,
                              split: bool = False):
    """One generated public header per class. Output path mirrors the
    source's subdir structure under `include/yetty/<module>/` so a
    widget at `src/yetty/<module>/widgets/foo.c` lands at
    `include/yetty/<module>/widgets/foo.h` (not `.gen.h` — the file
    IS the public interface; consumers don't care that it's
    generated).

    When `headers_local` is set, the header is written next to its annotated
    source instead (same directory as the .c / .gen.c) — for modules whose
    sources live outside the shared include tree, e.g. the ygui demos.

    The header is 100% generated — there is no hand-written section.
    Function APIs come from `expose` annotations and method slots; the public
    types they use (structs/enums used by value or `expose`d, typedefs, and
    `include@` directives) are authored plainly in the source .c and
    reproduced here, ordered so a type precedes whatever uses it."""
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
        rel_subdir = source_rel_subdir(src_file, module_src)
        api_name = api_namespace(module)  # api/<api_name> (facade drops api_ prefix)
        if split:
            # Role-split layout: the object-API header lives under
            # include/yetty/api/<api>/ — the ONE public header of the class.
            # It declares the object API only: typed method stubs (minus the
            # constructor lifecycle slot), create(), properties, exposed
            # functions, and the types those signatures need. It must not
            # expose the class accessor, data-slice access, skeletons or
            # implementation registration.
            #
            # For an api_<name> facade module, include_module_dir is ALREADY
            # include/yetty/api/<name> (module_include_subpath nests it there),
            # so the header sits directly in it; a normal module nests one level
            # down into api/<module>.
            if module.startswith("api_"):
                out_dir = include_module_dir / rel_subdir
            else:
                out_dir = include_module_dir.parent / "api" / api_name / rel_subdir
            out_dir.mkdir(parents=True, exist_ok=True)
            header_path = out_dir / f"{stem}.h"
        elif headers_local:
            # Co-locate the header with its source (same dir as the .c/.gen.c).
            header_path = src_path.with_name(f"{stem}.h")
        else:
            # Mirror the source's subdir (every nesting level) under
            # include/yetty/<module>/, relative to the module's source root.
            out_dir = include_module_dir / rel_subdir
            out_dir.mkdir(parents=True, exist_ok=True)
            header_path = out_dir / f"{stem}.h"
        # Distinct guard per file path under the module — prevents
        # collisions between widgets/button.h and any other button.h
        # that might exist in the tree.
        guard_path = "_".join(rel_subdir.parts + (stem,)) if rel_subdir != Path(".") else stem
        guard_scope = f"API_{api_name.upper()}" if split else module.upper()
        guard = (f"YETTY_YCLASSGEN_{guard_scope}_"
                 f"{guard_path.upper().replace('-', '_')}_H")
        # Class accessors are implementation-facing — absent from the
        # role-split object-API header.
        decls = "" if split else "\n".join(
            _doc_prefix(c.get("doc"))
            + f"struct yetty_yclass_ptr_result {qualified_class(c)}"
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
        # Result types the data-block already declares via YETTY_YRESULT_DECLARE
        # (the opaque data-handle `<class>_ptr_result`) — excluded from the
        # by-value result-dependency emission below so it isn't declared twice.
        data_block_result_tags = set()
        for c in classes:
            if not c.get("data"):
                continue
            pfields = [f for f in c.get("data_fields", []) if f.get("get") or f.get("set")]
            q = qualified_class(c)
            rid = f"{q}_ptr"
            data_block_result_tags.add(f"{rid}_result")
            # The data-block handle is object API in both layouts: the struct
            # body never leaves the owning .c (opaque pointer only), and
            # exposed functions / embedder code traffic in the typed handle
            # and its by-value Result. Only the struct CONTENTS are
            # implementation-facing, and those are never emitted anywhere.
            lines = [
                "/* Data-block handle — opaque outside the owning .c. The struct\n"
                " * stays private; only its pointer crosses here, in a Result so a\n"
                " * bad object surfaces rather than corrupting. Reach members\n"
                " * through the per-property getters/setters below. */",
                f"{c['data']};",
                _guard_type_def(
                    f"struct {rid}_result {{\n"
                    f"    int ok;\n"
                    f"    union {{\n"
                    f"        {c['data']} *value;\n"
                    f"        struct yetty_ycore_error error;\n"
                    f"    }};\n"
                    f"}};"),
                f"struct {rid}_result {q}_from(struct yetty_yclass_object *obj);",
            ]
            # Inverse accessor — recover the owning object from a data-slice
            # pointer. Regular classes only (a mixin slice has no invariant
            # offset); see the matching guard in emit_class_accessor.
            if c.get("type") != "mixin":
                lines.append(
                    f"struct yetty_yclass_object_ptr_result {q}_to({c['data']} *data);")
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
        # `expose`d functions defined in this source file — concrete, non-slot
        # public API — and the public types the file authors. Function
        # prototypes go in `exposed_protos`; full struct/enum definitions
        # (authored via `expose` on the type) go in `header_type_texts`.
        exposed_protos = []
        header_type_texts = []
        exposed_type_names = set()   # type tags this header fully defines itself
        proto_struct_types = set()   # struct tags named (any position) in a signature
        byval_type_tags = set()      # struct/enum tags used BY VALUE by a signature
        for e in model.get("exposed", []):
            if e["source_file"] != src_file:
                continue
            if "type_text" in e:
                header_type_texts.append(_doc_prefix(e.get("doc")) + e["type_text"])
                for m in re.finditer(r"\b(?:struct|enum)\s+(\w+)\s*\{", e["type_text"]):
                    exposed_type_names.add(m.group(1))
            else:
                exposed_protos.append(
                    _doc_prefix(e.get("doc"))
                    + _fmt_proto(e["return_type"], e["name"], e["args"]))
                for typ in [e["return_type"]] + [a["type"] for a in e["args"]]:
                    for m in re.finditer(r"struct\s+(\w+)", typ):
                        proto_struct_types.add(m.group(1))
                    # Used BY VALUE (no pointer) → needs the complete type.
                    category, tag = _classify_type(typ)
                    if category in ("struct", "union", "enum") and tag:
                        byval_type_tags.add(tag)

        # Public method-dispatch stubs whose owning class is defined in THIS
        # source. Folded in so the source's single header is its complete public
        # interface — there is no module-wide methods.h.
        group_class_names = {c["name"] for c in classes}
        group_methods = [m for m in model["methods"]
                         if m.get("owning_class") in group_class_names]
        if split:
            # The constructor lifecycle slot is invoked only through the
            # sanctioned creation paths — it is not object API.
            group_methods = [m for m in group_methods if m["slot"] != "constructor"]
        stub_struct_names = set()
        for m in group_methods:
            stub_struct_names |= struct_names_in(m["return_type"])
            for a in m["args"]:
                stub_struct_names |= struct_names_in(a["type"])
            for typ in [m["return_type"]] + [a["type"] for a in m["args"]]:
                category, tag = _classify_type(typ)
                if category in ("struct", "union", "enum") and tag:
                    byval_type_tags.add(tag)
        for known in ("yetty_yclass_ctx", "yetty_yclass_object", "yetty_yclass",
                      "yetty_ycore_buffer"):
            stub_struct_names.discard(known)
        stub_decls = "".join(
            _doc_prefix(m.get("doc"))
            + f"{result_type(m['return_type'])} {qualified_slot(m)}("
            + ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
            + ");\n"
            for m in group_methods)
        # Slot function-pointer typedefs — a subclass that overrides one of
        # these slots includes this header and needs `<slot>_fn` to type-check
        # its impl. Public, alongside the stubs (replaces the old methods.gen.h).
        # Implementation-facing: absent from the role-split object-API header
        # (the impl glue TU emits its own).
        stub_typedefs = "" if split else "".join(
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
        # register() is implementation registration — never part of the
        # role-split object-API header (a serving process forward-declares
        # or uses the impl-side surface).
        register_decl = "" if split else (f"struct yetty_ycore_void_result "
                                          f"yetty_{module}_register(void);\n")

        # Full definitions for module-local struct/enum types used BY VALUE by
        # an exposed function or a method — reproduced whole (transitively, deps
        # first) so a by-value parameter/return has the complete type at every
        # call site. Authoring the type in the owning .c and using it by value
        # is enough to publish it; no annotation on the type is required.
        # Pointer-only uses get a forward declaration below.
        local_type_defs, local_def_names = _local_byvalue_type_defs(
            model, src_file, byval_type_tags,
            exclude=exposed_type_names | data_block_result_tags)
        exposed_type_names |= local_def_names

        # Typedefs (callback function pointers, …) referenced by any emitted
        # prototype, stub or type definition — reproduced so the public header
        # is self-contained, emitted before the structs/functions that use them.
        # local_typedefs is the module-OWNED set (parse-time), so a typedef
        # defined in a SIBLING file (e.g. a mixin's callback type used by a
        # widget's stub, or a callback field type used by a factory-args struct)
        # reproduces here too — the source file it was authored in no longer
        # matters, a header emits whichever owned typedef its own content names.
        # EXCLUDE any name another emission block already DEFINES (the override
        # `_fn` typedefs the stub machinery emits) — a second identical typedef
        # in one header is redundant and trips -Werror on older toolchains.
        local_typedefs = model.get("local_typedefs", {})
        typedef_scan = "\n".join(exposed_protos + header_type_texts + local_type_defs
                                 + [stub_decls, stub_typedefs])
        already_defined = (_typedef_defined_names(stub_typedefs)
                           | _typedef_defined_names(stub_decls))
        typedef_defs = [
            info["text"]
            for name, info in sorted(local_typedefs.items())
            if name not in already_defined
            and re.search(r"\b" + re.escape(name) + r"\b", typedef_scan)]

        # Forward declarations for struct tags named only in pointer position
        # (completeness is only required at the call site and the definition).
        # Skip result/yclass-core types and anything this header fully defines.
        proto_struct_types |= stub_struct_names
        proto_struct_types -= exposed_type_names
        fwd_decls = "".join(
            f"struct {n};\n" for n in sorted(proto_struct_types)
            if not n.endswith("_result") and not n.startswith("yetty_yclass"))

        # Header-destined #include directives (`include@<path>`) for by-value
        # foreign types the public signatures need complete.
        extra_includes = "".join(
            f"#include <{inc}>\n"
            for inc in model.get("includes", {}).get(src_file, []))

        # Public type region, ordered for C: forward decls, then typedefs, then
        # the full struct/enum definitions (already dependency-ordered).
        type_block = ""
        if fwd_decls:
            type_block += fwd_decls + "\n"
        if typedef_defs:
            type_block += "\n".join(typedef_defs) + "\n\n"
        # Exposed type definitions (`header_type_texts`, e.g. a struct returned
        # by value) must precede the by-value dependency defs (`local_type_defs`),
        # because a generated `<type>_result` wrapper in the latter embeds such an
        # exposed type by value and needs its complete definition first.
        full_defs = [_guard_type_def(text)
                     for text in header_type_texts + local_type_defs]
        if full_defs:
            type_block += "\n".join(full_defs) + "\n\n"

        rpc_decls = ""
        if stub_decls:
            rpc_decls += "\n\n" + stub_decls.rstrip()
        if stub_typedefs:
            rpc_decls += "\n\n" + stub_typedefs.rstrip()
        if create_decls:
            rpc_decls += "\n\n" + create_decls.rstrip()
        rpc_decls += "\n\n" + register_decl.rstrip()

        method_decls = ""
        if exposed_protos:
            method_decls += "\n\n" + "\n".join(exposed_protos)

        kind = "mixin" if all(c['type'] == "mixin" for c in classes) else "regular class"
        # Class-name list for the file header banner.
        name_list = ", ".join(c["name"] for c in classes)
        if split:
            banner = (
                f"/* Object API for {kind}(es) `{name_list}` "
                f"(implementation module: {module}).\n"
                " * Fully generated from the source .c — do not edit. The API does\n"
                " * not encode whether an implementation dispatches in-process or\n"
                " * over RPC; it declares the typed methods, create(), properties,\n"
                " * exposed functions, and the types those signatures use. */\n")
        else:
            banner = (
                f"/* Public interface for {kind}(es) `{name_list}` "
                f"(module: {module}).\n"
                " * Fully generated from the source .c — do not edit. This single\n"
                " * header is the source's complete public interface: class\n"
                " * accessors, method stubs, create()/register(), exposed\n"
                " * functions, and the public types the signatures use. */\n")
        body = (
            HEADER
            + banner
            + f"#ifndef {guard}\n#define {guard}\n\n"
            + '#include <yetty/yclass/class.h>\n'
            + '#include <yetty/yclass/rpc.h>\n'
            + '#include <yetty/ycore/result.h>\n'
            + '#include <yetty/ycore/types.h>\n'
            + extra_includes + "\n"
            + "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
            + type_block
            + decls + data_decls + property_decls + rpc_decls + method_decls + "\n\n"
            + "#ifdef __cplusplus\n}\n#endif\n\n"
            + "#endif\n"
        )
        _write_atomic(header_path, body)

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


def emit_class_gen_c(model: dict, module: str, module_dir: Path, split: bool = False,
                     gen_root: Path = None):
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

    # Modules whose sources share a filename stem across subdirs (only
    # yplatform) emit registration as standalone per-submodule aggregators
    # instead of a per-source folded hook — see the split branch below.
    module_has_dup_stems = _module_has_duplicate_stems(model)

    # Route each owned method (its public stub + server skel) and each regular
    # class (its factory) to the <stem>.gen.c of the owning class's source, so
    # every class carries its own caller surface, server skels and factory —
    # there is no module-wide methods.gen.c. The one irreducibly module-level
    # artefact (name->accessor lookup, slot->skel table, yetty_<module>_register)
    # is emitted SEPARATELY into rpc.gen.c by emit_rpc_aggregator_c, NOT folded
    # into a class stem: its lookup tables name every class accessor + skel, so
    # co-locating it with a class .o would force the linker to pull in every
    # other class (and its heavy deps) whenever that .o is linked — even by a
    # minimal consumer. Its own TU stays unreferenced until register() is used.
    cls_source = {(c["domain"], c["name"]): c["source_file"]
                  for c in model["classes"]}
    methods_by_source: dict = {}
    for m in model.get("methods", []):
        src = cls_source.get((m["domain"], m["owning_class"]))
        if src is not None:
            methods_by_source.setdefault(src, []).append(m)

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
                needed.add(_class_header_lookup(model, p, fallback_dom=p['domain'],
                                                module_src=module_dir, split=split))
            for mx in c.get("mixins", []):
                needed.add(_class_header_lookup(model, mx, fallback_dom=mx['domain'],
                                                module_src=module_dir, split=split))
        include_block = "".join(f'#include "{h}"\n' for h in sorted(needed))
        # The accessor body emits ydebug() and YETTY_OK / YETTY_ERR
        # which expand to ycore + ytrace primitives. Pull those in
        # explicitly so a per-class .gen.c is self-sufficient when
        # included from a hand-written .c that hasn't imported them
        # yet (the hand-written .c may include only its module-local
        # header for backward compat).
        # The stubs/skels/factories/aggregator folded into each <stem>.gen.c need
        # the RPC runtime, buffer/container_of helpers and the C stdlib used for
        # marshalling.
        include_block += (
            '#include <yetty/yclass/rpc.h>\n'
            '#include <yetty/ycore/result.h>\n'
            '#include <yetty/ycore/types.h>  /* container_of, buffer */\n'
            '#include <yetty/ytrace/ytrace.h>\n'
            '#include <stdbool.h>\n'
            '#include <stddef.h>  /* NULL, size_t */\n'
            '#include <stdint.h>\n'
            '#include <stdio.h>  /* stderr */\n'
            '#include <stdlib.h>  /* calloc/free for proxy + buffer marshalling */\n'
            '#include <string.h>  /* memcpy/strcmp/strlen */\n'
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

        accessor_block = "\n".join(emit_class_accessor(c) for c in classes)

        # Caller stubs + server skels for the slots THIS stem's classes own, plus
        # the factory for each regular class — all folded into this <stem>.gen.c.
        # In split mode the caller stubs move OUT into a standalone
        # <stem>.call.gen.c (compiled on its own, linked by pure clients and by
        # the implementation library alike), and the `constructor` lifecycle
        # slot gets no skel — it is never remotely callable.
        own_methods = methods_by_source.get(src_path, [])
        stub_block = "".join(emit_stub_def(m, split) for m in own_methods)
        skel_block = "".join(
            emit_skel(m) + "\n" for m in own_methods
            if not m.get("local") and not (split and m["slot"] == "constructor"))
        # Local create prototype before each factory so the .gen.c is self-
        # sufficient (the matching decl also lives in the class's public .h,
        # but that header is not re-included here — see above). Redeclaration
        # with an identical signature is well-formed.
        regular_in_stem = [c for c in classes if c.get("type") == "regular"]
        create_block = "".join(
            f"struct yetty_yclass_object_ptr_result "
            f"{qualified_class(c)}_create(struct yetty_yclass_ctx *ctx);\n"
            + emit_create_fn(c, model, module, split) + "\n"
            for c in regular_in_stem)
        # If any factory in this stem calls the module's constructor stub
        # yetty_<module>_constructor (i.e. a regular class here carries the
        # constructor slot in its resolved dispatch), forward-declare it — the
        # stub is owned by (and defined in) another class's stem. Gate on the
        # same per-class condition emit_create_fn uses, so a stem whose classes
        # have no constructor slot (e.g. yrich:app) emits no dangling decl.
        stem_calls_ctor = any(
            op["slot_domain"] == module and op["slot"] == "constructor"
            for c in regular_in_stem for op in c["ops"])
        if regular_in_stem and stem_calls_ctor:
            create_block = (
                "struct yetty_ycore_void_result "
                f"yetty_{module}_constructor(struct yetty_yclass_object *obj);\n"
                + create_block)

        if split:
            # Role-split layout: raw annotated source, generated API, and
            # generated implementation glue live in three separate places
            # (the directory IS the role boundary — no filename suffixes):
            #
            #   src/yetty/<module>/<stem>.c            hand-written source
            #   src/yetty/gen/api/<api>/<stem>.c       object-API stubs
            #   src/yetty/gen/impl/<module>/<stem>.c   impl-bound glue
            #   include/yetty/api/<api>/<stem>.h       object-API header
            #
            # The API TU defines every typed method stub EXCEPT the
            # constructor (a lifecycle slot invoked only through creation) —
            # the object API does not encode whether an implementation
            # dispatches locally or over RPC. It has no strong reference to
            # the implementation, the class accessor, the skels or the
            # registration. The impl TU (#included from the hand-written
            # source, which names its static impl functions) carries the
            # accessor, check shims, constructor stub, skels, factory and
            # this source's registration hook — there is no module-level
            # rpc.gen.c in split mode.
            api_name = api_namespace(module)  # api/<api_name> (facade drops api_ prefix)
            # The generated .c tree is ALWAYS the shared src/yetty/gen — pinned
            # by the caller (gen_root), so an OUT-OF-TREE module (tools/*, demo/*)
            # emits into src/yetty/gen, not a stray tools/gen beside its source.
            # For in-tree modules this equals module_dir.parent / "gen".
            gen_base = gen_root if gen_root is not None else module_dir.parent / "gen"
            impl_dir = gen_base / "impl" / module / source_rel_subdir(src_path, module_dir)
            api_dir = gen_base / "api" / api_name / source_rel_subdir(src_path, module_dir)
            impl_dir.mkdir(parents=True, exist_ok=True)
            api_dir.mkdir(parents=True, exist_ok=True)

            api_methods = [m for m in own_methods if m["slot"] != "constructor"]
            ctor_methods = [m for m in own_methods if m["slot"] == "constructor"]
            api_stub_block = "".join(emit_stub_def(m, split) for m in api_methods)
            ctor_stub_block = "".join(emit_stub_def(m, split) for m in ctor_methods)

            api_runtime_includes = (
                '#include <yetty/yclass/rpc.h>\n'
                '#include <yetty/ycore/result.h>\n'
                '#include <yetty/ycore/types.h>  /* container_of, buffer */\n'
                '#include <yetty/ytrace/ytrace.h>\n'
                '#include <stdbool.h>\n'
                '#include <stddef.h>  /* NULL, size_t */\n'
                '#include <stdint.h>\n'
                '#include <stdio.h>  /* stderr */\n'
                '#include <stdlib.h>  /* malloc/free for buffer marshalling */\n'
                '#include <string.h>  /* memcpy/strlen */\n'
            )
            api_header_rel = (Path("yetty/api") / api_name
                              / source_rel_subdir(src_path, module_dir)
                              / f"{src_path_p.stem}.h")
            api_body = (HEADER
                        + f'#include <{api_header_rel.as_posix()}>\n\n'
                        + api_runtime_includes + "\n" + slot_block
                        + api_stub_block)
            _write_atomic(api_dir / f"{src_path_p.stem}.c", api_body)

            # This source's registration hook (name→accessor + slot→skel
            # lookups) folds into the impl TU — the per-class generated
            # implementation owns its registration records. A single-source
            # module's hook IS the module register; a multi-source module
            # gets one per-source hook (yetty_<module>_<stem>_register) plus
            # the module-level yetty_<module>_register() chaining them all,
            # emitted into the first source (sorted by stem) so existing
            # callers keep their one registration call.
            #
            # A module with duplicate source stems (yplatform's per-OS
            # variants: yclipboard/glfw.c, ywindow/glfw.c, …) can't fold a
            # per-source register — the yetty_<module>_glfw_register symbols
            # would collide. Its registration is emitted per-submodule into
            # standalone rpc.gen.c aggregators (emit_submodule_register_tus,
            # called from main()); the impl TU carries only accessor/skel/
            # factory, no registration block.
            if module_has_dup_stems:
                registration_block = ""
            else:
                all_stems = sorted(Path(p).stem for p in groups)
                if len(all_stems) == 1:
                    registration_block = emit_group_aggregator(
                        classes, own_methods, module, (), split)
                else:
                    source_group = re.sub(r"\W", "_", f"{module}_{src_path_p.stem}")
                    registration_block = emit_group_aggregator(
                        classes, own_methods, source_group, (), split)
                    if src_path_p.stem == all_stems[0]:
                        chain_groups = [re.sub(r"\W", "_", f"{module}_{stem}")
                                        for stem in all_stems]
                        registration_block += "\n" + emit_group_aggregator(
                            [], [], module, chain_groups, split)
            impl_body = (HEADER + include_block + "\n" + slot_block
                         + accessor_block + "\n\n"
                         + ctor_stub_block + skel_block + create_block
                         + "\n" + registration_block)
            _write_atomic(impl_dir / f"{src_path_p.stem}.c", impl_body)
        else:
            body = (HEADER + include_block + "\n" + slot_block
                    + accessor_block + "\n\n"
                    + stub_block + skel_block + create_block)
            _write_atomic(inc_path, body)


# ---------------- rpc.gen.{h,c} ------------------------------------------

def regular_classes(model: dict) -> list:
    return [c for c in model.get("classes", []) if c.get("type") == "regular"]


def source_rel_subdir(source_file, module_src: Path) -> Path:
    """Subdir of an annotated source relative to the module's source root,
    preserving every nesting level. `module_src` is the third codegen argument
    (e.g. src/yetty/yplatform, or tools/ai/yai for a relocated module), so this
    works regardless of how deeply the module is nested or where it lives — no
    hardcoded `src/yetty/<module>` assumption. Falls back to the module root
    (flat) if the source somehow lies outside module_src."""
    try:
        return Path(source_file).resolve().relative_to(module_src.resolve()).parent
    except ValueError:
        return Path(".")


def _class_header_lookup(model: dict, ref: dict, fallback_dom: str, module_src: Path,
                         split: bool = False) -> str:
    """Resolve a `{domain, name}` parent/mixin reference to its
    include-path. For same-module refs we use the model to find the
    source's subdir; for cross-module refs we fall back to the bare
    `yetty/<dom>/<name>.h` path (the foreign module's codegen owns
    that file's actual layout).

    In split mode the parent/mixin surface an override needs (class
    accessor, slot stubs, `_fn` typedefs) is implementation-facing and
    lives in the generated impl header under src/yetty/gen/impl/ —
    which requires the referenced module to already be on the split
    layout (migration order: parents before subclasses). Cross-module
    refs assume the parent's source stem equals the class name, the
    convention every migrated module follows."""
    dom = ref.get("domain", fallback_dom)
    name = ref["name"]
    if split:
        for c in model.get("classes", []):
            if c["domain"] == dom and c["name"] == name:
                src_path = Path(c["source_file"]).resolve()
                rel = source_rel_subdir(c["source_file"], module_src)
                rel_subdir = "" if str(rel) == "." else str(rel) + "/"
                return f"yetty/gen/impl/{dom}/{rel_subdir}{src_path.stem}.h"
        return f"yetty/gen/impl/{dom}/{name}.h"
    for c in model.get("classes", []):
        if c["domain"] == dom and c["name"] == name:
            return class_header_for(c, dom, module_src)
    return f"yetty/{module_include_subpath(dom)}/{name}.h"


def module_include_subpath(module: str) -> str:
    """Header directory for a module, relative to include/yetty. Normally the
    module name, but an `api_<name>` public-facade module nests as `api/<name>`
    to mirror its src/api/<name>/ sources and the yetty.api.<name> bindings —
    so `api_yplot` headers live under include/yetty/api/yplot/, not api_yplot/.
    The C symbol prefix stays `yetty_api_yplot_*` (that is the domain); only the
    on-disk header layout is nested."""
    if module.startswith("api_"):
        return "api/" + module[len("api_"):]
    return module


def api_namespace(module: str) -> str:
    """The object-API namespace segment for a module — the directory name under
    include/yetty/api/ and src/yetty/gen/api/. Normally the module name; an
    api_<name> facade drops the redundant `api_` prefix so its object API lands
    in api/<name> (matching the long-standing include/yetty/api/yplot layout),
    not api/api_yplot. For a non-facade module this is the module name
    unchanged, so every other module's paths stay byte-identical."""
    return module[len("api_"):] if module.startswith("api_") else module


def class_header_for(cls: dict, module: str, module_src: Path) -> str:
    """Public header path for a class. The file is named after the annotated
    source's stem, under a subdir that mirrors the source's location relative to
    the module's source root — every nesting level preserved (widgets/button.c →
    yetty/<module>/widgets/button.h, a/b/foo.c → yetty/<module>/a/b/foo.h,
    primitive-widget.c → yetty/<module>/primitive-widget.h). Codegen owns the
    file; hand-written helper declarations are preserved across regenerations
    via the MANUAL markers in emit_class_public_headers."""
    src_path = Path(cls["source_file"]).resolve()
    rel = source_rel_subdir(cls["source_file"], module_src)
    rel_subdir = "" if str(rel) == "." else str(rel) + "/"
    return f"yetty/{module_include_subpath(module)}/{rel_subdir}{src_path.stem}.h"


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
    vt = m["return_payload_type"]
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
    # The resolved server-side object is local (session NULL), so re-entering
    # the public stub dispatches in-process. No ctx argument — the stub reads
    # the session off the object.
    call_parts = []
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
        if (resp_max < 1) {{
            yetty_ycore_error_destroy({var}.error);
            return 0;
        }}
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize({var}.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy({var}.error);
        return 1 + err_bytes;
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

    # On error: log to the server, then write status=1 followed by the whole
    # serialized cause chain so the client can rebuild it (the value path is
    # symmetric — status=0 followed by the raw value bytes). If the chain does
    # not fit the response buffer, serialize returns 0 and we ship status-only.
    err_response = f"""\
    if (YETTY_IS_ERR(call_r)) {{
        yetty_ycore_error_print(stderr, "[skel] {slot}", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }}"""

    rt = f"struct {rid}_result"
    if vt is None:
        body = f"""\
    {rt} call_r = {slot}({call});
    if (resp_max < 1) return 0;
{err_response}
    ((uint8_t *)resp)[0] = 0;
    return 1;
"""
    elif rid == "yetty_yclass_object_ptr":
        # OBJECT REFERENCE return: register the returned object (dedup — a
        # repeated navigation returns the same handle) and ship the u64.
        # NULL object ships handle 0.
        body = f"""\
    {rt} call_r = {slot}({call});
    if (resp_max < 1 + sizeof(uint64_t)) return 0;
{err_response}
    uint64_t object_handle = 0;
    if (call_r.value) {{
        struct yetty_yclass_handle_result handle_r =
            yetty_yclass_rpc_register_object_dedup(call_r.value);
        if (YETTY_IS_ERR(handle_r)) {{
            yetty_ycore_error_print(stderr, "[skel] {slot}: register returned object",
                                    handle_r.error);
            ((uint8_t *)resp)[0] = 1;
            size_t err_bytes = yetty_ycore_error_serialize(handle_r.error, (uint8_t *)resp + 1,
                                                           resp_max - 1);
            yetty_ycore_error_destroy(handle_r.error);
            return 1 + err_bytes;
        }}
        object_handle = handle_r.value;
    }}
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &object_handle, sizeof(object_handle));
    return 1 + sizeof(object_handle);
"""
    else:
        body = f"""\
    {rt} call_r = {slot}({call});
    if (resp_max < 1) return 0;
{err_response}
    if (resp_max < 1 + sizeof(call_r.value)) return 0;
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
"""

    return f"""\
/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t {slot}_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t {slot}_skel(const void *body, size_t body_len,
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
{resolve_block}\
{body}}}
"""


def emit_create_fn(cls: dict, model: dict, module: str, split: bool = False) -> str:
    """Per-class factory. ctx decides; caller is location-agnostic.

    If *this class* carries the module's `constructor` slot in its
    resolved dispatch (its own override, or one inherited from a
    same-module ancestor), the factory's local branch invokes that
    slot automatically after allocating — so the returned object is
    fully constructed in one call. No separate _init step.

    The check is per-class, not per-module: a class that lives in the
    module but inherits from a foreign-module parent (e.g. yrich:app,
    parented on yapp:app) and defines no constructor has no such slot
    in its dispatch table, so it must NOT emit the auto-call — doing so
    would dispatch into an empty slice and fail at runtime."""
    accessor = cls["accessor"]
    qname = qualified_class(cls)
    has_constructor = any(
        op["slot_domain"] == module and op["slot"] == "constructor"
        for op in cls["ops"]
    )
    if has_constructor:
        ctor_call = f"""\

        struct yetty_ycore_void_result ctor_r =
            yetty_{module}_constructor(alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {{
            struct yetty_ycore_void_result free_r =
                yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) yetty_ycore_error_destroy(free_r.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "{qname}_create: constructor failed", ctor_r);
        }}"""
    else:
        ctor_call = ""
    if split:
        # Split-mode factory: LOCAL construction only, living with the
        # implementation. Remote consumers never call a factory — they wrap
        # a server-minted handle via GET_ROOT/CREATE +
        # yetty_yclass_object_proxy_create(session, handle, NULL). Keeping
        # the remote branch out of the factory keeps translate_class and
        # the wire CREATE handshake out of the implementation TU.
        return f"""\
struct yetty_yclass_object_ptr_result {qname}_create(struct yetty_yclass_ctx *ctx)
{{
    ydebug("class={qname}");
    if (ctx && ctx->session)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "{qname}_create: remote create unsupported for a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    struct yetty_yclass_ptr_result class_accessor_r = {accessor}();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "{qname}_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r =
        yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) return alloc_r;{ctor_call}
    return alloc_r;
}}
"""
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
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}}
"""


def _submodule_of(source_file, module_src) -> str:
    """The submodule a class source belongs to: the first path component of its
    source RELATIVE to the module dir, or "" when the source sits directly in
    the module dir (the module root itself). e.g.
    src/yetty/yplatform/ywindow/glfw.c -> "ywindow";
    src/yetty/yplatform/window-manager.c -> ""."""
    try:
        rel = Path(source_file).resolve().relative_to(Path(module_src).resolve())
    except (ValueError, OSError):
        return ""
    return rel.parts[0] if len(rel.parts) > 1 else ""


def emit_rpc_aggregator_c(classes, methods, group, chain_registers, out_path, split=False):
    """rpc.gen.c for ONE registration group — a module root or one of its
    submodule subdirs. Holds ONLY the registration glue for the classes in THIS
    group: the name->accessor lookup, the slot->skel table, and
    yetty_<group>_register(). The module-root register chains its submodule
    registers so existing callers of yetty_<module>_register() still register
    everything. Kept in its own TU (never folded into a class stem) because its
    tables name every class in the group; as its own .o it's pulled only when
    register() is actually called."""
    _write_atomic(out_path,
        HEADER
        + '#include <yetty/yclass/rpc.h>\n'
        + '#include <yetty/ycore/result.h>\n'
        + '#include <yetty/ytrace/ytrace.h>\n'
        + '#include <yetty/yclass/class.h>\n'
        + '#include <stdbool.h>\n#include <stddef.h>\n#include <string.h>\n\n'
        + emit_group_aggregator(classes, methods, group, chain_registers, split)
    )


def _module_has_duplicate_stems(model) -> bool:
    """True when two annotated sources of the module share a filename stem —
    yplatform's per-OS variants (yclipboard/glfw.c, ywindow/glfw.c, …) are the
    only case. Such a module cannot fold a per-source
    yetty_<module>_<stem>_register into each impl TU (the symbols collide), so
    its registration is emitted as standalone per-submodule rpc.gen.c
    aggregators instead — the same layout the legacy generator uses."""
    sources = {c["source_file"] for c in model["classes"]}
    stems = [Path(s).stem for s in sources]
    return len(stems) != len(set(stems))


def emit_submodule_register_tus(model, module, module_src, out_dir, split):
    """Standalone registration TUs: one rpc.gen.c per submodule subdir
    (registering that subdir's classes) plus the module-root rpc.gen.c that
    registers the root classes and chains the submodule registers, so callers
    of yetty_<module>_register() still register everything. `module_src` locates
    each source's submodule; `out_dir` is where the rpc.gen.c files are written
    (the module source tree for legacy modules, the gen/impl tree in split
    mode)."""
    cls_source = {(c["domain"], c["name"]): c["source_file"]
                  for c in model["classes"]}
    sub_groups: dict = {}
    for c in model["classes"]:
        sub = _submodule_of(c["source_file"], module_src)
        sub_groups.setdefault(sub, {"classes": [], "methods": []})["classes"].append(c)
    for m in model.get("methods", []):
        src = cls_source.get((m["domain"], m["owning_class"]))
        sub = _submodule_of(src, module_src) if src else ""
        sub_groups.setdefault(sub, {"classes": [], "methods": []})["methods"].append(m)

    def group_symbol(submodule):
        return re.sub(r"\W", "_", f"{module}_{submodule}") if submodule else module

    submodules = sorted(s for s in sub_groups if s)
    for sub in submodules:
        bucket = sub_groups[sub]
        (out_dir / sub).mkdir(parents=True, exist_ok=True)
        emit_rpc_aggregator_c(bucket["classes"], bucket["methods"],
                              group_symbol(sub), (),
                              out_dir / sub / "rpc.gen.c", split)
    root = sub_groups.get("", {"classes": [], "methods": []})
    out_dir.mkdir(parents=True, exist_ok=True)
    emit_rpc_aggregator_c(root["classes"], root["methods"], module,
                          [group_symbol(s) for s in submodules],
                          out_dir / "rpc.gen.c", split)


def _platform_macro(platform):
    return "YETTY_PLATFORM_" + re.sub(r"\W", "_", platform).upper()


def _platform_guard(text, platform):
    """Wrap a registration fragment in the class's platform #ifdef, or return
    it unchanged for a cross-platform class. Codegen runs once and emits every
    class; the #ifdef (in the REGISTRATION code only) leaves just the active
    platform's classes — the class .c is compiled only on that platform by
    CMake, so the guarded reference is always defined there."""
    if not platform:
        return text
    return f"#ifdef {_platform_macro(platform)}\n{text}#endif\n"


def emit_group_aggregator(classes, methods, group, chain_registers, split=False) -> str:
    """Guarded forward decls + lookup tables + yetty_<group>_register()
    for the classes of one registration group. In split mode the
    `constructor` lifecycle slot has no skel (never remotely callable), so
    it is absent from the proto list and the skel table alike."""
    cls_platform = {c["name"]: c.get("platform") for c in classes}
    accessor_protos = "".join(
        _platform_guard(
            f"struct yetty_yclass_ptr_result {c['accessor']}(void);\n",
            c.get("platform"))
        for c in classes)
    skel_protos = "".join(
        _platform_guard(
            f"size_t {qualified_slot(m)}_skel(const void *, size_t, void *, size_t);\n",
            cls_platform.get(m["owning_class"]))
        for m in methods
        if not m.get("local") and not (split and m["slot"] == "constructor"))
    chain_protos = "".join(
        f"struct yetty_ycore_void_result yetty_{cg}_register(void);\n"
        for cg in chain_registers)
    register_proto = f"struct yetty_ycore_void_result yetty_{group}_register(void);\n"
    forward = (
        "/* Forward decls. A class tagged platform@<x> is registered only on\n"
        " * that platform: its accessor/skel decls and its registration entry\n"
        " * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the\n"
        " * class .c. A cross-platform class is a plain strong ref, defined in\n"
        " * the same library and pulled in when register() is. Submodule\n"
        " * registers are chained as strong externs (always co-linked). */\n"
        + accessor_protos + skel_protos + chain_protos + register_proto + "\n"
    )
    if split:
        # The skel table below must match the proto list: no constructor row.
        methods = [m for m in methods if m["slot"] != "constructor"]
    return forward + emit_lookup_tables(classes, methods, group, chain_registers)


def emit_lookup_tables(classes, methods, group, chain_registers=()) -> str:
    cls_platform = {c["name"]: c.get("platform") for c in classes}

    def accessor_branch(c):
        platform = c.get("platform")
        # Strong ref. A platform class is wrapped in its #ifdef (compiled by
        # CMake only on that platform); a cross-platform class is always
        # defined in the same library. Either way the symbol is present.
        body = (f'    if (strcmp(name, "{qualified_class(c)}") == 0)\n'
                f'        return {c["accessor"]}();\n')
        return _platform_guard(body, platform)

    accessor_section = ""
    if classes:
        accessor_section = f"""\
/* ---- {group}: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_{group}_accessor_lookup(const char *name)
{{
{"".join(accessor_branch(c) for c in classes)}\
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}}
"""

    wire_methods = [m for m in methods if not m.get("local")]
    if not wire_methods:
        skel_install = ""
        skel_section = ""
    else:
        skel_rows = "".join(
            _platform_guard(
                f'    {{"{qualified_slot(m)}", {qualified_slot(m)}_skel}},\n',
                cls_platform.get(m["owning_class"]))
            for m in wire_methods)
        skel_section = f"""\

/* ---- {group}: slot -> skel, name-keyed static data --------------- */

struct yetty_{group}_skel_row {{ const char *name; yetty_yclass_rpc_skel_fn fn; }};

static const struct yetty_{group}_skel_row yetty_{group}_skel_rows[] = {{
{skel_rows}}};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_{group}_skel_lookup(yetty_yclass_method_slot slot)
{{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {{ yetty_ycore_error_destroy(slot_name_r.error); return NULL; }}
    const char *name = slot_name_r.value;
    for (size_t i = 0;
         i < sizeof(yetty_{group}_skel_rows) / sizeof(yetty_{group}_skel_rows[0]); ++i)
        if (strcmp(yetty_{group}_skel_rows[i].name, name) == 0)
            return yetty_{group}_skel_rows[i].fn;
    return NULL;
}}
"""
        skel_install = (
            f"    {{\n"
            f"        struct yetty_ycore_void_result add_skel_r =\n"
            f"            yetty_yclass_rpc_add_skel_lookup(yetty_{group}_skel_lookup);\n"
            f"        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,\n"
            f'                            "yetty_{group}_register: rpc_add_skel_lookup");\n'
            f"    }}\n"
        )

    accessor_install = ""
    if classes:
        accessor_install = (
            f"    struct yetty_ycore_void_result add_accessor_r =\n"
            f"        yetty_yclass_add_accessor_lookup(yetty_{group}_accessor_lookup);\n"
            f"    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,\n"
            f'                        "yetty_{group}_register: add_accessor_lookup");\n')

    chain_block = "".join(
        f"    {{\n"
        f"        /* Submodule aggregator is always compiled into the same\n"
        f"         * library, so this strong call is always resolved. */\n"
        f"        struct yetty_ycore_void_result sub_r = yetty_{cg}_register();\n"
        f"        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,\n"
        f'                            "yetty_{group}_register: submodule {cg}");\n'
        f"    }}\n"
        for cg in chain_registers)

    return f"""\
{accessor_section}\
{skel_section}\

/* ---- {group}: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_{group}_register(void)
{{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

{accessor_install}\
{skel_install}\
{chain_block}\
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
    _write_atomic(out_path, 
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


# ---------------- main ---------------------------------------------------

def main():
    # --headers-local writes each class's generated public header next to its
    # annotated source (the same directory as the .c / .gen.c) instead of under
    # <include_base>/<module>/. Used for modules whose sources live OUTSIDE the
    # shared include tree (e.g. the ygui demos under demo/ygui/), so each demo
    # directory stays self-contained rather than scattering headers into include/.
    argv = sys.argv[1:]
    headers_local = "--headers-local" in argv
    argv = [a for a in argv if a != "--headers-local"]
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        sys.exit(2)
    module = argv[0]
    include_base = Path(argv[1])
    module_src = Path(argv[2])
    sources = [Path(p) for p in argv[3:]]

    include_module = include_base / module_include_subpath(module)
    module_src.mkdir(parents=True, exist_ok=True)

    # Role-split layout — the ONLY layout. Raw annotated source, generated
    # object API and generated implementation glue live in three separate
    # roots (the directory is the role boundary):
    #   src/yetty/<module>/<stem>.c            hand-written source
    #   src/yetty/gen/api/<api>/<stem>.c       object-API stubs
    #   src/yetty/gen/impl/<module>/<stem>.c   impl glue (#included by the
    #                                          hand-written source)
    #   include/yetty/api/<api>/<stem>.h       the ONE public header
    # Remote slots resolve by canonical qualified name (no client-side slot
    # table), the factory is local-only, the constructor lifecycle slot gets
    # no skel, and there is no module-level rpc.gen.c. The pre-split legacy
    # layout is retired; the `split` conditionals below are dead branches
    # kept only until the next mechanical sweep deletes them.
    split = True

    # Pre-touch placeholders so clang -fsyntax-only can resolve the
    # #includes the annotated sources pull in before the real generated
    # content has been emitted on the very first invocation. Per-class
    # headers seed with `#include <yetty/yclass/class.h>` so the runtime structs
    # (ctx, object, class) are visible during AST parsing.
    placeholder_class_h = '#include <yetty/yclass/class.h>\n'
    # The role-split generated .c tree is ALWAYS the shared src/yetty/gen,
    # regardless of where the module's annotated sources live — derived from
    # include_base (<root>/include/yetty) so an out-of-tree module (tools/*,
    # demo/*, src/api/*) does not emit a stray <its-parent>/gen. For an in-tree
    # module this equals module_src.parent / "gen".
    gen_root = include_base.parent.parent / "src" / "yetty" / "gen"
    for s in sources:
        if s.suffix == ".c":
            # Place the per-source generated impl file where the
            # hand-written .c's foot #include expects it: next to the
            # source in the legacy layout, under src/yetty/gen/impl/ in
            # the role-split layout.
            if split:
                inc = (gen_root / "impl" / module
                       / source_rel_subdir(s, module_src) / f"{s.stem}.c")
                inc.parent.mkdir(parents=True, exist_ok=True)
            else:
                inc = s.with_name(s.stem + ".gen.c")
            if not inc.exists():
                _write_atomic(inc, "")
            # Pre-touch the per-class header. We don't know the class name (and a
            # source can define more than one) at this point, so we use the file
            # stem as a best-effort placeholder for first parse.
            # emit_class_public_headers may pick a different name later; the
            # orphan stub is harmless. With --headers-local the header sits next
            # to the source; otherwise at the source-mirrored subdir under
            # include/yetty/<module>/. Role-split modules need NO pre-touch:
            # their object-API header lives under include/yetty/api/<api>/ and
            # the hand-written source never includes it — nothing under
            # include/yetty/<module>/ must exist (or be created) for them.
            if split:
                continue
            if headers_local:
                hdr = s.with_name(s.stem + ".h")
            else:
                rel_subdir = source_rel_subdir(s, module_src)
                hdr_dir = include_module / rel_subdir
                hdr_dir.mkdir(parents=True, exist_ok=True)
                hdr = hdr_dir / (s.stem + ".h")
            if not hdr.exists():
                _write_atomic(hdr, placeholder_class_h)
    # Remove stale module-wide method headers — slot stubs + `_fn` typedefs
    # now live in each source's own <stem>.h (and inline in methods.gen.c).
    for stale in (module_src / "methods.gen.h", include_module / "methods.h"):
        if stale.exists():
            stale.unlink()

    # Clang search path, all derived from the two path arguments — no
    # environment variable needed. `include_base` is <root>/include/yetty and
    # `module_src` is <root>/src/yetty/<module>, so the project include/ and
    # src/ roots (which resolve the `<yetty/...>` includes) are their parents.
    project_include_root = include_base.parent  # <root>/include
    project_src_root = module_src.parent.parent  # <root>/src
    model = parse_sources(
        [project_include_root, include_base, include_module,
         project_src_root, module_src],
        sources, module)
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

    emit_class_public_headers(model, module, include_module, module_src, headers_local,
                              split)
    # The implementation-facing header: full legacy-shaped surface (class
    # accessor, from/to + property glue decls, slot stubs + `_fn` override
    # typedefs, create, register) emitted UNDER THE IMPL ROOT —
    # src/yetty/gen/impl/<module>/<stem>.h. Its consumers are impl-role by
    # definition: cross-module subclasses (their generated impl glue
    # includes the parent's impl header) and serving hosts (register()).
    # The include path lives under src/, so nothing under include/ ever
    # exposes it.
    if split:
        emit_class_public_headers(model, module,
                                  gen_root / "impl" / module,
                                  module_src, headers_local)
    # Compatibility headers for a module with un-flipped dependents
    # (YCLASS_COMPAT_HEADER=1, per-module in driver.py): ALSO emit the
    # legacy-content header at the legacy path
    # include/yetty/<module>/<stem>.h. Hand-written consumers that still
    # include that path (and serving hosts that take register()'s
    # declaration from it) keep compiling until they move to
    # include/yetty/api/. The compat header dies per-module once its last
    # dependent flips.
    if split and os.environ.get("YCLASS_COMPAT_HEADER", "") == "1":
        emit_class_public_headers(model, module, include_module, module_src,
                                  headers_local)

    # The public stubs and the server skels + factories are emitted by
    # emit_class_gen_c — into each class's own <stem>.gen.c in the legacy
    # layout, or into the role-split gen/api + gen/impl roots (see the
    # layout note at the top of main).
    emit_class_gen_c(model, module, module_src, split, gen_root)
    # The registration glue lives in rpc.gen.c — one PER SUBMODULE (each subdir of
    # the module), registering only that subdir's classes. The module-root
    # rpc.gen.c registers the root classes and chains the submodule registers, so
    # existing callers of yetty_<module>_register() still register everything.
    # Role-split modules normally emit NO rpc.gen.c: each generated impl file owns
    # its own registration hook (emitted by emit_class_gen_c). The exception is a
    # split module with duplicate source stems (yplatform), whose per-source hooks
    # would collide — it emits the same standalone per-submodule aggregators, but
    # into the gen/impl tree.
    if not split:
        emit_submodule_register_tus(model, module, module_src, module_src, False)
    elif _module_has_duplicate_stems(model):
        emit_submodule_register_tus(
            model, module, module_src,
            gen_root / "impl" / module, True)

    _write_atomic(module_src / "model.yaml", yaml_dump(model) + "\n")


if __name__ == "__main__":
    main()
