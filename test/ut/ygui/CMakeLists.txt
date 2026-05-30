# Unit tests for the ygui widget toolkit.
#
# Exercises class registration, instance allocation, data slice access,
# method dispatch, super chains, flex layout, the clickable mixin, and
# end-to-end two-pass wire emission for figure widgets.

enable_testing()

set(YGUI_TEST_SHIM_SOURCES
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

add_executable(ygui-object-test
    object-test.c
    ${YGUI_TEST_SHIM_SOURCES}
)

target_include_directories(ygui-object-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(ygui-object-test PRIVATE
    ygui
    yetty_ycore
    yetty_ydraw_core
    m
)

add_test(NAME ygui_object COMMAND ygui-object-test)

add_executable(ygui-layout-test
    layout-test.c
    ${YGUI_TEST_SHIM_SOURCES}
)

target_include_directories(ygui-layout-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(ygui-layout-test PRIVATE
    ygui
    yetty_ycore
    yetty_ydraw_core
    m
)

add_test(NAME ygui_layout COMMAND ygui-layout-test)

add_executable(ygui-figure-test
    figure-test.c
    ${YGUI_TEST_SHIM_SOURCES}
)

target_include_directories(ygui-figure-test PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(ygui-figure-test PRIVATE
    ygui
    yetty_ycore
    yetty_yplatform_core
    yetty_yface
    m
)

add_test(NAME ygui_figure COMMAND ygui-figure-test)
