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

# Facades whose whole archive must be pulled in so their `yetty_api_*` stubs
# reach the bindings even though nothing in this TU references them.
set(YETTY_FFI_WHOLE_ARCHIVE_TARGETS
    yetty_api_yplot      # public API facade — exports yetty_api_yplot_* for bindings
    yetty_ydrawlist2     # v2 ydraw client interface: drawable/shape/font/text/list
    yetty_api_ydrawlist2 # its generated stubs + class accessors
    yetty_ysdf2          # v2 generated shape classes
    yetty_api_ysdf2      # their generated stubs + class accessors
    yetty_ycomplex2      # v2 complex-kind drawables (image/mesh/shadertoy/video)
    yetty_api_ycomplex2
)

# The real render/compute dependency targets are pulled normally to satisfy
# references; their transitive third-party libs (libyaml, …) come along as
# usage requirements.
target_link_libraries(yetty_ffi PRIVATE
    ${YETTY_FFI_WHOLE_ARCHIVE_TARGETS}
    yetty_yplot_core  # yetty_yplot_render + dcs_bin_emit (GPU-less)
    yetty_yimage_core
    yetty_ymesh_core
    yetty_yshadertoy_core
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
if(YETTY_ENABLE_FEATURE_YVIDEO)
    target_link_libraries(yetty_ffi PRIVATE yetty_yvideo_core)
endif()

# Force-load the facade archives and fail on unresolved symbols. GNU ld and
# Apple ld spell both features differently — GNU ld brackets the whole set
# with --whole-archive / --no-whole-archive; Apple ld needs one -force_load
# per archive path.
if(APPLE)
    foreach(yetty_ffi_target IN LISTS YETTY_FFI_WHOLE_ARCHIVE_TARGETS)
        target_link_options(yetty_ffi PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:${yetty_ffi_target}>")
    endforeach()
    target_link_options(yetty_ffi PRIVATE "LINKER:-undefined,error")
else()
    target_link_options(yetty_ffi PRIVATE "LINKER:--push-state,--whole-archive")
    foreach(yetty_ffi_target IN LISTS YETTY_FFI_WHOLE_ARCHIVE_TARGETS)
        target_link_options(yetty_ffi PRIVATE "$<TARGET_FILE:${yetty_ffi_target}>")
    endforeach()
    target_link_options(yetty_ffi PRIVATE
        "LINKER:--pop-state"
        "LINKER:--no-undefined"
    )
endif()

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
