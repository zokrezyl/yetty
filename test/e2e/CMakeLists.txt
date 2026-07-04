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
