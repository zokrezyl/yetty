#!/usr/bin/env python3
"""End-to-end tests for the yclass codegen refactor.

Part A asserts the refactored PROPERTIES on the committed generated tree — no
clang required. Part B runs codegen.py on tiny synthetic modules (written to a
temp tree, parsed against the real yetty headers via a fixture
compile_commands.json) to check behaviors that only appear at generation time:
the strict unresolved-symbol failure, override@ arity, inline clang-format, and
reproducibility. Part B skips when clang / clang-format are unavailable.

Run standalone or under ctest:  python3 e2e.py
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GEN = REPO / "src" / "yetty" / "yclass" / "gen" / "codegen.py"
API = REPO / "include" / "yetty" / "api"
GENC = REPO / "src" / "yetty" / "gen"
CLANG = os.environ.get("CLANG", "clang")
CLANG_FORMAT = os.environ.get("CLANG_FORMAT", "clang-format")


def _read(path: Path) -> str:
    return path.read_text() if path.exists() else ""


# ------------------------------------------------------------------ Part A ---
# Properties of the committed generated tree. If a named module/file is renamed
# these pin to the current layout on purpose — they are regression guards.

class CommittedTypedefReproduction(unittest.TestCase):
    def test_cross_file_callback_reproduced_before_use(self):
        # yetty_ygui_click_cb is authored in ygui/mixins/clickable and used by a
        # tree_node stub — its typedef must be reproduced in tree_node's api
        # header, ahead of the signature that names it.
        header = _read(API / "ygui" / "widgets" / "tree_node.h")
        self.assertIn("yetty_ygui_click_cb", header)
        definition = header.find("(*yetty_ygui_click_cb)")
        usage = header.find("yetty_ygui_click_cb cb")
        self.assertGreater(definition, -1, "click_cb typedef not reproduced")
        self.assertGreater(usage, -1, "click_cb not used in a signature")
        self.assertLess(definition, usage, "typedef must precede its use")

    def test_no_system_typedef_leaks_into_api_headers(self):
        patterns = ("typedef unsigned long size_t;", "typedef __uint32_t uint32_t;",
                    "typedef __int32_t int32_t;", "typedef __uint8_t uint8_t;",
                    "typedef __uint64_t uint64_t;", "typedef __int128")
        offenders = [str(h) for h in API.rglob("*.h")
                     if any(p in h.read_text() for p in patterns)]
        self.assertEqual(offenders, [], f"system typedefs leaked: {offenders}")

    def test_override_fn_typedef_not_duplicated(self):
        # The stub machinery emits override _fn typedefs; the owned-typedef
        # reproduction must NOT emit a second copy in the same header.
        header = _read(GENC / "impl" / "ygui" / "widget.h")
        self.assertEqual(header.count("(*yetty_ygui_widget_on_press_fn)"), 1,
                         "override _fn typedef duplicated")

    def test_owned_typedef_from_exposed_struct_reproduced(self):
        # yetty_yrdawn_emit_osc_fn is used only inside the factory-args struct,
        # not a bare signature — it must still be reproduced.
        header = _read(API / "yrdawn" / "figure.h")
        self.assertIn("(*yetty_yrdawn_emit_osc_fn)", header)


class CommittedRegistration(unittest.TestCase):
    def _defs(self, path: Path, symbol: str) -> int:
        # count function DEFINITIONS (body), not forward decls
        return len(re.findall(
            r"^struct yetty_ycore_void_result " + re.escape(symbol) + r"\(void\)$",
            _read(path), re.M))

    def test_duplicate_stem_module_uses_standalone_aggregators(self):
        plat = GENC / "impl" / "yplatform"
        self.assertEqual(self._defs(plat / "rpc.gen.c", "yetty_yplatform_register"), 1)
        self.assertEqual(
            self._defs(plat / "yclipboard" / "rpc.gen.c",
                       "yetty_yplatform_yclipboard_register"), 1)

    def test_duplicate_stem_has_no_colliding_per_source_register(self):
        # yetty_yplatform_glfw_register would be defined 4× (one per glfw.c) —
        # the whole reason yplatform uses per-submodule aggregators.
        hits = [str(p) for p in (GENC / "impl" / "yplatform").rglob("*.c")
                if re.search(r"^struct yetty_ycore_void_result yetty_yplatform_glfw_register\(void\)$",
                             p.read_text(), re.M)]
        self.assertEqual(hits, [], f"colliding glfw register defined in: {hits}")

    def test_unique_stem_module_uses_folded_per_source_register(self):
        # ygui has unique stems → the register folds into each impl glue, and
        # there is NO standalone rpc.gen.c under its gen/impl tree.
        self.assertEqual(
            self._defs(GENC / "impl" / "ygui" / "widgets" / "button.c",
                       "yetty_ygui_button_register"), 1)
        self.assertEqual(list((GENC / "impl" / "ygui").rglob("rpc.gen.c")), [])


class CommittedLayout(unittest.TestCase):
    def test_no_stray_out_of_tree_gen_dirs(self):
        for root in ("tools", "demo", "src/api"):
            strays = [str(d) for d in (REPO / root).rglob("gen")
                      if d.is_dir() and "assets" not in str(d)]
            self.assertEqual(strays, [], f"stray gen dir under {root}: {strays}")

    def test_out_of_tree_module_emits_into_shared_gen(self):
        # yhello lives in tools/ but its generated tree is under src/yetty/gen.
        self.assertTrue((GENC / "impl" / "yhello").is_dir())
        self.assertTrue((GENC / "api" / "yhello").is_dir())

    def test_api_facade_namespace_drops_prefix(self):
        self.assertTrue((API / "yplot" / "plot.h").exists())
        self.assertFalse((API / "api_yplot").exists())
        self.assertFalse((API / "api").exists())  # no doubled api/api/


class CommittedModelHealth(unittest.TestCase):
    def test_vterm_types_not_degraded_to_int(self):
        model = _read(REPO / "src" / "yetty" / "yvterm" / "model.yaml")
        self.assertIn('"VTerm *"', model)
        self.assertIn('"VTermState *"', model)

    def test_no_model_has_trailing_whitespace(self):
        offenders = []
        for model in REPO.glob("src/yetty/*/model.yaml"):
            for num, line in enumerate(model.read_text().splitlines(), 1):
                if line != line.rstrip():
                    offenders.append(f"{model}:{num}")
        self.assertEqual(offenders, [], f"trailing whitespace: {offenders[:10]}")


@unittest.skipUnless(shutil.which(CLANG_FORMAT), "clang-format unavailable")
class CommittedFormatting(unittest.TestCase):
    def test_sampled_generated_files_are_already_formatted(self):
        samples = [API / "ygui" / "widgets" / "tree_node.h",
                   GENC / "impl" / "yplatform" / "rpc.gen.c",
                   GENC / "api" / "yhello" / "main.c"]
        for path in samples:
            if not path.exists():
                continue
            result = subprocess.run(
                [CLANG_FORMAT, f"-assume-filename={path}"],
                input=path.read_text(), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout, path.read_text(),
                             f"{path} is not clang-format-idempotent")


# ------------------------------------------------------------------ Part B ---

def _run_codegen(sources: dict, module: str):
    """Generate `module` from `sources` ({relpath: text}) in an isolated temp
    tree, parsing against the real yetty headers. Returns (tmpdir, CompletedProcess).
    The generated tree lands under <tmp>/src/yetty/gen (isolated)."""
    tmp = Path(tempfile.mkdtemp(prefix="cgtest_"))
    src_dir = tmp / "src" / "yetty" / module
    src_dir.mkdir(parents=True)
    (tmp / "include" / "yetty").mkdir(parents=True)
    entries, paths = [], []
    for rel, text in sources.items():
        path = src_dir / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        paths.append(path)
        entries.append({
            "directory": str(tmp), "file": str(path),
            "command": f"{CLANG} -I{REPO}/include -I{REPO}/src -std=gnu2x -c {path}"})
    (tmp / "compile_commands.json").write_text(json.dumps(entries))
    env = dict(os.environ, PYTHONHASHSEED="0",
               YCLASS_COMPILE_DB=str(tmp / "compile_commands.json"))
    argv = [sys.executable, str(GEN), module, str(tmp / "include" / "yetty"),
            str(src_dir), *[str(p) for p in paths]]
    result = subprocess.run(argv, cwd=str(tmp), env=env, capture_output=True, text=True)
    return tmp, result


_BASE_SUB = """
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

