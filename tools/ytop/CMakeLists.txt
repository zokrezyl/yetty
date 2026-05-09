# ytop — top-style live process monitor demoing ygui's TABLE widget
# alongside per-core CPU progress bars.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # /proc/* is Linux-only. Skip silently elsewhere — the rest of the
    # tools/ tree still builds on macOS / Windows.
    return()
endif()

# ytrace + yplatform/shared/{thread,term} are compiled per-final-target
# (they're not standalone libraries) — pull them in directly.
add_executable(ytop
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

target_include_directories(ytop PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# ygui pulls in ypaint-core, ysdf, ycore, yface, yfont, yrender, libuv as
# transitive PUBLIC deps — no need to list them again here.
target_link_libraries(ytop PRIVATE ygui)

if(NOT MSVC)
    target_compile_options(ytop PRIVATE -Wall -Wextra)
endif()
