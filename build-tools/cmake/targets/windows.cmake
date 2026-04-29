# Windows desktop build target

# RISC-V emulator integrations on Windows. Both enabled.
# - qemu:    uses the locally-built binary from build-windows-minimal/
#            (see 3rdparty-fetch.cmake / qemu/_build.sh windows path).
# - tinyemu: ported to Win32. SLIRP user-mode networking is left out for now
#            (16 files of POSIX socket code) — see tinyemu.cmake's WIN32 branch.
set(YETTY_ENABLE_LIB_TINYEMU ON CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_QEMU    ON CACHE BOOL "" FORCE)

# yetty-ymsdf-gen is an offline build-time tool for generating MSDF font
# atlases. yetty.exe consumes the pre-generated atlas via incbin and does
# not need msdfgen at runtime. Disable on Windows: the prebuilt msdfgen
# windows-x86_64 tarball ships /MT static-CRT, while the rest of yetty's
# 3rdparty Windows libs (freetype, zlib, png, lz4, ...) are /MD. Mixing
# triggers `LNK2038 mismatch detected for 'RuntimeLibrary'`. Cheapest fix
# is to skip the only consumer of msdfgen on Windows.
set(YETTY_ENABLE_FEATURE_YMSDF_GEN OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_MSDFGEN       OFF CACHE BOOL "" FORCE)

include(${YETTY_ROOT}/build-tools/cmake/targets/shared.cmake)

# Windows-specific libraries (guarded by variables.cmake)
if(YETTY_ENABLE_LIB_GLFW)
    include(${YETTY_ROOT}/build-tools/cmake/libs/glfw.cmake)
endif()

# tinyemu static library (RISC-V emulator) for --temu.
if(YETTY_ENABLE_LIB_TINYEMU)
    include(${YETTY_ROOT}/build-tools/cmake/tinyemu.cmake)
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
    ${YETTY_ROOT}/src/yetty/yplatform/shared/glfw-main.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/glfw-surface.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/glfw-event-loop.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/glfw-window.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/glfw-clipboard-manager.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/libuv-event-loop.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/ywebgpu.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/ycoroutine.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/conpty.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/pipe.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/socket.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/process.c
    ${YETTY_ROOT}/src/yetty/yplatform/shared/extract-assets.c
    ${YETTY_ROOT}/src/yetty/incbin-assets.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/platform-paths.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/thread.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/term.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/fs.c
    ${YETTY_ROOT}/src/yetty/yplatform/windows/time.c
)

# tinyemu PTY bridge (yetty <-> tinyemu lib) — only when LIB_TINYEMU is on.
# We use the Windows-specific port at yplatform/windows/tinyemu-pty.c (it
# differs from the POSIX one in the VM-thread main loop: select() doesn't
# work on Win32 CRT pipe fds, so the Windows version uses Sleep() for the
# timer pump and PeekNamedPipe for non-blocking input). Both still need the
# tinyemu win32-compat shim force-included.
if(YETTY_ENABLE_LIB_TINYEMU)
    set(_TINYEMU_PTY_SRC ${YETTY_ROOT}/src/yetty/yplatform/windows/tinyemu-pty.c)
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

# Embed all assets (logo, shaders, fonts, CDB files)
yetty_embed_assets(yetty)

# Dummy targets for dependency tracking (legacy)
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

# shared/ycoroutine.c uses <stdatomic.h>. MSVC sets __STDC_NO_ATOMICS__ by
# default and only clears it when both /std:clatest (or /std:c11) and
# /experimental:c11atomics are passed.
target_compile_options(yetty PRIVATE
    $<$<COMPILE_LANGUAGE:C>:/std:clatest>
    $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)

target_link_libraries(yetty PRIVATE
    ${YETTY_LIBS}
    yetty_yco
    yetty_telnet
    $<$<BOOL:${YETTY_ENABLE_LIB_QEMU}>:yetty_qemu>
    $<$<BOOL:${YETTY_ENABLE_LIB_TINYEMU}>:tinyemu>
    dwrite
    ws2_32
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
        COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DTARGET_TYPE=desktop -P ${YETTY_ROOT}/build-tools/cmake/verify-assets.cmake
        COMMENT "Verifying build assets..."
    )
endif()

# Tests
if(YETTY_ENABLE_FEATURE_TESTS)
    enable_testing()
    add_subdirectory(${YETTY_ROOT}/test/ut/windows ${CMAKE_BINARY_DIR}/test/ut/windows)
endif()
