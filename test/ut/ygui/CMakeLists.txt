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

# End-to-end wire-validation tests: build a ygui tree, render headless,
# feed the produced wire bytes into a yfigure_container + ygrid (the
# receiver), and verify the resulting state via the yfigure ops->dump.
add_executable(ygui-wire-test
    ygui-wire-test.c
    ${YGUI_TEST_SHIM_SOURCES}
)
target_include_directories(ygui-wire-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ygui-wire-test PRIVATE
    ygui yetty_yfigure yetty_ygrid yetty_ydraw_core yetty_ysdf
    yetty_ydraw_factory yetty_ycore m)
add_test(NAME ygui_wire COMMAND ygui-wire-test)

# Regression test: mirror ygreeter's Elements tab and drive open/close
# transitions on the collapsing-header sections. Catches the "section
# opens but children are invisible" symptom.
add_executable(ygui-elements-tab-test
    ygui-elements-tab-test.c
    ${YGUI_TEST_SHIM_SOURCES}
)
target_include_directories(ygui-elements-tab-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)
target_link_libraries(ygui-elements-tab-test PRIVATE
    ygui yetty_yfigure yetty_ygrid yetty_ydraw_core yetty_ysdf
    yetty_ydraw_factory yetty_ycore m)
add_test(NAME ygui_elements_tab COMMAND ygui-elements-tab-test)
