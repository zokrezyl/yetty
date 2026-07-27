# yclass codegen tests. Two Python lanes, registered like the e2e suite:
#
#   codegen_units — pure-function units for the generator's helpers (typedef
#                   reproduction/dedup, yaml scalar/dump, api namespace,
#                   duplicate-stem detection, submodule resolution, compile-flag
#                   extraction, atomic-write normalization). Headless, fast.
#
#   codegen_e2e   — Part A asserts the refactored properties on the committed
#                   generated tree (headless); Part B runs codegen.py on tiny
#                   synthetic modules to check the strict unresolved-symbol
#                   failure, override@ arity, inline clang-format and
#                   reproducibility. Part B self-skips when clang / clang-format
#                   are unavailable.
#
# The generator under test is src/yetty/yclass/gen/codegen.py; the scripts locate
# the repo from their own path, so no target dependency is needed.

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME codegen_units
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/units.py)
    set_tests_properties(codegen_units PROPERTIES
        LABELS  "codegen;unit"
        TIMEOUT 60)

    add_test(NAME codegen_e2e
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/e2e.py)
    set_tests_properties(codegen_e2e PROPERTIES
        LABELS  "codegen"
        TIMEOUT 180)
endif()
