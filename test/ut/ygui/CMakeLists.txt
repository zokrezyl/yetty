# ygui unit tests
#
# Runs as standalone C programs using ctest. Enable with
# -DYETTY_ENABLE_FEATURE_TESTS=ON.

enable_testing()

set(YGUI_TEST_SHIM_SOURCES
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

add_executable(ygui-flex-layout-test
    ygui-flex-layout-test.c
    ${YGUI_TEST_SHIM_SOURCES}
)
target_include_directories(ygui-flex-layout-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ygui-flex-layout-test PRIVATE
    ygui yetty_ycore m)
add_test(NAME ygui_flex_layout COMMAND ygui-flex-layout-test)

add_executable(ygui-yplot-test
    ygui-yplot-test.c
    ${YGUI_TEST_SHIM_SOURCES}
)
target_include_directories(ygui-yplot-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ygui-yplot-test PRIVATE
    ygui yetty_ycore m)
add_test(NAME ygui_yplot COMMAND ygui-yplot-test)
