#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["libclang>=16", "pyyaml"]
# ///
"""symbol-graph.py — cross-module symbol-use analyzer.

Walks every TU in the compile database (src/yetty + tools + test + demo)
with clang.cindex and records, per TU, which project symbols it defines
and which it uses. An aggregation pass then reports, per module:

  private_candidates — symbols declared in include/yetty/<mod>/ but
                       referenced only by <mod>'s own TUs → safe to move
                       into src/yetty/<mod>/.
  leaks              — symbols declared in src/yetty/<mod>/ but referenced
                       from outside <mod> → must be promoted to
                       include/yetty/<mod>/.
  public_used        — symbols declared in include/yetty/<mod>/ with
                       genuine external consumers (the real inter-module
                       API).

Tracked symbol kinds: functions, types (struct/union/enum, counted only
when used BY VALUE — field access, sizeof, value instantiation; a bare
`struct foo *` mention does not count), and constants (enum members and
file-scope const variables).

Out of scope by design: macros, typedefs (banned in this codebase),
non-default platform paths. Generated *.gen.{c,h} files are attributed to
their owning module by path.

Usage:
  symbol-graph.py                 # scan + report
  symbol-graph.py scan            # per-TU extraction only
  symbol-graph.py report          # aggregate existing per-TU YAMLs
  symbol-graph.py scan src/yetty/yplot tools/yplot   # limit scanned TUs

Outputs (default under tmp/qa/symbol-graph/):
  tus/<rel-tu-path>.yaml          # one record per scanned TU
  report/<module>.yaml            # per-module classification
  report/summary.yaml             # counts + anomalies

Environment: QA_BUILD_DIR selects the build dir holding
compile_commands.json (default: repo root, then first build-*/).
"""

from __future__ import annotations

import argparse
import json
import multiprocessing
import os
import shlex
import subprocess
import sys
import time
from pathlib import Path

import yaml
from clang import cindex

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
DEFAULT_OUT_DIR = REPO_ROOT / "tmp" / "qa" / "symbol-graph"

YAML_LOADER = getattr(yaml, "CSafeLoader", yaml.SafeLoader)
YAML_DUMPER = getattr(yaml, "CSafeDumper", yaml.SafeDumper)

SECTIONS = ("functions", "types", "consts")

# Roots whose TUs we analyse. tools/test/demo are consumer-only:
# they never get their own report but count as external users.
CONSUMER_ROOTS = ("tools", "test", "demo")

# Vendored third-party code living inside tracked paths — not yetty API,
# so neither defines nor uses are attributed to it.
VENDORED_FRAGMENTS = (
    "include/yetty/yrdawn/dawn/",
    "include/yetty/yrdawn/webgpu/",
)


# ---------------------------------------------------------------- output
_ISATTY = sys.stdout.isatty()


class Color:
    RESET = "\033[0m" if _ISATTY else ""
    RED = "\033[31m" if _ISATTY else ""
    GREEN = "\033[32m" if _ISATTY else ""
    YELLOW = "\033[33m" if _ISATTY else ""
    BLUE = "\033[34m" if _ISATTY else ""


def info(message: str) -> None:
    print(f"{Color.BLUE}[symbol-graph]{Color.RESET} {message}")


def warn(message: str) -> None:
    print(f"{Color.YELLOW}[warn]{Color.RESET} {message}")


def error(message: str) -> None:
    print(f"{Color.RED}[err]{Color.RESET} {message}", file=sys.stderr)


# ---------------------------------------------------------------- paths
def repo_relative(path: str) -> str | None:
    """Path relative to the repo root, or None when outside it."""
    try:
        return str(Path(path).resolve().relative_to(REPO_ROOT))
    except ValueError:
        return None


