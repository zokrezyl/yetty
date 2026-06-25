# webasm-stage-assets.cmake — stage runtime assets for the webasm build.
#
# Replaces the incbin embed pipeline on the webasm target. Instead of
# baking 200+ MB of shaders / fonts / msdf CDBs / configs into yetty.wasm
# (which forces a 240 MB yetty_data_data.c through clang and pushes link
# memory past 20 GB), we stage them as standalone files alongside
# yetty.{js,wasm}. A pre-js shim fetches them at startup and writes them
# into MEMFS at the same paths the in-process extract path used to
# produce, so all downstream C code (yetty_yplatform_get_data_dir() →
# /data, etc.) is unchanged.
#
# Layout under ${CMAKE_BINARY_DIR}/yetty-assets/ :
#
#   data/
#     shaders/*.wgsl.br
#     fonts/*.ttf.br
#     msdf-fonts/*.cdb.br        (already brotli'd by upstream — copied)
#   config/
#     config.yaml                 (uncompressed)
#     temu/yetty-temu*.cfg
#   manifest.json
#
# manifest.json schema:
#   { "version": "<build-stamp>",
#     "entries": [ { "url": "data/...", "dest": "/data/...", "brotli": true|false }, ... ] }

if(NOT EMSCRIPTEN)
    return()
endif()

find_program(BROTLI_BIN brotli REQUIRED
    DOC "brotli CLI (provided by nix .#web shell)")

# Output dir served by serve.py and fetched by the runtime preload.
set(YETTY_WEBASM_ASSETS_DIR "${CMAKE_BINARY_DIR}/yetty-assets" CACHE INTERNAL "")

# Track manifest entries as parallel lists; stitched into JSON at the
# end. Using lists keeps the per-file logic linear.
set(_MANIFEST_URLS "")
set(_MANIFEST_DESTS "")
set(_MANIFEST_BROTLI "")

# Helper: stage a single source file.
#   _stage_one(<src> <dest_subdir> <dest_filename> <compress>)
#     <src>           : absolute path of source
#     <dest_subdir>   : subdir under yetty-assets/ (e.g. "data/shaders")
#     <dest_filename> : final on-disk filename under <dest_subdir>;
#                       brotli adds ".br" to this when <compress>=TRUE
#     <compress>      : TRUE → brotli compress into <name>.br, manifest
#                       brotli=true; FALSE → plain copy, brotli=false
#     <runtime_root>  : "/data" or "/config" — prepended to the dest in
#                       the manifest so the JS shim writes to MEMFS at
#                       the expected location.
function(_stage_one SRC DEST_SUBDIR DEST_FILENAME COMPRESS RUNTIME_ROOT)
    set(_OUT_DIR "${YETTY_WEBASM_ASSETS_DIR}/${DEST_SUBDIR}")
    file(MAKE_DIRECTORY "${_OUT_DIR}")

    if(COMPRESS)
        # If the source is already .br (e.g. pre-shipped CDBs), copy
        # straight through — double-brotli silently corrupts the file
        # (the JS preload only does ONE decode pass, so the stored bytes
        # would still be brotli-compressed cdb, and cdb_open would
        # fopen-succeed but read garbage). Use ENDSWITH on the filename;
        # get_filename_component(... EXT) returns the *longest* extension
        # (".cdb.br" for foo.cdb.br), so a STREQUAL ".br" check is wrong.
        get_filename_component(_SRC_NAME "${SRC}" NAME)
        string(LENGTH "${_SRC_NAME}" _SRC_NAME_LEN)
        math(EXPR _SUFFIX_OFF "${_SRC_NAME_LEN} - 3")
        if(_SUFFIX_OFF GREATER 0)
            string(SUBSTRING "${_SRC_NAME}" ${_SUFFIX_OFF} 3 _SRC_SUFFIX)
        else()
            set(_SRC_SUFFIX "")
        endif()
        if(_SRC_SUFFIX STREQUAL ".br")
            configure_file("${SRC}" "${_OUT_DIR}/${DEST_FILENAME}.br" COPYONLY)
        else()
            execute_process(
                COMMAND "${BROTLI_BIN}" -q 11 -f -o "${_OUT_DIR}/${DEST_FILENAME}.br" "${SRC}"
                RESULT_VARIABLE _RES
                OUTPUT_QUIET ERROR_QUIET)
            if(NOT _RES EQUAL 0)
                message(FATAL_ERROR "brotli failed on ${SRC}")
            endif()
        endif()
        set(_URL "${DEST_SUBDIR}/${DEST_FILENAME}.br")
        set(_BR_FLAG "true")
    else()
        configure_file("${SRC}" "${_OUT_DIR}/${DEST_FILENAME}" COPYONLY)
        set(_URL "${DEST_SUBDIR}/${DEST_FILENAME}")
        set(_BR_FLAG "false")
    endif()

    set(_DEST_PATH "${RUNTIME_ROOT}/${DEST_SUBDIR}/${DEST_FILENAME}")
    # Strip the leading "data/" or "config/" from the dest when it's
    # already part of the runtime root (avoids /data/data/shaders/...).
    string(REGEX REPLACE "^/data/data/" "/data/" _DEST_PATH "${_DEST_PATH}")
    string(REGEX REPLACE "^/config/config/" "/config/" _DEST_PATH "${_DEST_PATH}")

    list(APPEND _MANIFEST_URLS "${_URL}")
    list(APPEND _MANIFEST_DESTS "${_DEST_PATH}")
    list(APPEND _MANIFEST_BROTLI "${_BR_FLAG}")
    set(_MANIFEST_URLS  "${_MANIFEST_URLS}"  PARENT_SCOPE)
    set(_MANIFEST_DESTS "${_MANIFEST_DESTS}" PARENT_SCOPE)
    set(_MANIFEST_BROTLI "${_MANIFEST_BROTLI}" PARENT_SCOPE)
