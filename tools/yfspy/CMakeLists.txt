# yfspy tool — renders a Python-subset yfsvm shader as a yplot complex.
#
# The shader is compiled to yfsvm bytecode IN-PROCESS: yetty_yfspy_compile
# parses it with the embedded libpython-free CPython parser (yetty_cpython)
# and lowers the AST to a yfsvm program. yfspy then wraps it in the same yplot
# OSC envelope tools/yplot emits. No subprocess, no CPython runtime.

# Codegen bridge library. This is the single translation unit that includes
# the cpython runtime's shadow headers (<Python.h>, pycore_ast.h, …), so those
# private include dirs are scoped to this target and never leak elsewhere.
add_library(yetty_yfspy_compile STATIC compile.c)
set_target_properties(yetty_yfspy_compile PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(yetty_yfspy_compile PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src/cpython/src/runtime
    ${YETTY_ROOT}/src/cpython/src/ast
    ${YETTY_ROOT}/src/cpython/third_party/cpython/Parser
)
target_compile_definitions(yetty_yfspy_compile PRIVATE Py_BUILD_CORE)
target_link_libraries(yetty_yfspy_compile PUBLIC
    yetty_yfsvm_core
    yetty_cpython
)

add_executable(yfspy
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yfspy PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Threads::Threads picks the right thing per platform — same pattern as
# tools/yplot and tools/yecho.
find_package(Threads REQUIRED)
target_link_libraries(yfspy PRIVATE
    yetty_yfspy_compile
    yetty_yplot_core
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yfspy PRIVATE -Wall -Wextra)
endif()
