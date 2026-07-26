# ymgui_frontend — links into the client app, not into yetty itself.
#
# Translation units of our own:
#   ymgui_encode.c         — pure C. non-blocking OSC write helper.
#   imgui_impl_yetty.cpp   — thin C++ shim on top of Dear ImGui.
#
# Dear ImGui itself comes via the imgui_core CPM target (no vendoring).
#
# The libuv-backed event loop now lives in src/yclient-lib (target
# yetty_yclient) — shared with ygui / yrich / ycat. We link it here so
# Dear ImGui apps that want the async path get it via this single target.

add_library(ymgui_frontend STATIC
    ymgui_encode.c
    imgui_impl_yetty.cpp
)

target_include_directories(ymgui_frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
)

target_link_libraries(ymgui_frontend PUBLIC
    imgui_core
    yetty_yface
    yetty_ycore
)
if(NOT EMSCRIPTEN)
    # yetty_yclient: libuv event loop. yetty_yclass: the connect runtime
    # (rpc_connect_fds / _disconnect / session_translate_class). yetty_yfigure:
    # typed container stubs (create_child / set_child_rect / apply_child_body /
    # …). yetty_api_yterminal: figure_root_container navigation from the session
    # root. The figure-tree records and per-figure body uploads travel as typed
    # yclass-RPC calls on the root container proxy instead of hand-built
    # YCOMPOSITOR_BIN envelopes.
    target_link_libraries(ymgui_frontend PUBLIC yetty_yclient yetty_yclass yetty_yfigure
                          yetty_api_yterminal)
endif()

set_target_properties(ymgui_frontend PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Public headers consumers need:
#   <yetty/ymgui/wire.h>              (installed with core headers)
#   <yetty/yclient-lib/event-loop.h>  (installed by yclient-lib)
#   "imgui_impl_yetty.h"              (this dir)
#   "ymgui_encode.h"                  (this dir — optional, for custom transports)

install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_impl_yetty.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ymgui_encode.h
    DESTINATION include/yetty/ymgui)

install(TARGETS ymgui_frontend ARCHIVE DESTINATION lib)
