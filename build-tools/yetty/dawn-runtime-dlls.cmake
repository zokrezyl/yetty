# yetty_find_dawn_runtime_dlls(<out_var>)
#
# Resolves the absolute source paths of the DirectX shader-compiler DLLs that
# Dawn's D3D12 backend loads at runtime (dxcompiler.dll, dxil.dll,
# d3dcompiler_47.dll) and returns them (only the ones actually found) in
# <out_var>. The paths point at the Windows SDK originals, which exist at
# configure time — so callers that need the bytes (e.g. the installer, to
# carry them into its payload) don't depend on any target's POST_BUILD copy.
#
# The single source of truth for the search; yetty_copy_dawn_runtime_dlls
# below is expressed in terms of it. Empty list off Windows.
function(yetty_find_dawn_runtime_dlls _out_var)
    set(${_out_var} "" PARENT_SCOPE)
    if(NOT WIN32)
        return()
    endif()

    file(GLOB _sdk_bin_x64_dirs "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
    file(GLOB _redist_x64_dirs  "C:/Program Files (x86)/Windows Kits/10/Redist/*/x64")
    if(_sdk_bin_x64_dirs)
        list(SORT _sdk_bin_x64_dirs ORDER DESCENDING)
    endif()
    if(_redist_x64_dirs)
        list(SORT _redist_x64_dirs ORDER DESCENDING)
    endif()

    set(_dlls "")
    # Each DLL: a preferred fixed path, then the SDK bin/redist dirs.
    foreach(_dll "dxcompiler.dll" "dxil.dll" "d3dcompiler_47.dll")
        set(_found "")
        # dxil ships under bin; dxcompiler/d3dcompiler also appear under Redist/D3D.
        if(EXISTS "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/${_dll}")
            set(_found "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64/${_dll}")
        endif()
        if(NOT _found)
            foreach(_dir ${_sdk_bin_x64_dirs} ${_redist_x64_dirs})
                if(EXISTS "${_dir}/${_dll}")
                    set(_found "${_dir}/${_dll}")
                    break()
                endif()
            endforeach()
        endif()

        if(_found)
            list(APPEND _dlls "${_found}")
        else()
            message(WARNING "yetty_find_dawn_runtime_dlls: ${_dll} not found in Windows SDK; dependent binaries may fail to open a GPU window")
        endif()
    endforeach()

    set(${_out_var} "${_dlls}" PARENT_SCOPE)
endfunction()

# yetty_copy_dawn_runtime_dlls(<target>)
#
# Copies the DirectX shader-compiler DLLs next to <target>'s executable as a
# POST_BUILD step. Windows searches the exe's own directory first, so every
# GPU-opening binary needs its own copy.
#
# The main yetty target carries an equivalent inline copy in
# platform/windows/cmake.cmake; this function lets standalone GPU tools
# (e.g. ygreeter) provision the same DLLs without an inter-target dependency.
function(yetty_copy_dawn_runtime_dlls _target)
    if(NOT WIN32)
        return()
    endif()

    yetty_find_dawn_runtime_dlls(_dawn_dlls)
    foreach(_found ${_dawn_dlls})
        get_filename_component(_dll "${_found}" NAME)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_found}" "$<TARGET_FILE_DIR:${_target}>/${_dll}"
            COMMENT "Copying ${_dll} next to ${_target}")
    endforeach()
endfunction()
