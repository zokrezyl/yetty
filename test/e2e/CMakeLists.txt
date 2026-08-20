# E2E lane: launched-yetty + yctl-driven smoke. Needs a real display/GPU, so it
# is labeled e2e/display/slow (excluded from the fast + asan gates) and the
# script itself self-skips (exit 77) when no display or yetty binary is present.
#
# YETTY points at the build-dir binary path (not a $<TARGET_FILE:yetty> genexp):
# the yetty executable target is defined after the test tree is added, so a
# target reference here would be empty. The script checks the path exists.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME e2e_yctl_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/yctl-smoke.py)
    set_tests_properties(e2e_yctl_smoke PROPERTIES
        ENVIRONMENT      "YETTY=${CMAKE_BINARY_DIR}/yetty;YCTL=${YETTY_ROOT}/tools/yctl-client/yctl.py"
        LABELS           "e2e;display;slow"
        TIMEOUT          120
        SKIP_RETURN_CODE 77)

    # Headful render + capture + tile-diff (#424; also exercises the #425
    # screenshot capture path). Renders content, screenshots two frames, and
    # asserts they differ at tile granularity. Self-skips headless.
    add_test(NAME e2e_render_tilediff
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/render-tilediff.py)
    set_tests_properties(e2e_render_tilediff PROPERTIES
        ENVIRONMENT      "YETTY=${CMAKE_BINARY_DIR}/yetty;YCTL=${YETTY_ROOT}/tools/yctl-client/yctl.py"
        LABELS           "e2e;render;display;slow"
        TIMEOUT          180
        SKIP_RETURN_CODE 77)

    # Headful ybrowser image-position guard: renders images.html interactive,
    # screenshots, and asserts the images land in their laid-out strips (guards
    # the figure-anchor double-count that pushed interactive images ~2x down).
    # Self-skips headless.
    add_test(NAME e2e_ybrowser_image_position
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ybrowser-image-position.py)
    set_tests_properties(e2e_ybrowser_image_position PROPERTIES
        ENVIRONMENT      "YETTY=${CMAKE_BINARY_DIR}/yetty;YBROWSER=${CMAKE_BINARY_DIR}/tools/ybrowser/ybrowser;YCTL=${YETTY_ROOT}/tools/yctl-client/yctl.py"
        LABELS           "e2e;render;display;slow"
        TIMEOUT          180
        SKIP_RETURN_CODE 77)

    # Nightly ymux grid GPU-readback (#699.5): daemon -> attach bridge ->
    # vtsink lane -> yscene grid -> compositor -> screenshot; asserts a
    # red-background run reaches the framebuffer (font-independent pixel
    # count). Self-skips headless.
    add_test(NAME e2e_ymux_grid_readback
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ymux-grid-readback.py)
    set_tests_properties(e2e_ymux_grid_readback PROPERTIES
        ENVIRONMENT      "YETTY=${CMAKE_BINARY_DIR}/yetty;YMUX=${CMAKE_BINARY_DIR}/tools/ymux/ymux;YCTL=${YETTY_ROOT}/tools/yctl-client/yctl.py"
        LABELS           "e2e;render;display;slow;ymux"
        TIMEOUT          180
        SKIP_RETURN_CODE 77)

    # Attach-level epoch-reset ordering (#699 review #14): mid-session
    # `ymux recover` forces the daemon's slow-client recovery; the bridge
    # must reset the receiver BEFORE the fresh epoch republishes — content
    # painted after the recovery must reach the framebuffer. Self-skips
    # headless.
    add_test(NAME e2e_ymux_reset_ordering
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ymux-reset-ordering.py)
    set_tests_properties(e2e_ymux_reset_ordering PROPERTIES
        ENVIRONMENT      "YETTY=${CMAKE_BINARY_DIR}/yetty;YMUX=${CMAKE_BINARY_DIR}/tools/ymux/ymux;YCTL=${YETTY_ROOT}/tools/yctl-client/yctl.py"
        LABELS           "e2e;render;display;slow;ymux"
        TIMEOUT          180
        SKIP_RETURN_CODE 77)

    # Overlay chrome scroll-mode e2e (#699 cycle 18/19): real click on the
    # indicator strip -> chrome focus -> real arrow keys -> daemon scroll
    # consumer -> marker reappears; then Down back to bottom, release click,
    # and a typed red marker PROVES keys fall through to the pane app again.
    # Self-skips headless.
    add_test(NAME e2e_ymux_overlay_scroll
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ymux-overlay-scroll.py)
    set_tests_properties(e2e_ymux_overlay_scroll PROPERTIES
        ENVIRONMENT      "YETTY=${CMAKE_BINARY_DIR}/yetty;YMUX=${CMAKE_BINARY_DIR}/tools/ymux/ymux;YCTL=${YETTY_ROOT}/tools/yctl-client/yctl.py"
        LABELS           "e2e;render;display;slow;ymux"
        TIMEOUT          240
        SKIP_RETURN_CODE 77)

    # Real-ycat figure render e2e (#699 cycle 19, the blocking-regression
    # gate): the built ycat runs INSIDE an attached ymux pane on committed
    # fixtures (tall SVG + markdown); framebuffer readback asserts the
    # figure's pixels, the scene scrolled by the document height, and
    # terminal text renders below the figure. Self-skips headless.
    add_test(NAME e2e_ymux_ycat_render
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ymux-ycat-render.py)
    set_tests_properties(e2e_ymux_ycat_render PROPERTIES
        ENVIRONMENT      "YETTY=${CMAKE_BINARY_DIR}/yetty;YMUX=${CMAKE_BINARY_DIR}/tools/ymux/ymux;YCAT=${CMAKE_BINARY_DIR}/tools/ycat/ycat;YCTL=${YETTY_ROOT}/tools/yctl-client/yctl.py;FIXTURES=${YETTY_ROOT}/test/fixtures/ymux"
        LABELS           "e2e;render;display;slow;ymux"
        TIMEOUT          240
        SKIP_RETURN_CODE 77)

    # Real-ycat MARKDOWN reservation e2e (#699 cycle 21): a fresh, clean
    # ymux pane renders the compact markdown fixture between an ABOVE and a
    # BELOW marker; framebuffer readback asserts the document reserved real
    # height (the zero scene_max_y regression gate). Self-skips headless.
    add_test(NAME e2e_ymux_markdown_reserve
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ymux-markdown-reserve.py)
    set_tests_properties(e2e_ymux_markdown_reserve PROPERTIES
        ENVIRONMENT      "YETTY=${CMAKE_BINARY_DIR}/yetty;YMUX=${CMAKE_BINARY_DIR}/tools/ymux/ymux;YCAT=${CMAKE_BINARY_DIR}/tools/ycat/ycat;YCTL=${YETTY_ROOT}/tools/yctl-client/yctl.py;FIXTURES=${YETTY_ROOT}/test/fixtures/ymux"
        LABELS           "e2e;render;display;slow;ymux"
        TIMEOUT          180
        SKIP_RETURN_CODE 77)

    # Generated Python connection facade against the real libyetty_ffi.so
    # (#439): dynamic channels, flow control, chunking, asyncio driver.
    # Headless; self-skips when the FFI PIC build tree is absent.
    add_test(NAME e2e_ffi_connection_smoke
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/ffi-connection-smoke.py)
    set_tests_properties(e2e_ffi_connection_smoke PROPERTIES
        LABELS           "e2e;ffi"
        TIMEOUT          60
        SKIP_RETURN_CODE 77)
endif()