def module_of(rel_path: str | None) -> str | None:
    """Owning module of a repo-relative path, or None when untracked.

    include/yetty/<mod>/... and src/yetty/<mod>/... map to <mod>.
    tools/<name>/..., test/<name>/..., demo/<name>/... map to
    "tools/<name>" etc. — consumer namespaces, never reported on.
    """
    if not rel_path:
        return None
    if any(fragment in rel_path for fragment in VENDORED_FRAGMENTS):
        return None
    parts = rel_path.split("/")
    if len(parts) >= 3 and parts[0] == "include" and parts[1] == "yetty":
        return parts[2]
    if len(parts) >= 3 and parts[0] == "src" and parts[1] == "yetty":
        return parts[2]
    if parts[0] in CONSUMER_ROOTS:
        return f"{parts[0]}/{parts[1]}" if len(parts) >= 3 else parts[0]
    return None


def is_consumer(module: str) -> bool:
    return module.split("/")[0] in CONSUMER_ROOTS


def locate_compile_db() -> Path | None:
    env_dir = os.environ.get("QA_BUILD_DIR")
    if env_dir:
        candidate = REPO_ROOT / env_dir / "compile_commands.json"
        return candidate if candidate.is_file() else None
    root_db = REPO_ROOT / "compile_commands.json"
    if root_db.is_file():
        return root_db
    for build_dir in sorted(REPO_ROOT.glob("build-*")):
        candidate = build_dir / "compile_commands.json"
        if candidate.is_file():
            return candidate
    return None


# ---------------------------------------------------------------- compile db
def clean_parse_args(entry: dict) -> list[str]:
    """Turn a compile_commands entry into libclang parse args."""
    if "arguments" in entry:
        raw_args = list(entry["arguments"])
    else:
        raw_args = shlex.split(entry["command"])
    raw_args = raw_args[1:]  # drop the compiler itself
    cleaned: list[str] = []
    skip_next = False
    for arg in raw_args:
        if skip_next:
            skip_next = False
            continue
        if arg == "-c" or arg == entry["file"]:
            continue
        if arg in ("-o", "-MF", "-MT", "-MQ"):
            skip_next = True
            continue
        if arg in ("-MD", "-MMD"):
            continue
        cleaned.append(arg)
    return cleaned


def compiler_resource_dir(compiler: str) -> str | None:
    """Builtin-header dir of the real compiler; the bundled libclang has none."""
    try:
        completed = subprocess.run(
            [compiler, "-print-resource-dir"], capture_output=True, text=True
        )
    except OSError:
        return None
    resource_dir = completed.stdout.strip()
    return resource_dir if completed.returncode == 0 and resource_dir else None


def collect_tu_jobs(db_path: Path, path_filters: list[str]) -> list[dict]:
    """Select analysable TUs: tracked module, deduped, optionally filtered."""
    with open(db_path) as db_file:
        entries = json.load(db_file)
    resource_dirs: dict[str, str | None] = {}
    jobs: list[dict] = []
    seen: set[str] = set()
    for entry in entries:
        rel_tu = repo_relative(entry["file"])
        tu_module = module_of(rel_tu)
        if tu_module is None or rel_tu in seen:
            continue
        if path_filters and not any(rel_tu.startswith(prefix) for prefix in path_filters):
            continue
        seen.add(rel_tu)
        if "arguments" in entry:
            compiler = entry["arguments"][0]
        else:
            compiler = shlex.split(entry["command"])[0]
        if compiler not in resource_dirs:
            resource_dirs[compiler] = compiler_resource_dir(compiler)
        parse_args = clean_parse_args(entry)
        if resource_dirs[compiler]:
            parse_args += ["-resource-dir", resource_dirs[compiler]]
        jobs.append({"file": entry["file"], "rel_tu": rel_tu,
                     "module": tu_module, "args": parse_args})
    jobs.sort(key=lambda job: job["rel_tu"])
    return jobs


# ---------------------------------------------------------------- TU scan
ARRAY_TYPE_KINDS = frozenset((
    cindex.TypeKind.CONSTANTARRAY,
    cindex.TypeKind.INCOMPLETEARRAY,
    cindex.TypeKind.VARIABLEARRAY,
))

