# ybrowser unit tests — pin layout geometry so regressions like
# "width: 100% renders 256px wide" or "float: right ends up at x=0"
# fail fast in CI instead of weeks later when someone notices the
# Wikipedia page looks wrong.
#
# Runs as standalone C programs using ctest. Enable with
# -DYETTY_ENABLE_FEATURE_TESTS=ON.

enable_testing()

set(YBROWSER_TEST_SHIM_SOURCES
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

add_executable(ybrowser-layout-test
    ybrowser-layout-test.c
    ${YBROWSER_TEST_SHIM_SOURCES}
)
target_include_directories(ybrowser-layout-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ybrowser-layout-test PRIVATE
    yetty_ybrowser yetty_ypaint yetty_ypaint_core yetty_ycore yetty_yplatform_core m)
add_test(NAME ybrowser_layout COMMAND ybrowser-layout-test)

# Wikipedia rendering test — loads the actual Photography page snapshot
# (committed alongside this CMakeLists.txt) and asserts geometric facts
# about its layout. This is the canonical regression net for the
# user-reported "garbage glyphs / all images upper-left" bugs.
add_executable(ybrowser-wikipedia-test
    ybrowser-wikipedia-test.c
    ${YBROWSER_TEST_SHIM_SOURCES}
)
target_include_directories(ybrowser-wikipedia-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ybrowser-wikipedia-test PRIVATE
    yetty_ybrowser yetty_ypaint yetty_ypaint_core yetty_ycore yetty_yplatform_core m)
target_compile_definitions(ybrowser-wikipedia-test PRIVATE
    YBROWSER_WIKI_PAGE=\"${CMAKE_CURRENT_SOURCE_DIR}/photography.html\"
)
add_test(NAME ybrowser_wikipedia COMMAND ybrowser-wikipedia-test)

# ybrowser-inline-test — pins inline text behaviour (list markers, wrap,
# UTF-8 safety, weight/italic/underline propagation). Exercises the new
# yetty_ylexbor_test_box_info_at introspection helper.
add_executable(ybrowser-inline-test
    ybrowser-inline-test.c
    ${YBROWSER_TEST_SHIM_SOURCES}
)
target_include_directories(ybrowser-inline-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ybrowser-inline-test PRIVATE
    yetty_ybrowser yetty_ypaint yetty_ypaint_core yetty_ycore yetty_yplatform_core m)
add_test(NAME ybrowser_inline COMMAND ybrowser-inline-test)

