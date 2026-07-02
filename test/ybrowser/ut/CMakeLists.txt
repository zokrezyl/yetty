# ybrowser unit tests — pin layout geometry so regressions like
# "width: 100% renders 256px wide" or "float: right ends up at x=0"
# fail fast in CI instead of weeks later when someone notices the
# Wikipedia page looks wrong.
#
# Headless C tests (no Chrome, no GPU, no network) → layer label `golden`,
# domain `browser`. Registered through yetty_add_c_test(); the `workpool` shim
# supplies ybrowser's async image-fetch worker-pool symbols.

yetty_add_c_test(
    NAME    ybrowser_layout
    SOURCES ybrowser-layout-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  golden browser
    TIMEOUT 30)

# Wikipedia rendering test — loads the actual Photography page snapshot
# (committed alongside this CMakeLists.txt) and asserts geometric facts about
# its layout. The canonical regression net for the user-reported "garbage
# glyphs / all images upper-left" bugs.
yetty_add_c_test(
    NAME    ybrowser_wikipedia
    SOURCES ybrowser-wikipedia-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  golden browser
    TIMEOUT 30)
target_compile_definitions(ybrowser_wikipedia-test PRIVATE
    YBROWSER_WIKI_PAGE=\"${CMAKE_CURRENT_SOURCE_DIR}/photography.html\")

# Inline text behaviour (list markers, wrap, UTF-8 safety, weight/italic/
# underline propagation) via the yetty_ylexbor_test_box_info_at helper.
yetty_add_c_test(
    NAME    ybrowser_inline
    SOURCES ybrowser-inline-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  golden browser
    TIMEOUT 30)

# Pins the serialized ydraw drawable list ybrowser emits (image bounds,
# text-decoration rects) with no Chrome and no GPU.
yetty_add_c_test(
    NAME    ybrowser_paint
    SOURCES ybrowser-paint-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  golden browser
    TIMEOUT 30)

# Python-driven suites that need the built ybrowser CLI. Both self-skip
# (exit 77) when the binary is unavailable.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    # Headless render-sanity smoke test over a handful of real sites: asserts
    # the laid-out box count stays above a per-site floor. It hits the network,
    # so it carries the "network" label (exclude with `ctest -LE network`) and
    # a generous timeout.
    add_test(NAME ybrowser_render
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../render/run.py)
    set_tests_properties(ybrowser_render PROPERTIES
        ENVIRONMENT      "YBROWSER=$<TARGET_FILE:ybrowser>"
        LABELS           "network;browser"
        TIMEOUT          120
        SKIP_RETURN_CODE 77)

    # WPT-shaped geometry suite: drives ybrowser --dump-boxes over the local
    # fixtures under test/ybrowser/wpt and asserts post-layout box geometry.
    # Fully offline and headless, so it stays in the fast lane; `make test-wpt`
    # selects it via the `wpt` label.
    add_test(NAME ybrowser_wpt
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../wpt/run.py)
    set_tests_properties(ybrowser_wpt PROPERTIES
        ENVIRONMENT      "YBROWSER=$<TARGET_FILE:ybrowser>"
        LABELS           "golden;browser;wpt"
        TIMEOUT          120
        SKIP_RETURN_CODE 77)
endif()
