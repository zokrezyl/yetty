# Windows desktop build target
include(${CMAKE_CURRENT_LIST_DIR}/variables.cmake)

# YETTY_ERR(...) uses __VA_ARGS__ count-based dispatch; MSVC's traditional
# preprocessor mishandles it. The conformant preprocessor (/Zc:preprocessor)
# ships in VS 2019 16.5+ and is a no-op rewrite for well-behaved code.
#
# /std:clatest + /experimental:c11atomics — several of our C TUs include
# <stdatomic.h> (ywire's coroutine impl, yui, …). MSVC sets
# __STDC_NO_ATOMICS__ by default and only clears it when BOTH flags are
# present. Applied globally at directory scope so every static lib added
# under shared.cmake picks them up — avoids a per-target C_STANDARD 11
# stanza in every CMakeLists that pulls stdatomic in transitively.
if(MSVC)
    add_compile_options(/Zc:preprocessor)
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C>:/std:clatest>
        $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
endif()

# Embedded asset bytes ride into the binary as RCDATA resources on Windows
# (see incbin.cmake's MSVC branch). enable_language(RC) lets CMake/Ninja
# pick up the generated .rc file as a regular source and run rc.exe.
enable_language(RC)

include(${YETTY_ROOT}/build-tools/yetty/platform/shared.cmake)

# Windows-specific libraries (guarded by variables.cmake)
if(YETTY_ENABLE_LIB_GLFW)
    include(${YETTY_ROOT}/build-tools/yetty/libs/glfw.cmake)
endif()

# tinyemu static library (RISC-V emulator) for --temu.
if(YETTY_ENABLE_LIB_TINYEMU)
    include(${YETTY_ROOT}/build-tools/yetty/tinyemu.cmake)
endif()

# Desktop-specific subdirectories
if(YETTY_ENABLE_FEATURE_GPU)
    add_subdirectory(${YETTY_ROOT}/src/yetty/gpu ${CMAKE_BINARY_DIR}/src/yetty/gpu)
endif()
if(YETTY_ENABLE_FEATURE_CLIENT)
    add_subdirectory(${YETTY_ROOT}/src/yetty/client ${CMAKE_BINARY_DIR}/src/yetty/client)
endif()

# Platform sources — Windows-specific + shared GLFW (C)
# Windows uses GLFW for window/surface but ConPTY for terminal and Windows pipes
set(YETTY_PLATFORM_SOURCES
    ${YETTY_ROOT}/src/yetty/yinit/glfw.c
    ${YETTY_ROOT}/src/yetty/ymain/glfw.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu-surface/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/os-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/window/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/clipboard/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/libuv-event-loop/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/webgpu/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/coroutine/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/yworkpool/default.c
    ${YETTY_ROOT}/src/yetty/ypty/conpty.c
    ${YETTY_ROOT}/src/yetty/ypty/memory-pty.c
    ${YETTY_ROOT}/src/yetty/yplatform/pipe/windows.c
    ${YETTY_ROOT}/src/yetty/yplatform/extract-assets/default.c
    ${YETTY_ROOT}/src/yetty/yncbin/incbin-assets.c
)