TAG_TYPE_KINDS = frozenset((cindex.TypeKind.RECORD, cindex.TypeKind.ENUM))

TAG_DECL_KINDS = frozenset((
    cindex.CursorKind.STRUCT_DECL,
    cindex.CursorKind.UNION_DECL,
    cindex.CursorKind.ENUM_DECL,
))

TYPED_DECL_KINDS = frozenset((
    cindex.CursorKind.VAR_DECL,
    cindex.CursorKind.PARM_DECL,
    cindex.CursorKind.FIELD_DECL,
))

CAST_KINDS = frozenset((
    cindex.CursorKind.CSTYLE_CAST_EXPR,
    cindex.CursorKind.COMPOUND_LITERAL_EXPR,
))


def value_tag_decl(clang_type: cindex.Type) -> cindex.Cursor | None:
    """The struct/union/enum decl a type uses BY VALUE, peeling arrays.

    Pointers deliberately resolve to None — a pointer-only mention is not
    a use of the pointee type.
    """
    canonical = clang_type.get_canonical()
    while canonical.kind in ARRAY_TYPE_KINDS:
        canonical = canonical.element_type.get_canonical()
    if canonical.kind in TAG_TYPE_KINDS:
        return canonical.get_declaration()
    return None


def named_tag_decl(decl: cindex.Cursor | None) -> cindex.Cursor | None:
    """Nearest named struct/union/enum decl: anonymous members attribute
    to their named enclosing record (e.g. the union inside a Result)."""
    while (decl is not None and decl.kind in TAG_DECL_KINDS
           and decl.is_anonymous()):
        decl = decl.semantic_parent
    if (decl is not None and decl.kind in TAG_DECL_KINDS
            and not decl.is_anonymous() and decl.spelling):
        return decl
    return None


def is_const_qualified_deep(clang_type: cindex.Type) -> bool:
    canonical = clang_type.get_canonical()
    while canonical.kind in ARRAY_TYPE_KINDS:
        if canonical.is_const_qualified():
            return True
        canonical = canonical.element_type.get_canonical()
    return canonical.is_const_qualified()


