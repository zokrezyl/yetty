# Shared configuration for all platforms
# Include this before platform-specific target files

# Note: variables.cmake is included from root CMakeLists.txt

# Determine platform name for config file selection
# tvOS check before iOS — tvos.cmake doesn't set YETTY_IOS at cmake level,
# but APPLE is true for tvOS so without an explicit branch it would fall
# through to "macos" and pick assets/default-config-macos.yaml instead of
# the new assets/default-config-tvos.yaml.
if(YETTY_ANDROID)
    set(YETTY_PLATFORM "android")
elseif(YETTY_TVOS OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    set(YETTY_PLATFORM "tvos")
elseif(YETTY_IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(YETTY_PLATFORM "ios")
elseif(EMSCRIPTEN)
    set(YETTY_PLATFORM "webasm")
elseif(WIN32)
    set(YETTY_PLATFORM "windows")
elseif(APPLE)
    set(YETTY_PLATFORM "macos")
else()
    set(YETTY_PLATFORM "linux")
endif()

# Platform abstraction sources (thread, term, fs, time)
if(WIN32)
    set(YETTY_YPLATFORM_THREAD_SOURCES
        ${YETTY_ROOT}/src/yetty/yplatform/thread/windows.c
        ${YETTY_ROOT}/src/yetty/yplatform/term/windows.c
        ${YETTY_ROOT}/src/yetty/yplatform/fs/windows.c
        ${YETTY_ROOT}/src/yetty/yplatform/time/windows.c
    )
else()
    set(YETTY_YPLATFORM_THREAD_SOURCES
        ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/fs/default.c
        ${YETTY_ROOT}/src/yetty/yplatform/time/default.c
    )
endif()


# Prebuilt 3rdparty assets — each one its own per-lib fetch, version
# pinned in build-tools/3rdparty/<name>/version. yetty_3rdparty_fetch
# downloads + extracts + auto-decompresses .br files side-by-side.
include(${YETTY_ROOT}/build-tools/cmake/3rdparty-fetch.cmake)

# msdf-fonts (cdb): pre-brotli'd, embedded directly by incbin.
if(YETTY_ENABLE_FEATURE_CDB_GEN OR YETTY_ENABLE_FEATURE_MSDF_GEN)
    yetty_3rdparty_fetch(cdb _CDB_DIR)
endif()

# yemu runtime (kernel + opensbi + alpine ext4): consumed by both the
# embed pipeline (via .cdb.br/.bin.br/.elf.br/.img.br) and the runtime
# path mode (via the auto-decompressed raw files).
if(YETTY_ENABLE_LIB_TINYEMU OR YETTY_ENABLE_LIB_QEMU)
    yetty_3rdparty_fetch(linux       _LINUX_DIR)
    yetty_3rdparty_fetch(opensbi     _OPENSBI_DIR)
    yetty_3rdparty_fetch(alpine-disk _ALPINE_DIR)
    yetty_3rdparty_fetch(alpine-extended-disk _ALPINE_EXTENDED_DIR)

    # First-party yetty asset (not lib-) — riscv64 demos+tools + repo
    # checkout packed as a brotli ext4 image. Mounted RO at /opt/yetty
    # inside --temu and --qemu guests via virtio-blk drive1.
    include(${YETTY_ROOT}/build-tools/cmake/yetty-asset-fetch.cmake)
    yetty_asset_fetch(yetty-tools-riscv _YETTY_TOOLS_RISCV_DIR)

    # Path constants consumed by tinyemu-runtime.cmake (bundle copy at
    # build time) and any future runtime-path consumer. Point at the
    # auto-decompressed RAW files; 3rdparty-fetch keeps the .br alongside.
    set(TINYEMU_OPENSBI_PATH
        "${_OPENSBI_DIR}/opensbi-fw_jump.elf"
        CACHE FILEPATH "" FORCE)
    set(QEMU_OPENSBI_PATH
        "${_OPENSBI_DIR}/opensbi-fw_dynamic.bin"
        CACHE FILEPATH "" FORCE)
    set(TINYEMU_KERNEL_PATH
        "${_LINUX_DIR}/kernel-riscv64.bin"
        CACHE FILEPATH "" FORCE)
    set(TINYEMU_ROOTFS_IMG
        "${_ALPINE_DIR}/alpine-rootfs.img"
        CACHE FILEPATH "" FORCE)
    set(TINYEMU_ALPINE_EXTENDED_IMG
        "${_ALPINE_EXTENDED_DIR}/alpine-extended-rootfs.img"
        CACHE FILEPATH "" FORCE)
    set(YETTY_TOOLS_RISCV_IMG
        "${_YETTY_TOOLS_RISCV_DIR}/yetty-riscv-disk.img"
        CACHE FILEPATH "" FORCE)
endif()

# qemu binary — per-host platform (ships per-target tarball).
# Pull qemu.cmake to make qemu_embed_runtime() available; the from-source
# qemu_build() in there is dormant unless explicitly invoked.
if(YETTY_ENABLE_LIB_QEMU)
    yetty_3rdparty_fetch(qemu _QEMU_DIR)
    include(${YETTY_ROOT}/build-tools/cmake/qemu.cmake)
endif()

#-----------------------------------------------------------------------------
# Libraries — guarded by YETTY_ENABLE_LIB_*
#-----------------------------------------------------------------------------

if(YETTY_ENABLE_LIB_INCBIN)
    include(${YETTY_ROOT}/build-tools/cmake/incbin.cmake)
endif()

if(YETTY_ENABLE_LIB_ARGS)
    include(${YETTY_ROOT}/build-tools/cmake/libs/args.cmake)
endif()

if(YETTY_ENABLE_LIB_LZ4)
    include(${YETTY_ROOT}/build-tools/cmake/libs/lz4.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBUV)
    include(${YETTY_ROOT}/build-tools/cmake/libs/libuv.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBCO)
    include(${YETTY_ROOT}/build-tools/cmake/libs/co.cmake)
endif()

if(YETTY_ENABLE_LIB_GLM)
    include(${YETTY_ROOT}/build-tools/cmake/libs/glm.cmake)
endif()

if(YETTY_ENABLE_LIB_STB)
    include(${YETTY_ROOT}/build-tools/cmake/libs/stb.cmake)
endif()

if(YETTY_ENABLE_LIB_CGLTF)
    include(${YETTY_ROOT}/build-tools/cmake/libs/cgltf.cmake)
endif()

if(YETTY_ENABLE_LIB_YAML_CPP)
    include(${YETTY_ROOT}/build-tools/cmake/libs/yaml-cpp.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBYAML)
    include(${YETTY_ROOT}/build-tools/cmake/libs/libyaml.cmake)
endif()

if(YETTY_ENABLE_LIB_SPDLOG)
    include(${YETTY_ROOT}/build-tools/cmake/libs/spdlog.cmake)
endif()

if(YETTY_ENABLE_LIB_YTRACE)
    include(${YETTY_ROOT}/build-tools/cmake/libs/ytrace.cmake)
endif()

if(YETTY_ENABLE_LIB_MSGPACK)
    include(${YETTY_ROOT}/build-tools/cmake/libs/msgpack.cmake)
endif()

if(YETTY_ENABLE_LIB_WEBGPU)
    include(${YETTY_ROOT}/build-tools/cmake/libs/webgpu.cmake)
endif()

if(YETTY_ENABLE_LIB_VTERM)
    include(${YETTY_ROOT}/build-tools/cmake/libs/vterm.cmake)
endif()

if(YETTY_ENABLE_LIB_ZLIB)
    include(${YETTY_ROOT}/build-tools/cmake/libs/zlib.cmake)
endif()

if(YETTY_ENABLE_LIB_PDFIO)
    include(${YETTY_ROOT}/build-tools/cmake/libs/pdfio.cmake)
endif()

if(YETTY_ENABLE_LIB_NETSURF)
    include(${YETTY_ROOT}/build-tools/cmake/libs/netsurf.cmake)
endif()

if(YETTY_ENABLE_LIB_LEXBOR)
    include(${YETTY_ROOT}/build-tools/cmake/libs/lexbor.cmake)
endif()

if(YETTY_ENABLE_LIB_QUICKJS)
    include(${YETTY_ROOT}/build-tools/cmake/libs/quickjs.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBPNG)
    include(${YETTY_ROOT}/build-tools/cmake/libs/libpng.cmake)
endif()

if(YETTY_ENABLE_LIB_FREETYPE)
    # Pull in brotli + bzip2 first — yetty's freetype consumers (e.g.
    # FREETYPE_ALL_LIBS link order) reference brotlidec/bz2_static, and
    # those static archives now come from their own 3rdparty fetch.
    include(${YETTY_ROOT}/build-tools/cmake/libs/brotli.cmake)
    include(${YETTY_ROOT}/build-tools/cmake/libs/bzip2.cmake)
    include(${YETTY_ROOT}/build-tools/cmake/libs/freetype.cmake)
endif()

if(YETTY_ENABLE_LIB_MSDFGEN)
    include(${YETTY_ROOT}/build-tools/cmake/libs/msdfgen.cmake)
endif()

if(YETTY_ENABLE_LIB_CDB)
    include(${YETTY_ROOT}/build-tools/cmake/libs/cdb.cmake)
endif()

if(YETTY_ENABLE_LIB_THORVG)
    include(${YETTY_ROOT}/build-tools/cmake/thorvg.cmake)
endif()

if(YETTY_ENABLE_LIB_TREESITTER)
    include(${YETTY_ROOT}/build-tools/cmake/TreeSitter.cmake)
endif()

if(YETTY_ENABLE_LIB_DAV1D)
    include(${YETTY_ROOT}/build-tools/cmake/Dav1d.cmake)
endif()

if(YETTY_ENABLE_LIB_OPENH264)
    include(${YETTY_ROOT}/build-tools/cmake/openh264.cmake)
endif()

if(YETTY_ENABLE_LIB_MINIMP4)
    include(${YETTY_ROOT}/build-tools/cmake/minimp4.cmake)
endif()

if(YETTY_ENABLE_LIB_WASM3)
    include(${YETTY_ROOT}/build-tools/cmake/libs/wasm3.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBSSH2)
    include(${YETTY_ROOT}/build-tools/cmake/libs/libssh2.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBJPEG_TURBO)
    include(${YETTY_ROOT}/build-tools/cmake/libs/libjpeg-turbo.cmake)
endif()

# Reusable render utilities (GPU tile diff, …). Lives outside src/yetty so it
# can be consumed by both the main yetty modules and standalone tools. Must
# be declared before src/yetty so yetty_vnc (et al.) can link against it.
add_subdirectory(${YETTY_ROOT}/src/yetty/yrender-utils ${CMAKE_BINARY_DIR}/src/yetty/yrender-utils)

# Shared client-side support (libuv event loop + yface bridge for ymgui /
# ygui / yrich / ycat). Outside src/yetty for the same reason as
# yrender-utils — keeps server abstractions out of client tools. Declared
# before src/yetty so the ymgui-layer (server side) and ymgui frontend
# can both link it.
if(NOT EMSCRIPTEN)
    add_subdirectory(${YETTY_ROOT}/src/yetty/yclient ${CMAKE_BINARY_DIR}/src/yetty/yclient)
endif()

# Add src/yetty (populates YETTY_SOURCES, YETTY_CORE_SOURCES, builds feature libraries)
add_subdirectory(${YETTY_ROOT}/src/yetty ${CMAKE_BINARY_DIR}/src/yetty)

# Vendored portable getopt/getopt_long (shared by all platforms)
list(APPEND YETTY_SOURCES ${YETTY_ROOT}/src/yetty/yplatform/getopt.c)

# Unit tests (opt-in)
if(YETTY_ENABLE_FEATURE_TESTS)
    enable_testing()
    if(YETTY_ENABLE_FEATURE_YPDF)
        add_subdirectory(${YETTY_ROOT}/test/ut/ypdf ${CMAKE_BINARY_DIR}/test/ut/ypdf)
    endif()
    add_subdirectory(${YETTY_ROOT}/test/ut/ygui ${CMAKE_BINARY_DIR}/test/ut/ygui)
    # Integration: ylexbor harness driver. Self-skips when QuickJS or
    # the ylexbor target isn't built.
    add_subdirectory(${YETTY_ROOT}/test/integration/ylexbor
                     ${CMAKE_BINARY_DIR}/test/integration/ylexbor)
endif()

# Common include directories
set(YETTY_INCLUDES
    ${YETTY_ROOT}/src
    ${YETTY_ROOT}/include
)

# Common compile definitions
set(YETTY_DEFINITIONS
    CMAKE_SOURCE_DIR="${YETTY_ROOT}"
)

if(YETTY_ENABLE_LIB_THORVG)
    list(APPEND YETTY_DEFINITIONS YETTY_HAS_THORVG=1)
endif()

# Common libraries to link (only include what's enabled)
set(YETTY_LIBS "")

# Third-party library link platforms
if(YETTY_ENABLE_LIB_WEBGPU)
    list(APPEND YETTY_LIBS webgpu)
endif()
if(YETTY_ENABLE_LIB_GLM)
    list(APPEND YETTY_LIBS glm::glm)
endif()
if(YETTY_ENABLE_LIB_STB)
    list(APPEND YETTY_LIBS stb)
endif()
if(YETTY_ENABLE_LIB_CGLTF)
    list(APPEND YETTY_LIBS cgltf)
endif()
if(YETTY_ENABLE_LIB_YAML_CPP)
    list(APPEND YETTY_LIBS yaml-cpp)
endif()
if(YETTY_ENABLE_LIB_LIBYAML)
    list(APPEND YETTY_LIBS yaml)
endif()
if(YETTY_ENABLE_LIB_VTERM)
    list(APPEND YETTY_LIBS vterm)
endif()
if(YETTY_ENABLE_LIB_MSGPACK)
    # yetty's only msgpack consumer (yrpc) uses the C API → link the static
    # C library. The C++ msgpack-cxx target was dropped — see msgpack.cmake.
    list(APPEND YETTY_LIBS msgpack-c)
endif()
if(YETTY_ENABLE_LIB_MSDFGEN)
    list(APPEND YETTY_LIBS msdfgen::msdfgen-core msdfgen::msdfgen-ext ${FREETYPE_ALL_LIBS} ${BROTLIDEC_LIBRARIES})
endif()
if(YETTY_ENABLE_LIB_CDB)
    list(APPEND YETTY_LIBS cdb-wrapper)
endif()
if(YETTY_ENABLE_LIB_ARGS)
    list(APPEND YETTY_LIBS args)
endif()
if(YETTY_ENABLE_LIB_LZ4)
    list(APPEND YETTY_LIBS lz4_static)
endif()
if(YETTY_ENABLE_LIB_LIBUV)
    list(APPEND YETTY_LIBS uv_a)
endif()
if(YETTY_ENABLE_LIB_YTRACE)
    list(APPEND YETTY_LIBS ytrace::ytrace)
endif()
if(YETTY_ENABLE_LIB_GLFW)
    list(APPEND YETTY_LIBS glfw glfw3webgpu)
endif()
if(YETTY_ENABLE_LIB_LIBJPEG_TURBO)
    list(APPEND YETTY_LIBS turbojpeg-static)
endif()
if(YETTY_ENABLE_LIB_ZLIB)
    list(APPEND YETTY_LIBS zlibstatic)
endif()

# Core libraries (always linked)
# Note: yetty_yui comes first because it depends on yetty_term
list(APPEND YETTY_LIBS yetty_yui yetty_yterm yetty_yrender yetty_yrender_utils yetty_ywebgpu yetty_yevent)

# Feature library link platforms
if(YETTY_ENABLE_FEATURE_BASE)
    list(APPEND YETTY_LIBS yetty_base)
endif()
if(YETTY_ENABLE_FEATURE_FONT)
    list(APPEND YETTY_LIBS yetty_font)
endif()
if(YETTY_ENABLE_FEATURE_YECHO)
    list(APPEND YETTY_LIBS yetty_yecho)
endif()
if(YETTY_ENABLE_FEATURE_YDRAW)
    list(APPEND YETTY_LIBS yetty_ydraw)
endif()
if(YETTY_ENABLE_FEATURE_YPAINT)
    list(APPEND YETTY_LIBS yetty_ypaint)
endif()
if(YETTY_ENABLE_FEATURE_DIAGRAM)
    list(APPEND YETTY_LIBS yetty_diagram)
endif()
if(YETTY_ENABLE_FEATURE_YGRID)
    list(APPEND YETTY_LIBS ygrid)
endif()
if(YETTY_ENABLE_FEATURE_CARDS)
    list(APPEND YETTY_LIBS yetty_cards)
endif()
if(YETTY_ENABLE_FEATURE_YAST)
    list(APPEND YETTY_LIBS yetty_yast)
endif()
if(YETTY_ENABLE_FEATURE_TELNET)
    list(APPEND YETTY_LIBS yetty_telnet)
endif()
if(YETTY_ENABLE_FEATURE_SSH)
    list(APPEND YETTY_LIBS yetty_ssh)
endif()
if(YETTY_ENABLE_FEATURE_YMSDF_WGSL)
    list(APPEND YETTY_LIBS yetty_ymsdf_wgsl)
    # Polymorphic ymsdf wrapper now builds with WGSL alone (cpu backend
    # gated by YMSDF_GEN inside ymsdf/CMakeLists.txt).
    list(APPEND YETTY_LIBS yetty_ymsdf)
endif()
if(YETTY_ENABLE_FEATURE_GPU)
    list(APPEND YETTY_LIBS yetty_gpu)
endif()
if(YETTY_ENABLE_FEATURE_YVNC)
    list(APPEND YETTY_LIBS yetty_vnc)
endif()
if(YETTY_ENABLE_FEATURE_YDVNC)
    list(APPEND YETTY_LIBS yetty_ydvnc)
endif()
if(YETTY_ENABLE_FEATURE_YRPC)
    list(APPEND YETTY_LIBS yetty_yrpc)
endif()
if(YETTY_ENABLE_FEATURE_YVIDEO)
    list(APPEND YETTY_LIBS yetty_yvideo)
endif()

#-----------------------------------------------------------------------------
# yetty_embed_assets(TARGET)
#
# Embeds shaders, fonts, and CDB files into the target binary.
# Call this AFTER creating the target with add_executable/add_library.
# WebAssembly: provides empty stubs (uses --preload-file instead)
#-----------------------------------------------------------------------------
function(yetty_embed_assets TARGET)
    if(NOT YETTY_ENABLE_LIB_INCBIN)
        return()
    endif()

    # Collect ALL data assets into one build directory
    set(EMBED_DATA_DIR "${CMAKE_BINARY_DIR}/embed-data")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}/shaders")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}/fonts")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}/msdf-fonts")

    # Stamp a fresh version on every cmake configure so the runtime's
    # asset-extract marker check (yetty_incbin_assets_needs_extraction)
    # actually re-extracts after a rebuild that changed an embedded
    # shader / asset. The marker is per-install (~/.local/share/yetty/
    # .yetty-assets/version on Linux); without a per-build version it
    # stays at "dev" forever and the deployed shaders silently rot out
    # of sync with the binary's incbin'd ones — an editor change to
    # text-layer.wgsl never reaches the runtime until the user wipes
    # ~/.local/share/yetty/.yetty-assets/ by hand.
    string(TIMESTAMP YETTY_BUILD_STAMP "%Y%m%d%H%M%S")
    target_compile_definitions(${TARGET} PRIVATE
        YETTY_BUILD_VERSION="${YETTY_BUILD_STAMP}")

    # Copy logo
    file(COPY "${YETTY_ROOT}/docs/logo-2.jpeg" DESTINATION "${EMBED_DATA_DIR}")

    # Collect shaders from module locations
    file(COPY "${YETTY_ROOT}/src/yetty/yterm/text-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yterm/ypaint-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yterm/ymgui-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yterm/background-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    # Generated SDF dispatcher + sdf_* functions — attached at runtime as a
    # child resource set of ypaint-layer; see src/yetty/ysdf/gen-sdf-code.py.
    file(COPY "${YETTY_ROOT}/src/yetty/ysdf/ysdf.gen.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yrender/blend.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(RENAME "${EMBED_DATA_DIR}/shaders/blend.wgsl" "${EMBED_DATA_DIR}/shaders/blender.wgsl")
    file(COPY "${YETTY_ROOT}/src/yetty/yfont/ms-msdf-font.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yfont/msdf-font.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yfont/ms-raster-font.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yfont/raster-font.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    # GPU MSDF compute shader — used by the ymsdf-wgsl generator when the
    # canvas materialises a font-blob FONT prim (PDF embedded / fontconfig
    # substituted). Without this the GPU generator can't init its pipeline
    # and every PDF font materialise fails → spans fall back to the
    # default font → per-glyph layout broken.
    file(COPY "${YETTY_ROOT}/src/yetty/ymsdf-wgsl/shaders/msdf_gen.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")

    # Shader-glyph layer + per-glyph procedurals. Layer reads the .wgsl
    # template AND scans glyph-shaders/*.wgsl at runtime, splices them into
    # one compiled shader. Adding a glyph is dropping a .wgsl file.
    file(COPY "${YETTY_ROOT}/src/yetty/yterm/shader-glyph-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}/shaders/glyph-shaders")
    # Glob both 0x*.wgsl (per-glyph procedurals) AND _*.wgsl (shared prelude
    # libs). The layer's runtime assembler reads `_*.wgsl` first, then glyphs.
    file(GLOB GLYPH_SHADER_FILES
        "${YETTY_ROOT}/src/yetty/yfont/glyph-shaders/0x*.wgsl"
        "${YETTY_ROOT}/src/yetty/yfont/glyph-shaders/_*.wgsl"
    )
    foreach(GLYPH_FILE ${GLYPH_SHADER_FILES})
        file(COPY "${GLYPH_FILE}" DESTINATION "${EMBED_DATA_DIR}/shaders/glyph-shaders")
    endforeach()

    # Copy fonts
    file(GLOB FONT_FILES "${YETTY_ROOT}/assets/fonts/*.ttf")
    foreach(FONT_FILE ${FONT_FILES})
        file(COPY "${FONT_FILE}" DESTINATION "${EMBED_DATA_DIR}/fonts")
    endforeach()

    # Copy msdf-fonts (cdb shipped pre-brotli'd as *.cdb.br; incbin's
    # already-compressed path embeds the bytes as-is and strips .br from
    # the in-binary asset name). Source dir comes from yetty_3rdparty_fetch(cdb).
    file(GLOB MSDF_FILES "${YETTY_3RDPARTY_cdb_DIR}/*.cdb.br")
    foreach(MSDF_FILE ${MSDF_FILES})
        file(COPY "${MSDF_FILE}" DESTINATION "${EMBED_DATA_DIR}/msdf-fonts")
    endforeach()

    # Embed ALL data assets (brotli compressed)
    incbin_add_directory(${TARGET} "data" "${EMBED_DATA_DIR}" "*" TRUE)

    # Collect config into separate build directory
    set(EMBED_CONFIG_DIR "${CMAKE_BINARY_DIR}/embed-config")
    file(MAKE_DIRECTORY "${EMBED_CONFIG_DIR}")
    set(DEFAULT_CONFIG_FILE "${YETTY_ROOT}/assets/default-config-${YETTY_PLATFORM}.yaml")
    if(NOT EXISTS "${DEFAULT_CONFIG_FILE}")
        set(DEFAULT_CONFIG_FILE "${YETTY_ROOT}/assets/default-config.yaml")
    endif()
    file(COPY "${DEFAULT_CONFIG_FILE}" DESTINATION "${EMBED_CONFIG_DIR}")
    get_filename_component(CONFIG_FILENAME "${DEFAULT_CONFIG_FILE}" NAME)
    # Rename to config.yaml when extracted
    file(RENAME "${EMBED_CONFIG_DIR}/${CONFIG_FILENAME}" "${EMBED_CONFIG_DIR}/config.yaml")

    # Stage tinyemu cfgs under yconfig/temu/ so extract-assets drops them at
    # <config_dir>/temu/. tinyemu-pty.c reads <config_dir>/temu/yetty-temu-extended.cfg
    # for --temu (the file is the source of truth — no in-process auto-gen).
    file(MAKE_DIRECTORY "${EMBED_CONFIG_DIR}/temu")
    file(COPY
        "${YETTY_ROOT}/assets/yemu/temu/yetty-temu.cfg"
        "${YETTY_ROOT}/assets/yemu/temu/yetty-temu-extended.cfg"
        DESTINATION "${EMBED_CONFIG_DIR}/temu")

    # Embed config (not compressed)
    incbin_add_directory(${TARGET} "yconfig" "${EMBED_CONFIG_DIR}" "*" FALSE)

    # Embed shared RISC-V runtime (kernel, opensbi, rootfs) under yemu/ prefix.
    # Used by both --temu (TinyEMU, in-process) and --qemu (external QEMU via
    # telnet). After the per-asset 3rdparty split, files come from three
    # separate fetched dirs (linux, opensbi, alpine-disk). Producer ships
    # them brotli-q11 (*.br); 3rdparty-fetch auto-decompresses raw copies
    # side-by-side for runtime path mode (see tinyemu_copy_runtime_to_bundle).
    # incbin's already-compressed path embeds the .br bytes as-is and strips
    # the .br suffix from the in-binary asset name.
    if(YETTY_ENABLE_LIB_TINYEMU OR YETTY_ENABLE_LIB_QEMU)
        set(EMBED_YEMU_DIR "${CMAKE_BINARY_DIR}/embed-yemu")
        file(REMOVE_RECURSE "${EMBED_YEMU_DIR}")
        file(MAKE_DIRECTORY "${EMBED_YEMU_DIR}")

        # source-dir → file (per-asset)
        foreach(_PAIR
                "${YETTY_3RDPARTY_linux_DIR}|kernel-riscv64.bin.br"
                "${YETTY_3RDPARTY_opensbi_DIR}|opensbi-fw_jump.elf.br"
                "${YETTY_3RDPARTY_opensbi_DIR}|opensbi-fw_dynamic.bin.br"
                "${YETTY_3RDPARTY_alpine-disk_DIR}|alpine-rootfs.img.br"
                "${YETTY_3RDPARTY_alpine-extended-disk_DIR}|alpine-extended-rootfs.img.br"
                "${YETTY_ASSET_yetty-tools-riscv_DIR}|yetty-riscv-disk.img.br")
            string(REPLACE "|" ";" _PARTS "${_PAIR}")
            list(GET _PARTS 0 _SRC_DIR)
            list(GET _PARTS 1 _F)
            if(EXISTS "${_SRC_DIR}/${_F}")
                file(COPY "${_SRC_DIR}/${_F}" DESTINATION "${EMBED_YEMU_DIR}")
            endif()
        endforeach()

        incbin_add_directory(${TARGET} "yemu" "${EMBED_YEMU_DIR}" "*" FALSE)
    endif()

    # Embed QEMU binary if enabled (fetched by yetty_3rdparty_fetch(qemu))
    if(YETTY_ENABLE_LIB_QEMU)
        qemu_embed_runtime(${TARGET})
    endif()

    # Make manifest headers available
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