struct YETTY_ANNOTATE("class@{m}:base") yetty_{m}_base {{ int a; }};

YETTY_ANNOTATE("virtual@{m}:base:tick")
static struct yetty_ycore_void_result base_tick(struct yetty_yclass_object *obj)
{{ (void)obj; struct yetty_ycore_void_result r = {{0}}; return r; }}

struct YETTY_ANNOTATE("class@{m}:sub") YETTY_ANNOTATE("parent@{m}:base")
    yetty_{m}_sub {{ int b; }};

YETTY_ANNOTATE("{override}")
static struct yetty_ycore_void_result sub_tick(struct yetty_yclass_object *obj)
{{ (void)obj; struct yetty_ycore_void_result r = {{0}}; return r; }}
"""


@unittest.skipUnless(shutil.which(CLANG), "clang unavailable")
class CodegenBehavior(unittest.TestCase):
    def tearDown(self):
        for tmp in getattr(self, "_tmps", []):
            shutil.rmtree(tmp, ignore_errors=True)

    def _gen(self, sources, module):
        tmp, result = _run_codegen(sources, module)
        self._tmps = getattr(self, "_tmps", []) + [tmp]
        return tmp, result

    def test_override_4segment_rejected(self):
        src = _BASE_SUB.format(m="tcgfour", override="override@tcgfour:sub:base:tick")
        _, result = self._gen({"mod.c": src}, "tcgfour")
        self.assertNotEqual(result.returncode, 0, "4-segment override must be rejected")
        self.assertIn("4-part form", result.stdout + result.stderr)

    def test_override_3segment_accepted(self):
        if not shutil.which(CLANG_FORMAT):
            self.skipTest("clang-format unavailable")
        src = _BASE_SUB.format(m="tcgthree", override="override@tcgthree:base:tick")
        _, result = self._gen({"mod.c": src}, "tcgthree")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unresolved_header_fails(self):
        src = ("#include <yetty/yclass/class.h>\n"
               "#include <totally_nonexistent_header_xyz.h>\n"
               'struct YETTY_ANNOTATE("class@tcgbad:thing") yetty_tcgbad_thing { int a; };\n')
        _, result = self._gen({"mod.c": src}, "tcgbad")
        self.assertNotEqual(result.returncode, 0, "unresolved header must fail codegen")
        self.assertIn("unresolved symbol", result.stdout + result.stderr)

    def test_undeclared_yetty_stub_tolerated(self):
        if not shutil.which(CLANG_FORMAT):
            self.skipTest("clang-format unavailable")
        # A reference to a not-yet-generated stub is an undeclared 'yetty_*'
        # identifier — tolerated, codegen still succeeds.
        src = (
            "#include <yetty/yclass/class.h>\n"
            "#include <yetty/ycore/result.h>\n"
            'struct YETTY_ANNOTATE("class@tcgstub:thing") yetty_tcgstub_thing { int a; };\n'
            'YETTY_ANNOTATE("virtual@tcgstub:thing:go")\n'
            "static struct yetty_ycore_void_result thing_go(struct yetty_yclass_object *o);\n"
            "typedef struct yetty_ycore_void_result (*tcg_fn)(struct yetty_yclass_object *);\n"
            "static tcg_fn tcg_ref = yetty_tcgstub_thing_go;\n"
            "static struct yetty_ycore_void_result thing_go(struct yetty_yclass_object *o)\n"
            "{ (void)o; (void)tcg_ref; struct yetty_ycore_void_result r = {0}; return r; }\n")
        _, result = self._gen({"mod.c": src}, "tcgstub")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_generated_files_are_formatted_and_reproducible(self):
        if not shutil.which(CLANG_FORMAT):
            self.skipTest("clang-format unavailable")
        src = _BASE_SUB.format(m="tcgrepro", override="override@tcgrepro:base:tick")
        tmp1, r1 = self._gen({"mod.c": src}, "tcgrepro")
        tmp2, r2 = self._gen({"mod.c": src}, "tcgrepro")
        self.assertEqual(r1.returncode, 0, r1.stdout + r1.stderr)
        self.assertEqual(r2.returncode, 0, r2.stdout + r2.stderr)
        gen1 = sorted((tmp1 / "src" / "yetty" / "gen").rglob("*"))
        self.assertTrue(gen1, "no generated files produced")
        # every emitted C/H is clang-format-idempotent
        for path in gen1:
            if path.suffix in (".c", ".h"):
                formatted = subprocess.run(
                    [CLANG_FORMAT, f"-assume-filename={path}"],
                    input=path.read_text(), capture_output=True, text=True).stdout
                self.assertEqual(formatted, path.read_text(),
                                 f"{path.name} not formatted at write time")
        # deterministic: two runs, identical relative outputs
        def snapshot(root):
            base = root / "src" / "yetty" / "gen"
            return {str(p.relative_to(base)): p.read_text()
                    for p in base.rglob("*") if p.is_file()}
        self.assertEqual(snapshot(tmp1), snapshot(tmp2), "codegen output non-deterministic")


if __name__ == "__main__":
    unittest.main(verbosity=2)