class TuScanner:
    """One-pass extraction of defines and uses from a parsed TU."""

    def __init__(self, tu_module: str):
        self.tu_module = tu_module
        self.defines: dict[str, dict[str, dict]] = {name: {} for name in SECTIONS}
        self.uses: dict[str, dict[str, dict]] = {name: {} for name in SECTIONS}
        self._file_rel_cache: dict[str, str | None] = {}

    # -------------------------------------------------- location helpers
    def _rel_file(self, location: cindex.SourceLocation) -> str | None:
        if location.file is None:
            return None
        name = location.file.name
        cached = self._file_rel_cache.get(name)
        if name not in self._file_rel_cache:
            cached = repo_relative(name)
            self._file_rel_cache[name] = cached
        return cached

    def _decl_ref(self, location: cindex.SourceLocation) -> str | None:
        rel_file = self._rel_file(location)
        if rel_file is None:
            return None
        return f"{rel_file}:{location.line}"

    # -------------------------------------------------- recording
    def _add_define(self, section: str, symbol: str, location) -> None:
        decl_ref = self._decl_ref(location)
        if decl_ref is None or module_of(decl_ref.rsplit(":", 1)[0]) != self.tu_module:
            return
        self.defines[section].setdefault(symbol, {"sym": symbol, "decl": decl_ref})

    def _add_use(self, section: str, symbol: str, decl_location) -> None:
        if symbol in self.uses[section]:
            return
        decl_ref = self._decl_ref(decl_location)
        if decl_ref is None or module_of(decl_ref.rsplit(":", 1)[0]) is None:
            return  # untracked symbol (system / third-party)
        self.uses[section][symbol] = {"sym": symbol, "decl": decl_ref}

    def _use_value_type(self, clang_type: cindex.Type) -> None:
        tag_decl = named_tag_decl(value_tag_decl(clang_type))
        if tag_decl is None:
            return
        definition = tag_decl.get_definition() or tag_decl
        self._add_use("types", definition.spelling, definition.location)

    def _is_file_local(self, cursor: cindex.Cursor) -> bool:
        """Static symbol living in a .c — internal by construction, so
        neither a private candidate nor a possible leak. Statics in
        headers ARE tracked: they are API used by inclusion."""
        if cursor.storage_class != cindex.StorageClass.STATIC:
            return False
        rel_file = self._rel_file(cursor.canonical.location)
        return rel_file is not None and rel_file.endswith(".c")

    # -------------------------------------------------- defines
    def _define_function(self, cursor: cindex.Cursor) -> None:
        if self._is_file_local(cursor):
            return
        canonical = cursor.canonical
        self._add_define("functions", cursor.spelling, canonical.location)

    # -------------------------------------------------- walk
    def scan(self, translation_unit: cindex.TranslationUnit) -> None:
        top_level = []
        for child in translation_unit.cursor.get_children():
            if self._rel_file(child.location) is not None:
                top_level.append(child)
        # Iterative DFS: (cursor, skip_direct_type_ref) — C initializer
        # lists and long else-if chains overflow Python's recursion limit.
        stack: list[tuple[cindex.Cursor, bool]] = [
            (cursor, False) for cursor in reversed(top_level)
        ]
        while stack:
            cursor, skip_type_ref = stack.pop()
            skip_child_type_refs = self._visit(cursor, skip_type_ref)
            if skip_child_type_refs is None:
                continue  # leaf — children not walked
            children = list(cursor.get_children())
            for child in reversed(children):
                stack.append((child, skip_child_type_refs))

    def _visit(self, cursor: cindex.Cursor, skip_type_ref: bool) -> bool | None:
        """Handle one cursor. Returns None to prune the subtree, else
        whether direct TYPE_REF children are already accounted for."""
        kind = cursor.kind

        if kind == cindex.CursorKind.TYPE_REF:
            # A TYPE_REF surviving to here sits in an expression context the
            # typed-decl/cast handlers didn't consume — e.g. sizeof(struct x).
            if not skip_type_ref:
                referenced = named_tag_decl(cursor.referenced)
                if referenced is not None:
                    definition = referenced.get_definition() or referenced
                    self._add_use("types", definition.spelling, definition.location)
            return None

        if kind == cindex.CursorKind.FUNCTION_DECL:
            self._define_function(cursor)
            self._use_value_type(cursor.result_type)
            return True  # return-type TYPE_REF handled via result_type

        if kind in TAG_DECL_KINDS:
            if (cursor.is_definition() and not cursor.is_anonymous()
                    and cursor.spelling):
                self._add_define("types", cursor.spelling, cursor.location)
            return False

        if kind == cindex.CursorKind.ENUM_CONSTANT_DECL:
            self._add_define("consts", cursor.spelling, cursor.location)
            return False

        if kind in TYPED_DECL_KINDS:
            self._use_value_type(cursor.type)
            if (kind == cindex.CursorKind.VAR_DECL
                    and cursor.semantic_parent is not None
                    and cursor.semantic_parent.kind
                    == cindex.CursorKind.TRANSLATION_UNIT
                    and is_const_qualified_deep(cursor.type)
                    and not self._is_file_local(cursor)):
                self._add_define("consts", cursor.spelling, cursor.location)
            return True  # declarator TYPE_REF handled via cursor.type

        if kind in CAST_KINDS:
            self._use_value_type(cursor.type)
            return True  # cast target TYPE_REF handled via cursor.type

        if kind == cindex.CursorKind.DECL_REF_EXPR:
            referenced = cursor.referenced
            if referenced is not None:
                ref_kind = referenced.kind
                if ref_kind == cindex.CursorKind.FUNCTION_DECL:
                    if not self._is_file_local(referenced):
                        self._add_use("functions", referenced.spelling,
                                      referenced.canonical.location)
                elif ref_kind == cindex.CursorKind.ENUM_CONSTANT_DECL:
                    self._add_use("consts", referenced.spelling,
                                  referenced.location)
                elif (ref_kind == cindex.CursorKind.VAR_DECL
                      and referenced.semantic_parent is not None
                      and referenced.semantic_parent.kind
                      == cindex.CursorKind.TRANSLATION_UNIT
                      and not self._is_file_local(referenced)):
                    self._add_use("consts", referenced.spelling,
                                  referenced.canonical.location)
            return False

        if kind == cindex.CursorKind.MEMBER_REF_EXPR:
            referenced = cursor.referenced  # the field decl
            if referenced is not None:
                record_decl = named_tag_decl(referenced.semantic_parent)
                if record_decl is not None:
                    self._add_use("types", record_decl.spelling,
                                  record_decl.location)
            return False

        return False

    # -------------------------------------------------- output
    def record(self, rel_tu: str) -> dict:
        def section_list(table: dict[str, dict[str, dict]]) -> dict:
            return {
                name: sorted(table[name].values(), key=lambda item: item["sym"])
                for name in SECTIONS
                if table[name]
            }

        return {
            "tu": rel_tu,
            "module": self.tu_module,
            "defines": section_list(self.defines),
            "uses": section_list(self.uses),
        }


