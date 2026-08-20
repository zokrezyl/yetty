# ylexbor integration tests — drive the full HTML/CSS/JS pipeline through
# a minimal WPT-shaped harness and emit per-assertion JSON or TAP.
#
# Two executables get built here:
#
#   ylexbor-wpt-runner — generic harness driver. Takes --html <file> for
#                       a single page or --suite <dir> for a tree walk.
#                       Emits TAP or JSON-Lines.
#
# Tests under tests/*.html exercise the parts of the platform we
# actually implement (DOM lookups, classList, IDL attribute mirroring,
# CSS var() resolution, timers, the WebAPI globals stub). The runner
# also accepts a path to a real WPT checkout so the same binary scales
# from "smoke" to "every dom/ test we can survive".
#
# Activated by -DYETTY_ENABLE_FEATURE_TESTS=ON, gated on QuickJS being
# present (we need real JS to drive the harness; without it the runner
# can't make assertions). Tests are skipped instead of failing the
# config when QuickJS is unavailable.

if(NOT TARGET yetty_ylexbor)
    message(STATUS "ylexbor integration tests: yetty_ylexbor target missing — skipping")
    return()
endif()
if(NOT TARGET qjs)
    message(STATUS "ylexbor integration tests: QuickJS missing — skipping")
    return()
endif()

add_executable(ylexbor-wpt-runner
    wpt-runner.c
    # ytrace symbols are referenced by yetty_ydraw_list's drawable-list entry
    # path; the regular yetty binary pulls them in transitively, but a
    # standalone ctest binary needs them explicitly.
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

# QuickJS is needed for the result-readback path; otherwise the runner
# couldn't pull globalThis.__ylexbor_test_results__ back out.
#
# yetty_yplatform_core supplies the platform primitives ytrace.c calls
# (ymutex_*, format_timestamp, stderr_supports_color). The regular yetty
# binary pulls them in transitively; this standalone ctest binary, which
# compiles ytrace.c directly above, must link them explicitly or the
# final link fails with undefined yetty_yplatform_* references.
target_link_libraries(ylexbor-wpt-runner PRIVATE
    yetty_ylexbor
    yetty_ydraw_list
    yetty_ycore
    yetty_yplatform_core
    qjs
)

target_include_directories(ylexbor-wpt-runner PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Bake the harness path into the binary so `ctest` invocations don't
# need to pass --harness explicitly. Override on the command line for
# ad-hoc runs.
target_compile_definitions(ylexbor-wpt-runner PRIVATE
    YLEXBOR_TEST_HARNESS_JS=\"${CMAKE_CURRENT_SOURCE_DIR}/testharness.js\"
)

# Each canned test page becomes one ctest entry that exits 0 iff every
# assertion passed.
file(GLOB YLEXBOR_TEST_PAGES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.html")
foreach(page IN LISTS YLEXBOR_TEST_PAGES)
    get_filename_component(name "${page}" NAME_WE)
    add_test(
        NAME ylexbor_wpt_${name}
        COMMAND ylexbor-wpt-runner --tap --html "${page}"
    )
    # Failing harness assertions surface as a non-zero exit code; the
    # TAP output is captured by ctest's --output-on-failure flag.
    # Layer: integration (full HTML/CSS/JS pipeline through several local
    # modules); domain: browser. SKIP_RETURN_CODE keeps the 77 convention for
    # any page the runner declines to execute.
    #
    # The Lexbor/libcss CSS+DOM engine retains internal lifetime state
    # (lxb_dom_document_css_init, css_select_style) that LeakSanitizer flags.
    # That third-party family is suppressed narrowly via test/lsan.supp
    # (issue #414) rather than asan-skipped, so these still run under ASAN.
    set_tests_properties(ylexbor_wpt_${name} PROPERTIES
        LABELS                  "integration;browser"
        TIMEOUT                 30
        SKIP_RETURN_CODE        77
        FAIL_REGULAR_EXPRESSION "^not ok"
    )
endforeach()

# Convenience wrapper: `make ylexbor-wpt-tests` runs every test page in
# one shot with combined TAP output. Useful in CI before the per-test
# ctest entries fan out.
add_custom_target(ylexbor-wpt-tests
    COMMAND ylexbor-wpt-runner --tap --suite ${CMAKE_CURRENT_SOURCE_DIR}/tests
    DEPENDS ylexbor-wpt-runner
    USES_TERMINAL
)
