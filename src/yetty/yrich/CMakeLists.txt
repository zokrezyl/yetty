# yrich — document-centric rich content (spreadsheet, slides, ydoc).
#
# Two targets:
#   yetty_yrich      — the lean document model (no GPU / ygui deps).
#   yetty_yrich_app  — the ygui-decorated editor shells + the standalone
#                      window host (yinit + yframework + in-process
#                      yfigure container fed via the ygui framework's
#                      yclass slot path). This is what the thin ydoc /
#                      ysheet / yslide tools link. It is an OBJECT library
#                      because the inlined yinit/yframework/platform
#                      sources carry file-static state that a static
#                      archive would let the linker prune — same reason
#                      the ygui demo runner is an OBJECT library.
# Annotated yclass sources (each #includes its own <stem>.gen.c at the
# foot); the per-class <stem>.gen.c are generated units (#included into each .c).
add_library(yetty_yrich STATIC
    element.c
    document.c
    ydoc.c
    spreadsheet.c
    slides.c
    rpc.gen.c
    yrich-selection.c
    yrich-operation.c
    yrich-command.c
    yrich-yaml.c
)

target_include_directories(yetty_yrich PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yrich PUBLIC
    yetty_ycore
    yetty_yclass
    yetty_ydraw_core
    yetty_ysdf
    yaml
)

# C23 for [[clang::annotate(...)]] attribute syntax.
if(NOT MSVC)
    set_target_properties(yetty_yrich PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()

# ---------------------------------------------------------------------------
# yetty_yrich_app — the standalone window host + editor shells. Linux +
# WebGPU only (it brings up a GLFW window + GPU device), mirroring the
# other standalone ygui apps (ycompositor-ygui / ygreeter / the demos).
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND YETTY_ENABLE_LIB_WEBGPU)
    find_package(Threads REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(YRICH_FONTCONFIG REQUIRED fontconfig)

    # yinit / yframework / platform bring-up — no static lib exists for
    # this layer, so each standalone-window binary pulls the sources in.
    set(YRICH_APP_PLATFORM_SOURCES
        ${YETTY_ROOT}/src/yetty/yinit/glfw.c
        ${YETTY_ROOT}/src/yetty/yframework/yframework.c
        ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/clipboard/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/window/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/pipe/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
        ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
        ${YETTY_ROOT}/src/yetty/ypty/forkpty.c
        ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
        ${YETTY_ROOT}/src/yetty/yconfig/config.c
        ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
        ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
    )

    add_library(yetty_yrich_app OBJECT
        yrich-shell.c
        yrich-app.c
        ${YRICH_APP_PLATFORM_SOURCES}
    )

    target_include_directories(yetty_yrich_app PUBLIC
        ${YETTY_ROOT}/src
        ${YETTY_ROOT}/include
        ${YRICH_FONTCONFIG_INCLUDE_DIRS}
    )

    target_compile_definitions(yetty_yrich_app PUBLIC
        YETTY_WEB=0
        YETTY_ANDROID=0
        YETTY_USE_FORKPTY=1
    )

    # The full link set the standalone yframework host needs (same shape
    # ycompositor-ygui / the demo runner use). Exported as a cache var so
    # the thin tools (in tools/) can link the exact same set wrapped in a
    # single --start-group/--end-group — the inlined platform objects and
    # these archives cross-reference, so the whole set must sit inside one
    # group for GNU ld to resolve. glfw3webgpu/glfw also carry the GLFW
    # include dirs the inlined yinit/yplatform sources compile against;
    # the ymsdf / yctl / vnc libs arrive through ${YETTY_LIBS}.
    #
    # uv_a is listed explicitly (not only via ${YETTY_LIBS}) because this
    # file is processed by add_subdirectory(src/yetty) BEFORE shared.cmake
    # populates ${YETTY_LIBS} — so the variable expands empty here. The
    # inlined libuv-event-loop/default.c needs uv.h at compile time, which
    # only uv_a's INTERFACE_INCLUDE_DIRECTORIES provides; the consuming
    # tools (yslide/ydoc) re-add the now-populated ${YETTY_LIBS} at link
    # time, but the OBJECT compile happens here and needs the include dir.
    set(YRICH_APP_LINK_LIBS
        yetty_yrich
        yetty_ygui
        yetty_ygrid
        yetty_yfigure
        yetty_ymgui
        yetty_yface
        yetty_yframework
        yetty_yshadertoy
        yetty_yrdawn
        yetty_yrender_core
        yetty_yfont_core
        yetty_ydraw_factory
        yetty_yimage
        yetty_yplot
        yetty_ydraw_core
        glfw3webgpu
        glfw
        uv_a
        ${YETTY_LIBS}
        yetty_ywire
        yetty_yplatform_core
        yetty_yplatform_move_resize
        yetty_yplatform_window_manager
        yetty_ychrome
        yetty_yco
        yetty_telnet
        ${BROTLIDEC_LIBRARIES}
        Threads::Threads
        ${YRICH_FONTCONFIG_LIBRARIES}
        rt
        util
        CACHE INTERNAL "link set for the yrich standalone editor tools")

    # The OBJECT library only needs these for its own compile-time usage
    # requirements (include dirs from glfw / webgpu / ygui headers).
    target_link_libraries(yetty_yrich_app PUBLIC ${YRICH_APP_LINK_LIBS})

    if(NOT MSVC)
        target_compile_options(yetty_yrich_app PRIVATE -Wall -Wextra)
    endif()
endif()
