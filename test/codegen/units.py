#!/usr/bin/env python3
"""Unit tests for the pure helpers of the yclass generator (codegen.py).

These exercise the functions that were added/changed in the codegen refactor
without touching clang or the filesystem (except a scratch dir for the atomic
writer). Run standalone or under ctest:  python3 units.py
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "src" / "yetty" / "yclass" / "gen"))
import codegen  # noqa: E402


class TypedefText(unittest.TestCase):
    def test_function_pointer_splices_name(self):
        decl = {"name": "yetty_x_cb",
                "type": {"qualType": "struct yetty_ycore_void_result (*)(struct o *, int)"}}
        self.assertEqual(
            codegen._typedef_text(decl),
            "typedef struct yetty_ycore_void_result (*yetty_x_cb)(struct o *, int);")

    def test_plain_typedef(self):
        decl = {"name": "yetty_x_id", "type": {"qualType": "unsigned long"}}
        self.assertEqual(codegen._typedef_text(decl), "typedef unsigned long yetty_x_id;")

    def test_pointer_underlying_no_double_space(self):
        decl = {"name": "p", "type": {"qualType": "struct x *"}}
        self.assertEqual(codegen._typedef_text(decl), "typedef struct x *p;")


class TypedefDefinedNames(unittest.TestCase):
    def test_function_pointer_form(self):
        text = "typedef struct r (*yetty_a_cb)(struct o *, int);"
        self.assertEqual(codegen._typedef_defined_names(text), {"yetty_a_cb"})

    def test_plain_form(self):
        self.assertEqual(codegen._typedef_defined_names("typedef unsigned long size_t;"),
                         {"size_t"})

    def test_multiple_mixed(self):
        text = ("typedef unsigned int uint32_t;\n"
                "typedef struct r (*cb_fn)(int);\n"
                "struct not_a_typedef { int x; };\n")
        self.assertEqual(codegen._typedef_defined_names(text), {"uint32_t", "cb_fn"})

    def test_no_typedefs(self):
        self.assertEqual(codegen._typedef_defined_names("int f(void) { return 0; }"), set())


class YamlScalar(unittest.TestCase):
    def test_empty_string_is_quoted(self):
        # Regression: a bare empty value used to print `key: ` with a trailing
        # space (an anonymous union member's empty name). Must be "".
        self.assertEqual(codegen._yaml_scalar(""), '""')

    def test_none_bool_number(self):
        self.assertEqual(codegen._yaml_scalar(None), "null")
        self.assertEqual(codegen._yaml_scalar(True), "true")
        self.assertEqual(codegen._yaml_scalar(False), "false")
        self.assertEqual(codegen._yaml_scalar(7), "7")

    def test_special_chars_are_quoted(self):
        self.assertEqual(codegen._yaml_scalar("int"), "int")              # plain, bare
        self.assertTrue(codegen._yaml_scalar("int *").startswith('"'))    # '*' → quote
        self.assertTrue(codegen._yaml_scalar("a: b").startswith('"'))     # colon → quote
        self.assertTrue(codegen._yaml_scalar(" leading").startswith('"'))  # ws → quote

    def test_plain_string_unquoted(self):
        self.assertEqual(codegen._yaml_scalar("yetty_ygui_click_cb"), "yetty_ygui_click_cb")


class YamlDump(unittest.TestCase):
    def test_empty_containers(self):
        self.assertEqual(codegen.yaml_dump({"a": [], "b": {}}), "a: []\nb: {}")

    def test_empty_string_value_has_no_trailing_space(self):
        out = codegen.yaml_dump({"name": ""})
        self.assertEqual(out, 'name: ""')
        self.assertFalse(out.endswith(" "))

    def test_no_line_has_trailing_whitespace(self):
        model = {"classes": [{"name": "c", "fields": [{"name": "", "kind": "union"}]}]}
        for line in codegen.yaml_dump(model).splitlines():
            self.assertEqual(line, line.rstrip(), f"trailing ws on: {line!r}")


class Namespaces(unittest.TestCase):
    def test_api_namespace(self):
        self.assertEqual(codegen.api_namespace("api_yplot"), "yplot")
        self.assertEqual(codegen.api_namespace("ygui"), "ygui")

    def test_module_include_subpath(self):
        self.assertEqual(codegen.module_include_subpath("api_yplot"), "api/yplot")
        self.assertEqual(codegen.module_include_subpath("ygui"), "ygui")


class DuplicateStems(unittest.TestCase):
    def test_true_on_shared_stem(self):
        model = {"classes": [{"source_file": "/r/m/a/glfw.c"},
                             {"source_file": "/r/m/b/glfw.c"}]}
        self.assertTrue(codegen._module_has_duplicate_stems(model))

    def test_false_on_unique_stems(self):
        model = {"classes": [{"source_file": "/r/m/a/button.c"},
                             {"source_file": "/r/m/b/slider.c"}]}
        self.assertFalse(codegen._module_has_duplicate_stems(model))


class SubmoduleOf(unittest.TestCase):
    def test_subdir(self):
        self.assertEqual(
            codegen._submodule_of("/r/src/yetty/yplatform/ywindow/glfw.c",
                                  "/r/src/yetty/yplatform"), "ywindow")

    def test_root_source(self):
        self.assertEqual(
            codegen._submodule_of("/r/src/yetty/yplatform/platform.c",
                                  "/r/src/yetty/yplatform"), "")


class ExtractParseFlags(unittest.TestCase):
    def test_keeps_includes_and_defines_attached(self):
        flags = codegen._extract_parse_flags("clang -Iinc -DFOO -c a.c -o a.o")
        self.assertIn("-Iinc", flags)
        self.assertIn("-DFOO", flags)

    def test_keeps_separated_forms(self):
        flags = codegen._extract_parse_flags(
            "clang -isystem /sys -include prefix.h -I foo -D BAR -c a.c")
        self.assertEqual(flags.count("-isystem"), 1)
        self.assertIn("/sys", flags)
        self.assertIn("prefix.h", flags)
        self.assertIn("foo", flags)     # separated -I foo
        self.assertIn("BAR", flags)     # separated -D BAR

    def test_drops_noise(self):
        flags = codegen._extract_parse_flags(
            "clang -O2 -g -Wall -std=gnu2x -MD -c a.c -o a.o")
        for junk in ("-O2", "-g", "-Wall", "-std=gnu2x", "-MD", "-c", "-o", "a.o", "a.c"):
            self.assertNotIn(junk, flags)


class CompileFlagsFor(unittest.TestCase):
    def setUp(self):
        self._saved = codegen._COMPILE_DB

    def tearDown(self):
        codegen._COMPILE_DB = self._saved

    def test_exact_hit(self):
        codegen._COMPILE_DB = {os.path.abspath("/r/m/a.c"): ["-I/x"]}
        self.assertEqual(codegen._compile_flags_for(Path("/r/m/a.c")), ["-I/x"])

    def test_same_dir_sibling_fallback(self):
        # yplatform's android/webasm variants (not compiled in this build) reuse
        # a same-directory sibling's flags.
        codegen._COMPILE_DB = {os.path.abspath("/r/m/glfw.c"): ["-I/x"]}
        self.assertEqual(codegen._compile_flags_for(Path("/r/m/android.c")), ["-I/x"])

    def test_no_entry_no_sibling(self):
        codegen._COMPILE_DB = {os.path.abspath("/r/m/a.c"): ["-I/x"]}
        self.assertIsNone(codegen._compile_flags_for(Path("/r/other/z.c")))

    def test_empty_db(self):
        codegen._COMPILE_DB = {}
        self.assertIsNone(codegen._compile_flags_for(Path("/r/m/a.c")))


class WriteAtomicTrailingEdge(unittest.TestCase):
    """Non-C outputs (model.yaml) get their trailing edge normalized; empty
    pre-touch placeholders are left empty. (The C/H clang-format path is covered
    in e2e.py where clang-format is available.)"""
    def test_single_trailing_newline(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "model.yaml"
            codegen._write_atomic(path, "a: 1\n\n\n")
            self.assertEqual(path.read_text(), "a: 1\n")

    def test_empty_stays_empty(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "placeholder.c"
            codegen._write_atomic(path, "")
            self.assertEqual(path.read_text(), "")


if __name__ == "__main__":
    unittest.main(verbosity=2)
