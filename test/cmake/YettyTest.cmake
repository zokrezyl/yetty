# YettyTest.cmake — one place that owns C test registration.
#
# Before this helper, every test/ut/<module>/CMakeLists.txt repeated the same
# boilerplate: the include dirs, the ytrace/platform shim source list, the
# add_executable / target_link_libraries / add_test / set_tests_properties
# sequence, and the label + timeout policy. yetty_add_c_test() centralises all
# of it so a new test is one call, and the fast-lane label/timeout/skip policy
# stays correct-by-construction.
#
# Usage:
#
#   yetty_add_c_test(
#       NAME    ygrid_wire                       # ctest name (also the label seed)
#       SOURCES ygrid-wire-test.c                # test sources (relative to caller)
#       LIBS    yetty_ygrid yetty_ydraw_core ... # libraries to link
#       SHIMS   trace platform_thread platform_term
#       LABELS  contract wire                    # one layer label + optional domain
#       TIMEOUT 30)                              # seconds
#
# The executable is named "<NAME>-test" and the ctest test is named "<NAME>".
# libm is always linked (tests routinely pull in math); SHIMS names map to the
# standalone-binary shim sources listed below.
#
# Every registered test gets:
#     LABELS           "${LABELS}"
#     TIMEOUT          ${TIMEOUT}   (default 30)
#     SKIP_RETURN_CODE 77           (so a test that exits 77 counts as skipped)
#
# The `77` skip convention pairs with ytest.h's YTEST_SKIP / ytest_end.

# Map a short SHIMS token to the standalone-test shim source it stands for.
# These are the platform/trace symbols a per-module static lib references
# internally but does not itself define — the main yetty binary pulls them in
# transitively, a lone ctest executable must compile them directly.
function(_yetty_test_shim_source token out_var)
    if(token STREQUAL "trace")
        set(${out_var} "${YETTY_ROOT}/src/yetty/ytrace/ytrace.c" PARENT_SCOPE)
    elseif(token STREQUAL "platform_thread")
        set(${out_var} "${YETTY_ROOT}/src/yetty/yplatform/thread/default.c" PARENT_SCOPE)
    elseif(token STREQUAL "platform_term")
        set(${out_var} "${YETTY_ROOT}/src/yetty/yplatform/term/default.c" PARENT_SCOPE)
    elseif(token STREQUAL "workpool")
        set(${out_var} "${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "yetty_add_c_test: unknown SHIM '${token}' "
                            "(known: trace platform_thread platform_term workpool)")
    endif()
endfunction()

function(yetty_add_c_test)
    set(one_value_args NAME TIMEOUT)
    set(multi_value_args SOURCES LIBS SHIMS LABELS)
    cmake_parse_arguments(ARG "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ARG_NAME)
        message(FATAL_ERROR "yetty_add_c_test: NAME is required")
    endif()
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "yetty_add_c_test(${ARG_NAME}): SOURCES is required")
    endif()
    if(NOT ARG_TIMEOUT)
        set(ARG_TIMEOUT 30)
    endif()

    # Resolve SHIMS tokens to concrete shim source paths.
    set(shim_sources "")
    foreach(shim IN LISTS ARG_SHIMS)
        _yetty_test_shim_source(${shim} shim_source)
        list(APPEND shim_sources "${shim_source}")
    endforeach()

    set(exe "${ARG_NAME}-test")
    add_executable(${exe} ${ARG_SOURCES} ${shim_sources})

    target_include_directories(${exe} PRIVATE
        ${YETTY_ROOT}/include
        ${YETTY_ROOT}/src
        ${YETTY_ROOT}/test/support
    )

    # libm is universally wanted by these tests; link it once here so callers
    # never repeat it.
    target_link_libraries(${exe} PRIVATE ${ARG_LIBS} m)

    add_test(NAME ${ARG_NAME} COMMAND ${exe})
    set_tests_properties(${ARG_NAME} PROPERTIES
        LABELS           "${ARG_LABELS}"
        TIMEOUT          ${ARG_TIMEOUT}
        SKIP_RETURN_CODE 77
    )
endfunction()
