# ybrowser unit tests — pin layout geometry so regressions like
# "width: 100% renders 256px wide" or "float: right ends up at x=0"
# fail fast in CI instead of weeks later when someone notices the
# Wikipedia page looks wrong.
#
# Headless C tests (no Chrome, no GPU, no network) → layer label `golden`,
# domain `browser`. Registered through yetty_add_c_test(); the `workpool` shim
# supplies ybrowser's async image-fetch worker-pool symbols.
#
# These exercise the libcss/Lexbor CSS+DOM stack, which retains internal
# lifetime state LeakSanitizer flags (css_select_style, lxb_dom_document_css_init
# via yetty_ylexbor_create, cb_node_classes). The tests create+destroy their
# instances and free select results; the leaks are third-party engine internals.
# Suppressed narrowly via test/lsan.supp (issue #414) rather than asan-skipped,
# so the tests still run under ASAN and catch any new Yetty-owned leak.

yetty_add_c_test(
    NAME    ybrowser_layout
    SOURCES ybrowser-layout-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  golden browser
    TIMEOUT 30)

# DOM-model contract (#422): element attribute readback (id/class/href/data-*),
# text-content survival through the box tree, tag/class counting. Deterministic,
# offline — no geometry pinning, so labelled `contract` rather than `golden`.
yetty_add_c_test(
    NAME    ybrowser_dom
    SOURCES ybrowser-dom-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  contract browser
    TIMEOUT 30)

# Viewport coherence (dynamic): window.innerWidth / matchMedia / documentElement
# client metrics track the engine viewport LIVE across set_viewport(), and a
# retained MediaQueryList re-evaluates its `matches` getter. Offline; self-skips
# when the build carries no JS engine.
yetty_add_c_test(
    NAME    ybrowser_viewport
    SOURCES ybrowser-viewport-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  contract browser
    TIMEOUT 30)

# Storage isolation (#444): localStorage / sessionStorage / document.cookie
# are engine-owned — two live engines share none of them. Offline; self-skips
# when the build carries no JS engine.
yetty_add_c_test(
    NAME    ybrowser_storage_isolation
    SOURCES ybrowser-storage-isolation-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  contract browser
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

# Network loader: request/response fetch round-trip + resource-cache hit.
# Hits the network (see the `network` label); self-skips offline.
yetty_add_c_test(
    NAME    ybrowser_loader
    SOURCES ybrowser-loader-test.c
    LIBS    yetty_ybrowser yetty_ydraw yetty_ydraw_core yetty_ycore yetty_yplatform_core
    SHIMS   trace workpool
    LABELS  network browser
    TIMEOUT 60)

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

    # Anchor suite: whole-page fixtures with stamped data-test anchors,
    # compared against committed Chrome-generated geometry references
    # (test/ybrowser/anchors). Catches the layout-class divergences (grid
    # collapse, flex mis-splits) that per-feature WPT pins are too small to
    # exhibit. Offline and headless — the Chrome oracle only runs at
    # fixture-authoring time, never in CI.
    add_test(NAME ybrowser_anchors
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../anchors/run.py)
    set_tests_properties(ybrowser_anchors PROPERTIES
        ENVIRONMENT      "YBROWSER=$<TARGET_FILE:ybrowser>"
        LABELS           "golden;browser;anchors"
        TIMEOUT          120
        SKIP_RETURN_CODE 77)

    # Paint-order (CSS stacking) test: pins the composite stacking-key painter
    # against the cases a single scalar level cannot get right at once —
    # early-in-DOM overlay over later content, root canvas vs negative-z,
    # z-index-child ESCAPING an auto-z wrapper above a lower-z sibling, and
    # a sibling CONTAINED above an explicit-z wrapper's higher-z child.
    # Painter's order only (no depth buffer), so this is paint-order, not
    # geometry — the WPT/anchor suites cannot see it. Offline, headless.
    add_test(NAME ybrowser_stacking_order
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../render/stacking-order.py)
    set_tests_properties(ybrowser_stacking_order PROPERTIES
        ENVIRONMENT      "YBROWSER=$<TARGET_FILE:ybrowser>"
        LABELS           "golden;browser"
        TIMEOUT          90
        SKIP_RETURN_CODE 77)

    # Iframe rendering test: asserts a nested browsing context's document
    # (srcdoc, so offline) is composited into the iframe box — its text reaches
    # the parent's paint stream. Also covers one level of iframe nesting. This
    # is compositing, not geometry, so the WPT/anchor suites cannot see it.
    add_test(NAME ybrowser_iframe_render
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../render/iframe-render.py)
    set_tests_properties(ybrowser_iframe_render PROPERTIES
        ENVIRONMENT      "YBROWSER=$<TARGET_FILE:ybrowser>"
        LABELS           "golden;browser"
        TIMEOUT          90
        SKIP_RETURN_CODE 77)
endif()
