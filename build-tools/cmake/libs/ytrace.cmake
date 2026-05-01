# ytrace - logging with runtime control
# Depends on spdlog
if(TARGET ytrace::ytrace)
    return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/spdlog.cmake)

if(EMSCRIPTEN OR YETTY_ANDROID)
    # No control socket on Emscripten (no filesystem) or Android (sandboxed fs)
    set(YTRACE_BUILD_TOOLS_OPT "YTRACE_BUILD_TOOLS OFF")
else()
    set(YTRACE_BUILD_TOOLS_OPT "YTRACE_BUILD_TOOLS ON")
endif()

# Allow command-line override of logging levels (default: all ON)
# yinfo builds pass -DYTRACE_ENABLE_YTRACE=0 -DYTRACE_ENABLE_YDEBUG=0
if(NOT DEFINED YTRACE_ENABLE_YTRACE)
    set(YTRACE_ENABLE_YTRACE ON)
endif()
if(NOT DEFINED YTRACE_ENABLE_YDEBUG)
    set(YTRACE_ENABLE_YDEBUG ON)
endif()
if(NOT DEFINED YTRACE_ENABLE_YINFO)
    set(YTRACE_ENABLE_YINFO ON)
endif()
if(NOT DEFINED YTRACE_ENABLE_YWARN)
    set(YTRACE_ENABLE_YWARN ON)
endif()
if(NOT DEFINED YTRACE_ENABLE_YLOG)
    set(YTRACE_ENABLE_YLOG ON)
endif()
if(NOT DEFINED YTRACE_ENABLE_YFUNC)
    set(YTRACE_ENABLE_YFUNC ON)
endif()
if(NOT DEFINED YTRACE_ENABLE_YTEST)
    set(YTRACE_ENABLE_YTEST ON)
endif()

CPMAddPackage(
    NAME ytrace
    GITHUB_REPOSITORY zokrezyl/ytrace
    GIT_TAG v0.0.13
    OPTIONS
        ${YTRACE_BUILD_TOOLS_OPT}
        "YTRACE_BUILD_EXAMPLES OFF"
        "YTRACE_ENABLE_YLOG ${YTRACE_ENABLE_YLOG}"
        "YTRACE_ENABLE_YTRACE ${YTRACE_ENABLE_YTRACE}"
        "YTRACE_ENABLE_YDEBUG ${YTRACE_ENABLE_YDEBUG}"
        "YTRACE_ENABLE_YINFO ${YTRACE_ENABLE_YINFO}"
        "YTRACE_ENABLE_YWARN ${YTRACE_ENABLE_YWARN}"
        "YTRACE_ENABLE_YFUNC ${YTRACE_ENABLE_YFUNC}"
        "YTRACE_ENABLE_YTEST ${YTRACE_ENABLE_YTEST}"
)

# Disable control socket on Emscripten (no filesystem) or Android (sandboxed fs)
if(EMSCRIPTEN OR YETTY_ANDROID)
    target_compile_definitions(ytrace INTERFACE YTRACE_NO_CONTROL_SOCKET)
endif()

# Propagate log-level switches to the C ytrace implementation
# (src/yetty/ytrace/ytrace.c + include/yetty/ytrace.h). That header uses separate
# compile-time switches (YTRACE_C_ENABLE_*) from the external ytrace C++ lib,
# so the YTRACE_ENABLE_* options above do not disable ydebug/ytrace in our C
# macros by themselves. Emit project-wide compile definitions so every TU
# that includes <yetty/ytrace.h> sees the correct level.
if(NOT YTRACE_ENABLE_YTRACE)
    add_compile_definitions(YTRACE_C_ENABLE_TRACE=0)
endif()
if(NOT YTRACE_ENABLE_YDEBUG)
    add_compile_definitions(YTRACE_C_ENABLE_DEBUG=0)
endif()
if(NOT YTRACE_ENABLE_YINFO)
    add_compile_definitions(YTRACE_C_ENABLE_INFO=0)
endif()
if(NOT YTRACE_ENABLE_YWARN)
    add_compile_definitions(YTRACE_C_ENABLE_WARN=0)
endif()
