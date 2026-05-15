# yetty sources and subdirectories

#-----------------------------------------------------------------------------
# Feature subdirectories — guarded by YETTY_ENABLE_FEATURE_*
#-----------------------------------------------------------------------------

if(YETTY_ENABLE_FEATURE_YGUI)
    add_subdirectory(ygui)
endif()

if(YETTY_ENABLE_FEATURE_YMGUI)
    add_subdirectory(ymgui)
endif()

if(YETTY_ENABLE_FEATURE_YDRAW_ZOO)
    add_subdirectory(ydraw-zoo)
endif()

if(YETTY_ENABLE_FEATURE_YDRAW_MAZE)
    add_subdirectory(ydraw-maze)
endif()

if(YETTY_ENABLE_FEATURE_YGRID)
    add_subdirectory(cards/ygrid)
endif()

if(YETTY_ENABLE_FEATURE_MSDF_GEN)
    add_subdirectory(msdf-gen)
endif()

if(YETTY_ENABLE_FEATURE_YRPC)
    add_subdirectory(yrpc)
endif()

# Yetty sources (src/yetty/*.c)
set(YETTY_SOURCES
    ${YETTY_ROOT}/src/yetty/yetty/yetty.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/ynotify/ynotify.c
)


# Render subsystem
add_subdirectory(yrender)

# WebGPU utilities
add_subdirectory(ywebgpu)

# Top-level event dispatch wiring
add_subdirectory(yevent)

# Video codec facade (H.264 encode/decode via openh264). Must be declared
# before yvnc so vnc-server can link against it.
if(YETTY_ENABLE_LIB_OPENH264)
    add_subdirectory(yvideo)
endif()

# Coroutine wrappers (libco on desktop, emscripten_fiber_t on web)
if(NOT EMSCRIPTEN)
    add_subdirectory(yco)
endif()

# Abstract view interface — shared base for yui and yterm (breaks the
# yui ↔ yterm cycle by giving both a common dependency that depends on
# neither).
add_subdirectory(yui-core)

# UI subsystem (workspace, tile, view)
add_subdirectory(yui)

# Core sources (src/yetty/core/*.cpp)
set(YETTY_YCORE_SOURCES "")

# Wire-format state machine (OSC framer + decode stack)
add_subdirectory(ywire)

# Term subsystem
add_subdirectory(yterm)

set(YETTY_SOURCES ${YETTY_SOURCES} PARENT_SCOPE)
set(YETTY_YCORE_SOURCES ${YETTY_YCORE_SOURCES} PARENT_SCOPE)

# Desktop-only sources (Linux, macOS - NOT Windows)
set(YETTY_DESKTOP_SOURCES "" PARENT_SCOPE)

# Windows-specific sources
set(YETTY_WINDOWS_SOURCES "" PARENT_SCOPE)

# WebAssembly-specific sources
set(YETTY_WEB_SOURCES "" PARENT_SCOPE)

# Android-specific sources
set(YETTY_ANDROID_SOURCES "" PARENT_SCOPE)

# iPhone/iOS-specific sources
set(YETTY_IOS_SOURCES "" PARENT_SCOPE)

# Include directories for renderer sources
set(YETTY_YRENDERER_INCLUDES "" PARENT_SCOPE)

#-----------------------------------------------------------------------------
# Library subdirectories — guarded by YETTY_ENABLE_FEATURE_*
#-----------------------------------------------------------------------------

if(YETTY_ENABLE_FEATURE_YCORE)
    add_subdirectory(ycore)
endif()

# yplatform — split into two libs (core + full). Must come after ycore
# (PUBLIC dep) and before any tool / consumer of its target names.
add_subdirectory(yplatform)

if(YETTY_ENABLE_FEATURE_YFACE)
    add_subdirectory(yface)
endif()

if(YETTY_ENABLE_FEATURE_YCDB)
    add_subdirectory(ycdb)
endif()

if(YETTY_ENABLE_FEATURE_YFONT)
    add_subdirectory(yfont)
endif()

if(YETTY_ENABLE_FEATURE_YECHO)
    add_subdirectory(yecho)
endif()