# ---------------------------------------------------------------- workers
_WORKER_INDEX: cindex.Index | None = None
_WORKER_OUT_DIR: Path | None = None


def worker_init(out_dir_str: str) -> None:
    global _WORKER_INDEX, _WORKER_OUT_DIR
    _WORKER_INDEX = cindex.Index.create()
    _WORKER_OUT_DIR = Path(out_dir_str)


def scan_one_tu(job: dict) -> dict:
    assert _WORKER_INDEX is not None and _WORKER_OUT_DIR is not None
    result = {"rel_tu": job["rel_tu"], "status": "ok",
              "errors": [], "defines": 0, "uses": 0}
    try:
        translation_unit = _WORKER_INDEX.parse(job["file"], args=job["args"])
    except cindex.TranslationUnitLoadError as load_error:
        result["status"] = "parse-failed"
        result["errors"] = [str(load_error)]
        return result

    fatal_diags = [
        f"{diag.location}: {diag.spelling}"
        for diag in translation_unit.diagnostics
        if diag.severity >= cindex.Diagnostic.Error
    ]
    if fatal_diags:
        result["status"] = "diagnostics"
        result["errors"] = fatal_diags[:5]

    scanner = TuScanner(job["module"])
    scanner.scan(translation_unit)
    record = scanner.record(job["rel_tu"])

    out_path = _WORKER_OUT_DIR / "tus" / (job["rel_tu"] + ".yaml")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w") as out_file:
        yaml.dump(record, out_file, Dumper=YAML_DUMPER,
                  default_flow_style=None, sort_keys=False, width=200)

    result["defines"] = sum(len(record["defines"].get(name, ())) for name in SECTIONS)
    result["uses"] = sum(len(record["uses"].get(name, ())) for name in SECTIONS)
    return result


def run_scan(out_dir: Path, jobs_count: int, path_filters: list[str]) -> int:
    db_path = locate_compile_db()
    if db_path is None:
        error("no compile_commands.json found")
        error("run 'make build-desktop-ytrace-release' first, or set QA_BUILD_DIR")
        return 2
    info(f"compile db: {db_path}")

    tu_jobs = collect_tu_jobs(db_path, path_filters)
    if not tu_jobs:
        error("no analysable TUs matched")
        return 2
    info(f"scanning {len(tu_jobs)} TUs with {jobs_count} workers")

    started = time.time()
    troubled: list[dict] = []
    done = 0
    with multiprocessing.Pool(
        processes=jobs_count, initializer=worker_init, initargs=(str(out_dir),)
    ) as pool:
        for result in pool.imap_unordered(scan_one_tu, tu_jobs, chunksize=4):
            done += 1
            if result["status"] != "ok":
                troubled.append(result)
            if done % 100 == 0:
                info(f"  {done}/{len(tu_jobs)}")

    elapsed = time.time() - started
    info(f"scanned {len(tu_jobs)} TUs in {elapsed:.1f}s → {out_dir / 'tus'}")
    if troubled:
        log_path = out_dir / "scan-problems.yaml"
        with open(log_path, "w") as log_file:
            yaml.dump(troubled, log_file, Dumper=YAML_DUMPER,
                      default_flow_style=False, sort_keys=False)
        warn(f"{len(troubled)} TU(s) had parse errors — see {log_path}")
    return 0


