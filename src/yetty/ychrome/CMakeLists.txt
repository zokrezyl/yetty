# ychrome — yclass class `ychrome:chrome`: a widget-free window-chrome gesture
# engine (caption drag → move, edges → resize, double-click → maximize). It
# drives a yplatform:window_manager object and depends on neither ygui nor yui,
# so any app can get draggable/resizable/maximizable borderless windows.
#
# chrome.c is the hand-written annotated source (#includes chrome.gen.c at the
# foot); methods.gen.c + rpc.gen.c are codegen output. Pure logic — no GLFW, no
# rendering. The window_manager gesture symbols it calls are resolved by the
# consumer, which links yetty_yplatform_window_manager.
add_library(yetty_ychrome STATIC
    chrome.c
    methods.gen.c
    rpc.gen.c
)

target_include_directories(yetty_ychrome PUBLIC
    ${YETTY_ROOT}/include
)
# Internal codegen headers (methods.gen.h) live under src/yetty/ychrome/.
target_include_directories(yetty_ychrome PRIVATE ${YETTY_ROOT}/src)

target_link_libraries(yetty_ychrome PUBLIC
    yetty_ycore
    yetty_yclass
)

# chrome.c carries C23 `[[clang::annotate(...)]]` attributes.
if(NOT MSVC)
    set_target_properties(yetty_ychrome PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
