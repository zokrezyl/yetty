# ygui2 wire-cost contract test — headless (sink capture, no PTY/GPU).
yetty_add_c_test(
    NAME    ygui2_wire
    SOURCES wire-test.c
    LIBS    yetty_ygui2 yetty_ysdf yetty_ydraw_list yetty_yface yetty_yclass yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract wire ygui2
    TIMEOUT 30)

# ygui2 Python binding test — headless ctypes suite against libyetty_ffi.so:
# generated constructor routing, writable out-buffers, builder/generated
# signature parity, callback capture + release, wrapper liveness, close.
if(TARGET yetty_ffi)
    find_program(YETTY_TEST_PYTHON3 python3 PATHS /usr/bin NO_DEFAULT_PATH)
    if(NOT YETTY_TEST_PYTHON3)
        find_program(YETTY_TEST_PYTHON3 python3)
    endif()
    if(YETTY_TEST_PYTHON3)
        add_test(NAME ygui2_python_binding
            COMMAND ${YETTY_TEST_PYTHON3}
                    ${CMAKE_CURRENT_SOURCE_DIR}/python-binding-test.py)
        set_tests_properties(ygui2_python_binding PROPERTIES
            ENVIRONMENT "PYTHONPATH=${YETTY_ROOT}/bindings/python;YETTY_FFI_LIB=$<TARGET_FILE:yetty_ffi>"
            LABELS "contract;ffi;ygui2"
            TIMEOUT 60)
    endif()

    # ygui2 Lua binding test — the LuaJIT wrapper's contracts: deferred
    # close from widget/sink callbacks, reservation-mode exposure, error
    # cause chains, liveness.
    find_program(YETTY_TEST_LUAJIT luajit)
    if(YETTY_TEST_LUAJIT)
        add_test(NAME ygui2_lua_binding
            COMMAND ${CMAKE_COMMAND} -E env
                    "LUA_PATH=${YETTY_ROOT}/bindings/lua/?.lua$<SEMICOLON>$<SEMICOLON>"
                    "YETTY_FFI_LIB=$<TARGET_FILE:yetty_ffi>"
                    ${YETTY_TEST_LUAJIT}
                    ${CMAKE_CURRENT_SOURCE_DIR}/lua-binding-test.lua)
        set_tests_properties(ygui2_lua_binding PROPERTIES
            LABELS "contract;ffi;ygui2"
            TIMEOUT 60)
    endif()

    # ygui2 node binding test — the koffi wrapper's contracts (same set as
    # the Lua test). Needs koffi: gate on bindings/typescript's installed
    # node_modules (cd bindings/typescript && npm install).
    find_program(YETTY_TEST_NODE node)
    if(YETTY_TEST_NODE AND EXISTS ${YETTY_ROOT}/bindings/typescript/node_modules/koffi)
        add_test(NAME ygui2_ts_binding
            COMMAND ${YETTY_TEST_NODE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/ts-binding-test.mjs)
        set_tests_properties(ygui2_ts_binding PROPERTIES
            ENVIRONMENT "YETTY_FFI_LIB=$<TARGET_FILE:yetty_ffi>"
            LABELS "contract;ffi;ygui2"
            TIMEOUT 60)
    endif()

    # ygui2 Go binding test — the cgo wrapper's contracts, plus the
    # explicit-zero option semantics (zero pad reset, dialog at (0,0)).
    find_program(YETTY_TEST_GO go)
    if(YETTY_TEST_GO)
        add_test(NAME ygui2_go_binding
            COMMAND ${CMAKE_COMMAND} -E env
                    "CGO_LDFLAGS=-L$<TARGET_FILE_DIR:yetty_ffi> -lyetty_ffi"
                    "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:yetty_ffi>"
                    ${YETTY_TEST_GO} run .
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/go-binding-test)
        set_tests_properties(ygui2_go_binding PROPERTIES
            LABELS "contract;ffi;ygui2"
            TIMEOUT 120)
    endif()
endif()
