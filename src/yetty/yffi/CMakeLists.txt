# libyetty_ffi.so — shared library exposing yclass module symbols for the
# language bindings under bindings/<lang>/. Built ONLY in the PIC double-build
# tree (YETTY_BUILD_FFI_SHARED=ON); never linked by the static application, so
# the non-PIC application archives keep their full speed.

# ytrace.c is compiled per-consumer (not a static lib target — see
# tools/ycat/CMakeLists.txt), so include it directly to satisfy the
# trace/log symbols (ytrace_output, …) the module archives reference.
add_library(yetty_ffi SHARED
    yffi.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yetty_ffi PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)

# Force-include each FFI-exposed module's whole archive so its
# `yetty_<module>_*` stubs are exported even though nothing in this TU
# references them. Transitive deps (ycore / yclass / yface / ydraw-core) are
# pulled normally to satisfy the references. yplatform_core provides the
# thread / timestamp helpers ytrace depends on. Add further modules here as
# their bindings are validated.
target_link_libraries(yetty_ffi PRIVATE
    -Wl,--whole-archive
    yetty_yfigure
    yetty_ygrid
    yetty_ygui
    yetty_ymgui
    yetty_yrdawn
    yetty_yshadertoy
    yetty_yview
    yetty_yvterm
    -Wl,--no-whole-archive
    yetty_yplatform_core
    yetty_ydraw     # full ydraw (cell-ref-table, …), referenced by yvterm
    uv_a            # ygui framework is libuv-driven
    Threads::Threads
)

# Fail the link on any unresolved symbol so a missing transitive module shows
# up here at build time, not as a dlopen error in the bindings.
target_link_options(yetty_ffi PRIVATE -Wl,--no-undefined)

set_target_properties(yetty_ffi PROPERTIES
    OUTPUT_NAME yetty_ffi
    POSITION_INDEPENDENT_CODE ON
)
