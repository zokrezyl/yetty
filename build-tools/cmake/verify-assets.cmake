# Verify build assets are present
# Usage: cmake -P verify-assets.cmake -DBUILD_DIR=<path> -DTARGET_TYPE=<webasm|desktop|android|ios>

if(NOT BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR not set")
endif()

if(NOT TARGET_TYPE)
    message(FATAL_ERROR "TARGET_TYPE not set (webasm, desktop, android, or ios)")
endif()

set(MISSING_FILES "")
set(CHECKED_COUNT 0)

function(check_file FILE_PATH DESCRIPTION)
    math(EXPR CHECKED_COUNT "${CHECKED_COUNT} + 1" OUTPUT_FORMAT DECIMAL)
    set(CHECKED_COUNT ${CHECKED_COUNT} PARENT_SCOPE)
    if(NOT EXISTS "${BUILD_DIR}/${FILE_PATH}")
        list(APPEND MISSING_FILES "${FILE_PATH} (${DESCRIPTION})")
        set(MISSING_FILES ${MISSING_FILES} PARENT_SCOPE)
    endif()
endfunction()

# check_file_or_br: succeeds if either ${PATH_NO_BR} or ${PATH_NO_BR}.br
# exists. Used for assets that may arrive either as raw files (from the
# local-gen path in prepare-assets.cmake or the runtime-decompressed
# yemu files) or as .br-only (from the prebuilt-fetch path —
# yetty_3rdparty_fetch(cdb) in 3rdparty-fetch.cmake leaves cdbs compressed for
# direct incbin embedding).
function(check_file_or_br PATH_NO_BR DESCRIPTION)
    math(EXPR CHECKED_COUNT "${CHECKED_COUNT} + 1" OUTPUT_FORMAT DECIMAL)
    set(CHECKED_COUNT ${CHECKED_COUNT} PARENT_SCOPE)
    if(NOT EXISTS "${BUILD_DIR}/${PATH_NO_BR}" AND NOT EXISTS "${BUILD_DIR}/${PATH_NO_BR}.br")
        list(APPEND MISSING_FILES "${PATH_NO_BR}{,.br} (${DESCRIPTION})")
        set(MISSING_FILES ${MISSING_FILES} PARENT_SCOPE)
    endif()
endfunction()

function(check_dir DIR_PATH DESCRIPTION)
    math(EXPR CHECKED_COUNT "${CHECKED_COUNT} + 1" OUTPUT_FORMAT DECIMAL)
    set(CHECKED_COUNT ${CHECKED_COUNT} PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${BUILD_DIR}/${DIR_PATH}")
        list(APPEND MISSING_FILES "${DIR_PATH}/ (${DESCRIPTION})")
        set(MISSING_FILES ${MISSING_FILES} PARENT_SCOPE)
    endif()
endfunction()

message(STATUS "=== Verifying build assets for ${TARGET_TYPE} ===")
message(STATUS "Build directory: ${BUILD_DIR}")

#-----------------------------------------------------------------------------
# Common assets (all platforms)
# iOS uses ios-assets/ prefix, others use assets/
#-----------------------------------------------------------------------------
message(STATUS "Checking common assets...")

if(TARGET_TYPE STREQUAL "ios")
    set(ASSETS_PREFIX "ios-assets")
else()
    set(ASSETS_PREFIX "assets")
endif()

# Font files
check_file("${ASSETS_PREFIX}/fonts/DejaVuSansMNerdFontMono-Regular.ttf" "Regular font")
check_file("${ASSETS_PREFIX}/fonts/DejaVuSansMNerdFontMono-Bold.ttf" "Bold font")
check_file("${ASSETS_PREFIX}/fonts/DejaVuSansMNerdFontMono-Oblique.ttf" "Oblique font")
check_file("${ASSETS_PREFIX}/fonts/DejaVuSansMNerdFontMono-BoldOblique.ttf" "Bold Oblique font")

# Font CDB files (only if CDB generation is enabled)
if(CHECK_CDB)
    check_dir("${ASSETS_PREFIX}/msdf-fonts" "Font CDB directory")
    check_file_or_br("${ASSETS_PREFIX}/msdf-fonts/DejaVuSansMNerdFontMono-Regular.cdb" "Regular font CDB")
    check_file_or_br("${ASSETS_PREFIX}/msdf-fonts/DejaVuSansMNerdFontMono-Bold.cdb" "Bold font CDB")
    check_file_or_br("${ASSETS_PREFIX}/msdf-fonts/DejaVuSansMNerdFontMono-Oblique.cdb" "Oblique font CDB")
    check_file_or_br("${ASSETS_PREFIX}/msdf-fonts/DejaVuSansMNerdFontMono-BoldOblique.cdb" "Bold Oblique font CDB")
endif()

# Shader directory structure
check_dir("${ASSETS_PREFIX}/shaders" "Shaders directory")
check_dir("${ASSETS_PREFIX}/shaders/lib" "Shader lib directory")
check_dir("${ASSETS_PREFIX}/shaders/cards" "Shader cards directory")
check_dir("${ASSETS_PREFIX}/shaders/effects" "Shader effects directory")
check_dir("${ASSETS_PREFIX}/shaders/pre-effects" "Shader pre-effects directory")
check_dir("${ASSETS_PREFIX}/shaders/post-effects" "Shader post-effects directory")

# Core shaders
check_file("${ASSETS_PREFIX}/shaders/gpu-screen.wgsl" "Main GPU screen shader")
check_file("${ASSETS_PREFIX}/shaders/cursor.wgsl" "Cursor shader")
check_file("${ASSETS_PREFIX}/shaders/msdf_gen.wgsl" "MSDF generation shader")

# Card shaders
check_file("${ASSETS_PREFIX}/shaders/cards/0x0000-texture.wgsl" "Texture card shader")
check_file("${ASSETS_PREFIX}/shaders/cards/0x0001-plot.wgsl" "Plot card shader")
check_file("${ASSETS_PREFIX}/shaders/cards/0x0003-ydraw.wgsl" "Ydraw card shader")
check_file("${ASSETS_PREFIX}/shaders/cards/0x0004-ypaint.wgsl" "Kdraw card shader")
check_file("${ASSETS_PREFIX}/shaders/cards/0x0006-ytext.wgsl" "Ytext card shader")

#-----------------------------------------------------------------------------
# WebAssembly specific
#-----------------------------------------------------------------------------
if(TARGET_TYPE STREQUAL "webasm")
    message(STATUS "Checking WebAssembly assets...")

    # WASM output files
    check_file("yetty.js" "Yetty JS loader")
    check_file("yetty.wasm" "Yetty WASM binary")
    check_file("yetty.data" "Yetty preloaded data")

    # Web files
    check_file("index.html" "Web index page")
    check_file("serve.py" "Development server")

    # JSLinux files
    check_dir("jslinux" "JSLinux directory")
    check_file("jslinux/vm-bridge.html" "JSLinux VM bridge")
    check_file("jslinux/term-bridge.js" "JSLinux terminal bridge")

    # Note: vm-tools and vfsync are checked by Makefile verify-webasm target (built after cmake)

    # Note: demo directory is preloaded from source into yetty.data, not copied to build dir
endif()

#-----------------------------------------------------------------------------
# Desktop specific
#-----------------------------------------------------------------------------
if(TARGET_TYPE STREQUAL "desktop")
    message(STATUS "Checking Desktop assets...")

    # Executable - Ninja puts it at root, MSVC generator puts it in Release/
    if(WIN32)
        if(EXISTS "${BUILD_DIR}/yetty.exe")
            check_file("yetty.exe" "Yetty executable")
        else()
            check_file("Release/yetty.exe" "Yetty executable")
        endif()
    else()
        check_file("yetty" "Yetty executable")
    endif()
endif()


#-----------------------------------------------------------------------------
# iOS specific
#-----------------------------------------------------------------------------
if(TARGET_TYPE STREQUAL "ios")
    message(STATUS "Checking iOS assets...")

    # App bundle
    check_dir("yetty.app" "Yetty app bundle")

    # iOS assets directory
    check_dir("ios-assets" "iOS assets directory")
    check_dir("ios-assets/shaders" "iOS shaders directory")
    check_dir("ios-assets/msdf-fonts" "iOS font CDB directory")
endif()

#-----------------------------------------------------------------------------
# Report results
#-----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== Asset Verification Results ===")
message(STATUS "Checked: ${CHECKED_COUNT} items")

list(LENGTH MISSING_FILES MISSING_COUNT)
if(MISSING_COUNT GREATER 0)
    message(STATUS "Missing: ${MISSING_COUNT} items")
    message(STATUS "")
    message(FATAL_ERROR "Missing files:\n  ${MISSING_FILES}")
else()
    message(STATUS "Missing: 0 items")
    message(STATUS "")
    message(STATUS "All assets verified successfully!")
endif()
