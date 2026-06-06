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
    yetty_yview
    -Wl,--no-whole-archive
    yetty_yplatform_core
    Threads::Threads
)

set_target_properties(yetty_ffi PROPERTIES
    OUTPUT_NAME yetty_ffi
    POSITION_INDEPENDENT_CODE ON
)