if(YETTY_ENABLE_FEATURE_YCAT)
    if(NOT YETTY_IOS)
        add_subdirectory(ycat)
    endif()
endif()

if(YETTY_ENABLE_FEATURE_YRICH)
    add_subdirectory(yrich)
endif()

if(YETTY_ENABLE_FEATURE_YDRAW)
    add_subdirectory(ydraw-core)
    add_subdirectory(ydraw-factory)
    add_subdirectory(ydraw-yaml)
    add_subdirectory(ysdf)
    add_subdirectory(ydraw)
endif()

if(YETTY_ENABLE_FEATURE_YNETSURF)
    add_subdirectory(ynetsurf)
endif()

if(YETTY_ENABLE_FEATURE_YLEXBOR)
    add_subdirectory(ylexbor)
endif()

if(YETTY_ENABLE_FEATURE_YBROWSER)
    add_subdirectory(ybrowser)
endif()

if(YETTY_ENABLE_FEATURE_YDIAGRAM)
    add_subdirectory(ydiagram)
endif()

if(YETTY_ENABLE_FEATURE_YGRID)
    add_subdirectory(ygrid)
endif()

if(YETTY_ENABLE_FEATURE_YTHORVG)
    if(YETTY_ENABLE_LIB_THORVG)
        add_subdirectory(ythorvg)
    endif()
endif()

if(YETTY_ENABLE_FEATURE_YVIDEO)
    add_subdirectory(yvideo)
endif()

if(YETTY_ENABLE_FEATURE_CARDS)
    add_subdirectory(cards)
endif()

if(YETTY_ENABLE_FEATURE_YEXPR)
    add_subdirectory(yexpr)
endif()

if(YETTY_ENABLE_FEATURE_YFSVM)
    add_subdirectory(yfsvm)
endif()

if(YETTY_ENABLE_FEATURE_YPLOT)
    add_subdirectory(yplot)
endif()

if(YETTY_ENABLE_FEATURE_YPDF)
    add_subdirectory(ypdf)
endif()

if(YETTY_ENABLE_FEATURE_YMARKDOWN)
    add_subdirectory(ymarkdown)
endif()

if(YETTY_ENABLE_FEATURE_YSVG)
    add_subdirectory(ysvg)
endif()

if(YETTY_ENABLE_FEATURE_YIMAGE)
    add_subdirectory(yimage)
endif()

if(YETTY_ENABLE_FEATURE_YMESH)
    add_subdirectory(ymesh)
endif()

if(YETTY_ENABLE_FEATURE_YMAZE)
    add_subdirectory(ymaze)
endif()

if(YETTY_ENABLE_FEATURE_YZOO)
    add_subdirectory(yzoo)
endif()

if(YETTY_ENABLE_FEATURE_YMSDF_GEN)
    add_subdirectory(ymsdf-gen)
endif()

if(YETTY_ENABLE_FEATURE_YMSDF_WGSL)
    add_subdirectory(ymsdf-wgsl)
endif()

# Polymorphic MSDF generator wrapper (cpu | gpu) — built whenever the GPU
# impl is on, since yetty.c always invokes the factory. The cpu backend is
# only compiled in when YMSDF_GEN is also on; otherwise factory.c rejects
# `msdf/generator=cpu` at runtime (Windows path: msdfgen ships /MT, rest of
# the 3rdparty stack is /MD — see windows.cmake).
if(YETTY_ENABLE_FEATURE_YMSDF_WGSL)
    add_subdirectory(ymsdf)
endif()

if(YETTY_ENABLE_FEATURE_YSHADERS)
    add_subdirectory(yshaders)
endif()

if(YETTY_ENABLE_FEATURE_YVNC)
    add_subdirectory(yvnc)
endif()

if(YETTY_ENABLE_FEATURE_YDVNC)
    add_subdirectory(ydvnc)
endif()

if(YETTY_ENABLE_FEATURE_TELNET)
    add_subdirectory(ytelnet)
endif()

if(YETTY_ENABLE_LIB_QEMU)
    add_subdirectory(yqemu)
endif()

if(YETTY_ENABLE_FEATURE_SSH)
    add_subdirectory(yssh)
endif()