# tinyemu PTY bridge (yetty <-> tinyemu lib) — only when LIB_TINYEMU is on.
# We use the Windows-specific port at ypty/windows-temu-pty.c (it differs
# from the POSIX one in the VM-thread main loop: select() doesn't work on
# Win32 CRT pipe fds, so the Windows version uses Sleep() for the timer
# pump and PeekNamedPipe for non-blocking input). Both still need the
# tinyemu win32-compat shim force-included.
if(YETTY_ENABLE_LIB_TINYEMU)
    set(_TINYEMU_PTY_SRC ${YETTY_ROOT}/src/yetty/ypty/windows-temu-pty.c)
    list(APPEND YETTY_PLATFORM_SOURCES ${_TINYEMU_PTY_SRC})
    if(MSVC)
        set_source_files_properties(${_TINYEMU_PTY_SRC} PROPERTIES
            COMPILE_OPTIONS "/FI${YETTY_ROOT}/src/tinyemu/win32-compat.h"
            INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/tinyemu-win32-stubs")
    endif()
endif()

# Create executable with core sources + platform
add_executable(yetty
    ${YETTY_SOURCES}
    ${YETTY_CORE_SOURCES}
    ${YETTY_DESKTOP_SOURCES}
    ${YETTY_PLATFORM_SOURCES}
)

target_include_directories(yetty PRIVATE ${YETTY_INCLUDES} ${YETTY_RENDERER_INCLUDES} ${JPEG_INCLUDE_DIRS} ${BROTLI_INCLUDE_DIR})

# Embed all assets only for a standalone build. By default the desktop yetty
# is thin — the yinstall installer carries the assets (and yetty itself).
# The build version is stamped either way.
if(YETTY_EMBED_ASSETS_IN_BINARIES)
    yetty_embed_assets(yetty)
else()
    yetty_compute_build_version()
    target_compile_definitions(yetty PRIVATE YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}")
endif()

# Dummy platforms for dependency tracking (legacy)
add_custom_target(copy-shaders-for-incbin)
add_custom_target(copy-fonts-for-incbin)

target_compile_definitions(yetty PRIVATE
    ${YETTY_DEFINITIONS}
    YETTY_WEB=0
    YETTY_ANDROID=0
    YETTY_USE_PREBUILT_ATLAS=0
    YETTY_USE_DIRECTWRITE=1
    YETTY_USE_CONPTY=1
    YETTY_HAS_VNC=1
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

set_target_properties(yetty PROPERTIES ENABLE_EXPORTS TRUE)

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    yetty_yco
    yetty_telnet
    $<$<BOOL:${YETTY_ENABLE_LIB_QEMU}>:yetty_qemu>
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:tinyemu>
    dwrite
    ws2_32
    yetty_yplatform_core
    yetty_yplatform_move_resize
    yetty_yplatform_window_manager
)

# Copy runtime assets to build directory
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_subdirectory(${YETTY_ROOT}/assets ${CMAKE_BINARY_DIR}/assets-build)
endif()

# Ensure all runtime assets are in build output before yetty
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_dependencies(yetty copy-shaders copy-assets copy-shaders-for-incbin copy-fonts-for-incbin)
endif()

# Copy DirectX runtime DLLs needed by Dawn (shader compiler + DXIL signing)
if(WIN32)
    # Gather Windows SDK search dirs once, sorted descending (latest first)
    file(GLOB _sdk_bin_x64_dirs  "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
    file(GLOB _redist_x64_dirs   "C:/Program Files (x86)/Windows Kits/10/Redist/*/x64")
    if(_sdk_bin_x64_dirs)
        list(SORT _sdk_bin_x64_dirs ORDER DESCENDING)
    endif()
    if(_redist_x64_dirs)
        list(SORT _redist_x64_dirs ORDER DESCENDING)
    endif()

    # --- Find d3dcompiler_47.dll ---
    set(_d3dcompiler_dll "")
    if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/d3dcompiler_47.dll")
        set(_d3dcompiler_dll "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/d3dcompiler_47.dll")
    else()
        foreach(_dir ${_redist_x64_dirs})
            if(EXISTS "${_dir}/d3dcompiler_47.dll")
                set(_d3dcompiler_dll "${_dir}/d3dcompiler_47.dll")
                break()
            endif()
        endforeach()
    endif()
    if(NOT _d3dcompiler_dll)
        foreach(_dir ${_sdk_bin_x64_dirs})
            if(EXISTS "${_dir}/d3dcompiler_47.dll")
                set(_d3dcompiler_dll "${_dir}/d3dcompiler_47.dll")
                break()
            endif()
        endforeach()
    endif()

    if(_d3dcompiler_dll)
        message(STATUS "Found d3dcompiler_47.dll: ${_d3dcompiler_dll}")
        add_custom_command(TARGET yetty POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_d3dcompiler_dll}"
                "$<TARGET_FILE_DIR:yetty>/d3dcompiler_47.dll"
            COMMENT "Copying d3dcompiler_47.dll"
        )
    else()
        message(WARNING "Could not find d3dcompiler_47.dll in Windows SDK")
    endif()

    # --- Find dxil.dll ---
    set(_dxil_dll "")
    foreach(_dir ${_sdk_bin_x64_dirs})
        if(EXISTS "${_dir}/dxil.dll")
            set(_dxil_dll "${_dir}/dxil.dll")
            break()
        endif()
    endforeach()
    if(NOT _dxil_dll)
        if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/dxil.dll")
            set(_dxil_dll "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/dxil.dll")
        else()
            foreach(_dir ${_redist_x64_dirs})
                if(EXISTS "${_dir}/dxil.dll")
                    set(_dxil_dll "${_dir}/dxil.dll")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(_dxil_dll)
        message(STATUS "Found dxil.dll: ${_dxil_dll}")
        add_custom_command(TARGET yetty POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dxil_dll}"
                "$<TARGET_FILE_DIR:yetty>/dxil.dll"
            COMMENT "Copying dxil.dll"
        )
    else()
        message(WARNING "Could not find dxil.dll in Windows SDK")
    endif()

    # --- Find dxcompiler.dll (needed by Dawn for D3D12 shader compilation) ---
    set(_dxcompiler_dll "")
    if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/dxcompiler.dll")
        set(_dxcompiler_dll "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/dxcompiler.dll")
    else()
        foreach(_dir ${_sdk_bin_x64_dirs})
            if(EXISTS "${_dir}/dxcompiler.dll")
                set(_dxcompiler_dll "${_dir}/dxcompiler.dll")
                break()
            endif()
        endforeach()
    endif()

    if(_dxcompiler_dll)
        message(STATUS "Found dxcompiler.dll: ${_dxcompiler_dll}")
        add_custom_command(TARGET yetty POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dxcompiler_dll}"
                "$<TARGET_FILE_DIR:yetty>/dxcompiler.dll"
            COMMENT "Copying dxcompiler.dll"
        )
    else()
        message(WARNING "Could not find dxcompiler.dll in Windows SDK")
    endif()
endif()

# Verify all required assets are present
if(YETTY_ENABLE_FEATURE_ASSETS)
    add_custom_command(TARGET yetty POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET_TYPE=desktop -P ${YETTY_ROOT}/build-tools/yetty/verify-assets.cmake
        COMMENT "Verifying build assets..."
    )
endif()

# Tests
if(YETTY_ENABLE_FEATURE_TESTS)
    enable_testing()
    add_subdirectory(${YETTY_ROOT}/test/ut/windows ${CMAKE_BINARY_DIR}/test/ut/windows)
endif()
