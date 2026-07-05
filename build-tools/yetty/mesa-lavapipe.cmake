# yetty_find_lavapipe_runtime_files(<out_var>)
#
# Windows-only. Resolves the three files that give yetty a software Vulkan
# device on machines with no (usable) GPU driver:
#
#   vulkan_lvp.dll        Mesa lavapipe — LLVM-JIT software Vulkan ICD
#   lvp_icd.x86_64.json   its ICD manifest (registered at runtime by
#                         yframework via VK_ADD_DRIVER_FILES)
#   vulkan-1.dll          the Khronos Vulkan loader (Windows ships none)
#
# Why: Windows has no built-in Vulkan driver, and its only built-in software
# rasterizer is D3D12's WARP, which is much slower for yetty's workload than
# lavapipe (see yframework.c's adapter recovery chain). Linux gets lavapipe
# for free from the distro's Mesa; on Windows we bundle it, exactly like the
# DirectX shader-compiler DLLs in dawn-runtime-dlls.cmake.
#
# Both archives are fetched at configure time and pinned:
#   mesa-dist-win (MIT)       https://github.com/pal1000/mesa-dist-win
#   LunarG runtime (Apache-2) https://vulkan.lunarg.com
#
# Returns the absolute paths of the files actually found (missing ones are
# warned about and skipped, mirroring yetty_find_dawn_runtime_dlls). Empty
# list off Windows.

include(FetchContent)

set(YETTY_LAVAPIPE_MESA_VERSION "26.1.3" CACHE STRING
    "mesa-dist-win release providing the bundled lavapipe ICD")
set(YETTY_VULKAN_RUNTIME_VERSION "1.4.313.0" CACHE STRING
    "LunarG Vulkan runtime release providing the bundled vulkan-1.dll loader")

function(yetty_find_lavapipe_runtime_files _out_var)
    set(${_out_var} "" PARENT_SCOPE)
    if(NOT WIN32)
        return()
    endif()

    FetchContent_Declare(mesa_lavapipe
        URL "https://github.com/pal1000/mesa-dist-win/releases/download/${YETTY_LAVAPIPE_MESA_VERSION}/mesa3d-${YETTY_LAVAPIPE_MESA_VERSION}-release-msvc.7z"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_Declare(vulkan_runtime
        URL "https://sdk.lunarg.com/sdk/download/${YETTY_VULKAN_RUNTIME_VERSION}/windows/vulkan-runtime-components.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(mesa_lavapipe vulkan_runtime)

    # The LunarG zip's top-level dir is stripped by FetchContent, but its
    # exact layout has varied across releases — locate the x64 loader.
    set(_loader "${vulkan_runtime_SOURCE_DIR}/x64/vulkan-1.dll")
    if(NOT EXISTS "${_loader}")
        file(GLOB_RECURSE _loader_candidates "${vulkan_runtime_SOURCE_DIR}/*x64*/vulkan-1.dll")
        if(_loader_candidates)
            list(GET _loader_candidates 0 _loader)
        endif()
    endif()

    set(_files "")
    foreach(_file
        "${mesa_lavapipe_SOURCE_DIR}/x64/vulkan_lvp.dll"
        "${mesa_lavapipe_SOURCE_DIR}/x64/lvp_icd.x86_64.json"
        "${_loader}")
        if(_file AND EXISTS "${_file}")
            list(APPEND _files "${_file}")
        else()
            message(WARNING "yetty_find_lavapipe_runtime_files: ${_file} not found; "
                "the software-Vulkan (lavapipe) fallback will be unavailable at runtime")
        endif()
    endforeach()

    set(${_out_var} "${_files}" PARENT_SCOPE)
endfunction()

# yetty_copy_lavapipe_runtime_files(<target>)
#
# Copies the lavapipe ICD + Vulkan loader next to <target>'s executable as a
# POST_BUILD step (Windows searches the exe's directory first for DLLs, and
# yframework points VK_ADD_DRIVER_FILES at the manifest sitting there).
function(yetty_copy_lavapipe_runtime_files _target)
    if(NOT WIN32)
        return()
    endif()

    yetty_find_lavapipe_runtime_files(_lvp_files)
    foreach(_found ${_lvp_files})
        get_filename_component(_name "${_found}" NAME)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_found}" "$<TARGET_FILE_DIR:${_target}>/${_name}"
            COMMENT "Copying ${_name} next to ${_target}")
    endforeach()
endfunction()