endfunction()

# Helper: glob a directory, stage every match.
function(_stage_glob SRC_GLOB DEST_SUBDIR COMPRESS RUNTIME_ROOT)
    file(GLOB _FILES ${SRC_GLOB})
    foreach(_F ${_FILES})
        get_filename_component(_NAME "${_F}" NAME)
        # If we're compressing AND name already ends .br, _stage_one
        # will copy through — but the manifest dest should drop the .br
        # so MEMFS gets the decompressed name. Trim it here.
        set(_DEST_NAME "${_NAME}")
        if(COMPRESS)
            string(REGEX REPLACE "\\.br$" "" _DEST_NAME "${_NAME}")
        endif()
        _stage_one("${_F}" "${DEST_SUBDIR}" "${_DEST_NAME}" "${COMPRESS}" "${RUNTIME_ROOT}")
        set(_MANIFEST_URLS  "${_MANIFEST_URLS}"  PARENT_SCOPE)
        set(_MANIFEST_DESTS "${_MANIFEST_DESTS}" PARENT_SCOPE)
        set(_MANIFEST_BROTLI "${_MANIFEST_BROTLI}" PARENT_SCOPE)
    endforeach()
endfunction()

function(yetty_stage_webasm_assets)
    file(REMOVE_RECURSE "${YETTY_WEBASM_ASSETS_DIR}")
    file(MAKE_DIRECTORY "${YETTY_WEBASM_ASSETS_DIR}/data")
    file(MAKE_DIRECTORY "${YETTY_WEBASM_ASSETS_DIR}/data/shaders")
    file(MAKE_DIRECTORY "${YETTY_WEBASM_ASSETS_DIR}/data/shaders/glyph-shaders")
    file(MAKE_DIRECTORY "${YETTY_WEBASM_ASSETS_DIR}/data/fonts")
    file(MAKE_DIRECTORY "${YETTY_WEBASM_ASSETS_DIR}/data/msdf-fonts")
    file(MAKE_DIRECTORY "${YETTY_WEBASM_ASSETS_DIR}/config")
    file(MAKE_DIRECTORY "${YETTY_WEBASM_ASSETS_DIR}/config/temu")

    set(_MANIFEST_URLS "")
    set(_MANIFEST_DESTS "")
    set(_MANIFEST_BROTLI "")

    # Shaders — same set as yetty_embed_assets (shared.cmake lines 413-446).
    foreach(_SHADER
            "${YETTY_ROOT}/src/yetty/ymgui/ymgui-layer.wgsl"
            "${YETTY_ROOT}/src/yetty/yterminal/background-layer.wgsl"
            "${YETTY_ROOT}/src/yetty/ysdf/ysdf.gen.wgsl"
            "${YETTY_ROOT}/src/yetty/yfont/ms-msdf-font.wgsl"
            "${YETTY_ROOT}/src/yetty/yfont/msdf-font.wgsl"
            "${YETTY_ROOT}/src/yetty/yfont/ms-raster-font.wgsl"
            "${YETTY_ROOT}/src/yetty/yfont/raster-font.wgsl"
            "${YETTY_ROOT}/src/yetty/ymsdf-wgsl/shaders/msdf_gen.wgsl"
            # ygrid.wgsl — the yui chrome (titlebar/tabbar/statusbar) renders
            # through an ygrid figure since the yui→ygui refactor; without its
            # shader staged here the chrome ygrid fails to create on webasm and
            # the whole UI silently fails to render.
            "${YETTY_ROOT}/src/yetty/ygrid/ygrid.wgsl"
            # yvterm-sdf-layer.wgsl — the yvterm "vterm-as-figure" SDF pass that
            # rasterizes ycat inline rich content (markdown/charts/diagrams). The
            # refactor made sdf-layer.c load this from <paths/shaders>; without it
            # staged, sdf_layer_create fails ("load_layer_shader") and ycat rich
            # content is silently disabled on webasm.
            "${YETTY_ROOT}/src/yetty/yvterm/yvterm-sdf-layer.wgsl")
        get_filename_component(_NAME "${_SHADER}" NAME)
        _stage_one("${_SHADER}" "data/shaders" "${_NAME}" TRUE "/data")
    endforeach()
    # blender.wgsl is renamed from blend.wgsl in the incbin path; mirror.
    _stage_one("${YETTY_ROOT}/src/yetty/yrender/blend.wgsl"
               "data/shaders" "blender.wgsl" TRUE "/data")

    # Per-glyph procedurals + shared prelude libs (_*.wgsl).
    _stage_glob("${YETTY_ROOT}/src/yetty/yfont/glyph-shaders/0x*.wgsl"
                "data/shaders/glyph-shaders" TRUE "/data")
    _stage_glob("${YETTY_ROOT}/src/yetty/yfont/glyph-shaders/_*.wgsl"
                "data/shaders/glyph-shaders" TRUE "/data")

    # Fonts.
    _stage_glob("${YETTY_ROOT}/assets/fonts/*.ttf"
                "data/fonts" TRUE "/data")

    # MSDF CDBs (already shipped .br by 3rdparty cdb fetch).
    if(YETTY_3RDPARTY_cdb_DIR)
        _stage_glob("${YETTY_3RDPARTY_cdb_DIR}/*.cdb.br"
                    "data/msdf-fonts" TRUE "/data")
    endif()

    # ygreeter media assets — the showcase's Media (Images + Video), PDF and
    # Markdown tabs read these straight from <data_dir>/ (i.e. /data/...) via
    # discover_logo_images / discover_video_files / discover_pdf /
    # discover_readme. On desktop ygreeter self-extracts them from its own
    # incbin "data/" section; on webasm that path is compiled out, so they
    # must be staged here or the tabs report "no images/videos found".
    # Stage EVERY logo (assets/logo-*.jpeg), not just the four the desktop
    # incbin embeds. JPEG / MP4 / PDF are already compressed — copy them
    # uncompressed (brotli q11 on them only burns build CPU for no win).
    _stage_glob("${YETTY_ROOT}/assets/logo-*.jpeg" "data" FALSE "/data")
    if(EXISTS "${YETTY_ROOT}/assets/yetty-unchained-2.mp4")
        _stage_one("${YETTY_ROOT}/assets/yetty-unchained-2.mp4"
                   "data" "yetty-unchained-2.mp4" FALSE "/data")
    endif()
    if(EXISTS "${YETTY_ROOT}/test/ut/ypdf/pdf-sample.pdf")
        _stage_one("${YETTY_ROOT}/test/ut/ypdf/pdf-sample.pdf"
                   "data" "pdf-sample.pdf" FALSE "/data")
    endif()
    if(EXISTS "${YETTY_ROOT}/README.md")
        _stage_one("${YETTY_ROOT}/README.md" "data" "README.md" FALSE "/data")
    endif()

    # Config: pick platform-specific default if it exists, else the generic one.
    set(_CFG "${YETTY_ROOT}/build-tools/yetty/platform/${YETTY_PLATFORM}/config.yaml")
    if(NOT EXISTS "${_CFG}")
        set(_CFG "${YETTY_ROOT}/build-tools/yetty/platform/config-defaults.yaml")
    endif()
    _stage_one("${_CFG}" "config" "config.yaml" FALSE "/config")
    # Always stage config-defaults.yaml as defaults.yaml so config.yaml can import it
    _stage_one("${YETTY_ROOT}/build-tools/yetty/platform/config-defaults.yaml"
               "config" "defaults.yaml" FALSE "/config")

    # Tinyemu cfgs — these used to live under <config_dir>/temu/. Keep
    # them around even though webasm no longer runs the in-process VM
    # (cfg lookup may exist for telnet/etc. paths).
    foreach(_F
            "${YETTY_ROOT}/assets/yemu/temu/yetty-temu.cfg"
            "${YETTY_ROOT}/assets/yemu/temu/yetty-temu-extended.cfg")
        get_filename_component(_NAME "${_F}" NAME)
        _stage_one("${_F}" "config/temu" "${_NAME}" FALSE "/config")
    endforeach()

    # Build the manifest JSON. Keep it human-readable for debuggability —
    # the file is small (~100 entries) so newlines don't hurt.
    string(TIMESTAMP _BUILD_STAMP "%Y%m%d%H%M%S")
    set(_JSON "{\n  \"version\": \"${_BUILD_STAMP}\",\n  \"entries\": [\n")

    list(LENGTH _MANIFEST_URLS _N)
    math(EXPR _LAST "${_N} - 1")
    set(_I 0)
    while(_I LESS _N)
        list(GET _MANIFEST_URLS  ${_I} _URL)
        list(GET _MANIFEST_DESTS ${_I} _DEST)
        list(GET _MANIFEST_BROTLI ${_I} _BR)
        if(_I LESS _LAST)
            set(_COMMA ",")
        else()
            set(_COMMA "")
        endif()
        string(APPEND _JSON
            "    { \"url\": \"${_URL}\", \"dest\": \"${_DEST}\", \"brotli\": ${_BR} }${_COMMA}\n")
        math(EXPR _I "${_I} + 1")
    endwhile()
    string(APPEND _JSON "  ]\n}\n")

    file(WRITE "${YETTY_WEBASM_ASSETS_DIR}/manifest.json" "${_JSON}")

    message(STATUS "webasm-stage-assets: staged ${_N} files into ${YETTY_WEBASM_ASSETS_DIR}")
endfunction()