# ---------------------------------------------------------------- report
def run_report(out_dir: Path) -> int:
    tu_dir = out_dir / "tus"
    tu_files = sorted(tu_dir.rglob("*.yaml")) if tu_dir.is_dir() else []
    if not tu_files:
        error(f"no per-TU records under {tu_dir} — run the scan first")
        return 2
    info(f"aggregating {len(tu_files)} per-TU records")

    # (section, sym) → {claiming module: set[decl "path:line"]}
    define_claims: dict[tuple[str, str], dict[str, set]] = {}
    # (section, sym) → set[(user module, decl "path:line")]
    uses: dict[tuple[str, str], set] = {}

    for tu_file in tu_files:
        with open(tu_file) as tu_handle:
            record = yaml.load(tu_handle, Loader=YAML_LOADER)
        for section in SECTIONS:
            for item in (record.get("defines") or {}).get(section, ()):
                key = (section, item["sym"])
                owner = module_of(item["decl"].rsplit(":", 1)[0])
                define_claims.setdefault(key, {}).setdefault(
                    owner, set()).add(item["decl"])
            for item in (record.get("uses") or {}).get(section, ()):
                key = (section, item["sym"])
                uses.setdefault(key, set()).add(
                    (record["module"], item["decl"]))

    # Resolve ownership. Consumers (tools/test/demo) routinely re-declare a
    # module's symbol locally (a hand-written extern) or reuse throwaway
    # names like `opts` — those claims never compete with a real module's.
    # Only real-module vs real-module double claims are genuine conflicts.
    defines: dict[tuple[str, str], dict] = {}
    conflicts: dict[tuple[str, str], list[str]] = {}
    for key, claims in define_claims.items():
        real_owners = sorted(
            module for module in claims if not is_consumer(module))
        if len(real_owners) > 1:
            conflicts[key] = sorted(claims)
            continue
        if real_owners:
            owner = real_owners[0]
            defines[key] = {"module": owner, "decls": set(claims[owner])}
        # consumer-only symbols (each tool's own main/opts) are unreportable

    # Symbols only ever seen at use sites (header-only module never compiled
    # as its own TU): synthesize the define from the use-side decl location.
    for key, user_decls in uses.items():
        if key in defines or key in conflicts:
            continue
        owners = {module_of(decl.rsplit(":", 1)[0]) for user_module, decl in user_decls}
        owners.discard(None)
        if len(owners) == 1:
            defines[key] = {"module": owners.pop(),
                            "decls": {decl for user_module, decl in user_decls}}

    # A defining TU may never include its module's public header (the decl
    # it sees is the definition in the .c) while consumers resolve the same
    # symbol through include/yetty/<mod>/. Merge use-side decl locations
    # owned by the SAME module so the public/private split sees every decl;
    # consumer-local extern redeclarations stay out (different owner).
    for key, define_slot in defines.items():
        for user_module, decl in uses.get(key, ()):
            if module_of(decl.rsplit(":", 1)[0]) == define_slot["module"]:
                define_slot["decls"].add(decl)

    section_kind = {"functions": "function", "types": "type", "consts": "const"}
    reports: dict[str, dict] = {}
    for (section, symbol), define_slot in defines.items():
        owner = define_slot["module"]
        if owner is None or is_consumer(owner):
            continue
        decls = sorted(define_slot["decls"])
        public_decls = [decl for decl in decls if decl.startswith("include/")]
        is_public = bool(public_decls)
        decl_ref = public_decls[0] if is_public else decls[0]

        # Functions join by name — external linkage makes the name one
        # entity program-wide, which also catches consumers that hand-write
        # an extern instead of including the header. Types and constants
        # have NO linkage: an unrelated local `struct opts` in some tool is
        # a different type, so their uses must point at the owner's decl.
        user_decls = uses.get((section, symbol), set())
        if section == "functions":
            users = {user_module for user_module, decl in user_decls}
        else:
            users = {user_module for user_module, decl in user_decls
                     if decl in define_slot["decls"]}
        external_users = sorted(users - {owner})
        internal_use = owner in users

        report = reports.setdefault(
            owner,
            {"module": owner, "private_candidates": [],
             "leaks": [], "public_used": []},
        )
        entry = {"sym": symbol, "kind": section_kind[section], "decl": decl_ref}
        if is_public and external_users:
            report["public_used"].append({**entry, "used_by": external_users})
        elif is_public:
            report["private_candidates"].append(
                {**entry, "internal_use": internal_use})
        elif external_users:
            report["leaks"].append({**entry, "used_by": external_users})

    report_dir = out_dir / "report"
    report_dir.mkdir(parents=True, exist_ok=True)
    for stale in report_dir.glob("*.yaml"):
        stale.unlink()

    summary_rows = []
    totals = {"private_candidates": 0, "leaks": 0, "public_used": 0}
    for module in sorted(reports):
        report = reports[module]
        for category in ("private_candidates", "leaks", "public_used"):
            report[category].sort(key=lambda item: item["sym"])
            totals[category] += len(report[category])
        with open(report_dir / f"{module}.yaml", "w") as report_file:
            yaml.dump(report, report_file, Dumper=YAML_DUMPER,
                      default_flow_style=None, sort_keys=False, width=200)
        summary_rows.append({
            "module": module,
            "private_candidates": len(report["private_candidates"]),
            "leaks": len(report["leaks"]),
            "public_used": len(report["public_used"]),
        })

    summary = {
        "tus": len(tu_files),
        "modules": len(reports),
        "totals": totals,
        "per_module": summary_rows,
    }
    if conflicts:
        summary["conflicts"] = sorted(
            (
                {"kind": section_kind[section], "sym": symbol,
                 "modules": claimants}
                for (section, symbol), claimants in conflicts.items()
            ),
            key=lambda item: item["sym"],
        )
    with open(report_dir / "summary.yaml", "w") as summary_file:
        yaml.dump(summary, summary_file, Dumper=YAML_DUMPER,
                  default_flow_style=None, sort_keys=False, width=200)

    header = f"{'module':<22} {'private?':>9} {'leaks':>6} {'public':>7}"
    info(header)
    for row in summary_rows:
        flagged = row["private_candidates"] or row["leaks"]
        color = Color.YELLOW if flagged else ""
        print(f"{color}{row['module']:<22} {row['private_candidates']:>9} "
              f"{row['leaks']:>6} {row['public_used']:>7}{Color.RESET}")
    info(f"totals: {totals['private_candidates']} private candidate(s), "
         f"{totals['leaks']} leak(s), {totals['public_used']} public-used")
    info(f"per-module reports: {report_dir}")
    return 1 if (totals["private_candidates"] or totals["leaks"]) else 0


# ---------------------------------------------------------------- main
def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("phase", nargs="?", default="all",
                        choices=("all", "scan", "report"))
    parser.add_argument("paths", nargs="*",
                        help="repo-relative prefixes limiting scanned TUs")
    parser.add_argument("--out-dir", default=str(DEFAULT_OUT_DIR),
                        help="output root (default: tmp/qa/symbol-graph)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = REPO_ROOT / out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    path_filters = args.paths or (os.environ.get("QA_PATHS", "").split() or [])

    if args.phase in ("all", "scan"):
        scan_status = run_scan(out_dir, args.jobs, path_filters)
        if scan_status != 0:
            return scan_status
    if args.phase in ("all", "report"):
        return run_report(out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
