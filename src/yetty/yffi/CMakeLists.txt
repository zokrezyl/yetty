# libyetty_ffi.so — shared library backing the public `yetty.api.*` language
# bindings. It exports ONLY the src/api/* facade symbols and links the minimal
# render/compute closure those facades need — NOT the GPU / window / event-loop
# / app surface. That keeps the library small and, crucially, means PIC is
# required on just a handful of dependency targets (marked in
# build-tools/yetty/platform/shared.cmake), never a whole-project PIC build.
#
# As new src/api/<feature> facades land, add each facade's whole-archive line
# here and its dependency targets to the PIC list in shared.cmake.

find_package(Threads REQUIRED)

# ytrace.c is compiled per-consumer (not a standalone lib target), so include it
# directly to satisfy the trace/log symbols (ytrace_output, …) the dependency
# archives reference through the ydebug/ytrace macros. It in turn needs a few
# leaf platform helpers (mutex, timestamp, stderr-color); pull just those two
# self-contained platform sources rather than the heavy yplatform_core.
add_library(yetty_ffi SHARED
    yffi.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

target_include_directories(yetty_ffi PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Force-export each facade's whole archive so its `yetty_api_*` stubs are
# available to ctypes even though nothing in this TU references them. The real
# render/compute dependency targets are pulled normally to satisfy references;
# their transitive third-party libs (libyaml, …) come along as usage
# requirements.
target_link_libraries(yetty_ffi PRIVATE
    -Wl,--whole-archive
    yetty_api_yplot   # public API facade — exports yetty_api_yplot_* for bindings
    -Wl,--no-whole-archive
    yetty_yplot_core  # yetty_yplot_render + dcs_bin_emit (GPU-less)
    yetty_ydraw_list
    yetty_yface
    yetty_ysdf
    yetty_yfsvm_core
    yetty_yexpr
    yetty_yclass
    yetty_ywire
    yetty_ycore
    Threads::Threads
    m
)

# Fail the link on any unresolved symbol so a missing transitive dependency
# shows up here at build time, not as a dlopen error in the bindings.
target_link_options(yetty_ffi PRIVATE -Wl,--no-undefined)

set_target_properties(yetty_ffi PROPERTIES
    OUTPUT_NAME yetty_ffi
    POSITION_INDEPENDENT_CODE ON
)

if(NOT MSVC)
    set_target_properties(yetty_ffi PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
