# ============================================================================
# py-parse — standalone dev build of the libpython-free CPython parser subset.
#
# This is the self-contained build used for development and verification. The
# yetty build integrates the same sources via CMakeLists.txt (target
# yetty_cpython + the py-parse tool); see README.md.
#
# `make`           build build/full/py-parse-free (links libc ONLY)
# `make run`       build + parse samples/demo.py
# `make verify`    diff AST output against a reference CPython 3.14 parser
#                  (requires that reference binary — see tools/verify.sh)
# `make gen-ast`   regenerate the plain-C AST from Python.asdl
# `make clean`     remove build/
# ============================================================================
PYTHON ?= python3

.PHONY: all run verify gen-ast gen-printable clean

all:
	@bash tools/build_full.sh

run: all
	@./build/full/py-parse-free samples/demo.py

verify: all
	@bash tools/verify.sh

# Regenerate the plain-C AST (structs, constructors, dumper) from the ASDL.
gen-ast:
	$(PYTHON) tools/gen_ast.py third_party/cpython/Parser/Python.asdl \
		src/ast third_party/cpython/Parser

# Regenerate the printable-codepoint table. For an exact match to a given
# CPython, generate it from that version (see tools/gen_printable.py, or link a
# tiny helper against its libpython — see README).
gen-printable:
	$(PYTHON) tools/gen_printable.py > src/ast/printable.gen.c

clean:
	rm -rf build
