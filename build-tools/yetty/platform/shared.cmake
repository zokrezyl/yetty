# Shared configuration for all platforms
# Include this before platform-specific target files

# Note: variables.cmake is included from root CMakeLists.txt

# Compute YETTY_BUILD_VERSION_STR once per configure: a stable identifier of
# the current source state. Used by yetty_embed_assets() below to bake the
# value into each target so the runtime asset-extract check can detect a
# version change and re-extract.
#
# Algorithm: git short SHA (12 chars) + "-dirty" if the working tree has
# uncommitted changes; falls back to a YYYYMMDDHHMMSS timestamp when .git
# is absent (source tarball builds).
function(yetty_compute_build_version)
    if(DEFINED CACHE{YETTY_BUILD_VERSION_STR})
        return()
    endif()
    set(_v "")
    if(EXISTS "${YETTY_ROOT}/.git")
        find_package(Git QUIET)
        if(Git_FOUND)
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" -C "${YETTY_ROOT}" rev-parse --short=12 HEAD
                OUTPUT_VARIABLE _sha
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _rc
                ERROR_QUIET)
            if(_rc EQUAL 0 AND _sha)
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" -C "${YETTY_ROOT}" status --porcelain
                    OUTPUT_VARIABLE _dirty
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
                if(_dirty)
                    set(_v "${_sha}-dirty")
                else()
                    set(_v "${_sha}")
                endif()
            endif()
            # Make CMake reconfigure when the commit moves (.git/HEAD on
            # commit / checkout / reset; .git/index on `git add`). Without
            # this, the baked-in value stays stale until something else
            # triggers a reconfigure.
            set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
                CMAKE_CONFIGURE_DEPENDS
                "${YETTY_ROOT}/.git/HEAD"
                "${YETTY_ROOT}/.git/index")
        endif()
    endif()
    if(NOT _v)
        string(TIMESTAMP _v "%Y%m%d%H%M%S")
    endif()
    set(YETTY_BUILD_VERSION_STR "${_v}" CACHE INTERNAL "yetty source-state stamp")
    message(STATUS "yetty: build version = ${_v}")
endfunction()

# Determine platform name for config file selection
# tvOS check before iOS — tvos.cmake doesn't set YETTY_IOS at cmake level,
# but APPLE is true for tvOS so without an explicit branch it would fall
# through to "macos" and pick platform/macos/config.yaml instead of
# platform/tvos/config.yaml.
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

# Platform abstractions are linked from the yetty_yplatform_core and
# yetty_yplatform libraries (declared in src/yetty/yplatform/CMakeLists.txt).
# Tools target_link_libraries(... yetty_yplatform_core); the main yetty
# exec links both via target_link_libraries in each platform's cmake.cmake.


# Prebuilt 3rdparty assets — each one its own per-lib fetch, version
# pinned in build-tools/3rdparty/<name>/version. yetty_3rdparty_fetch
# downloads + extracts + auto-decompresses .br files side-by-side.
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

# msdf-fonts (cdb): pre-brotli'd, embedded directly by incbin.
if(YETTY_ENABLE_FEATURE_CDB_GEN OR YETTY_ENABLE_FEATURE_MSDF_GEN)
    yetty_3rdparty_fetch(cdb _CDB_DIR)
endif()

# yemu runtime (kernel + opensbi + alpine + unified yetty rootfs):
# consumed by both the embed pipeline (via *.br) and the runtime-path
# mode (via the auto-decompressed raw files).
if(YETTY_ENABLE_LIB_TINYEMU OR YETTY_ENABLE_LIB_QEMU)
    yetty_3rdparty_fetch(linux       _LINUX_DIR)
    yetty_3rdparty_fetch(opensbi     _OPENSBI_DIR)
    yetty_3rdparty_fetch(alpine-disk _ALPINE_DIR)

    # First-party yetty asset (not lib-) — alpine-extended userland with
    # cross-compiled riscv64 demos+tools at /yetty/bin and `git archive
    # HEAD` at /yetty/repo, in one bootable ext4 image. Mounted as
    # drive0 in --temu and --qemu guests; replaces the previous two-disk
    # setup (alpine-extended-rootfs.img + yetty-riscv-disk.img).
    #
    # Version tracks yetty itself — the rootfs is built by the same CI
    # pipeline that tags yetty-X.Y.Z (build-yetty-rootfs-riscv.yml runs
    # first inside cmake-multi-platform.yml). No version file: derive
    # from the latest yetty-X.Y.Z tag reachable from HEAD.
    #
    # Skip entirely when this build *produces* the rootfs (the riscv64
    # cross-compile feeds /yetty/bin into yetty-rootfs-riscv-<ver>.tar.gz
    # via build-tools/yemu/yetty-rootfs-riscv/build.sh). Fetching here
    # would be circular and would FATAL on a freshly bumped yetty tag
    # before that tag's release has been cut.
    if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
        include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

        execute_process(
            COMMAND git -C "${YETTY_ROOT}" describe --tags --abbrev=0
                    --match "yetty-[0-9]*.[0-9]*.[0-9]*" HEAD
            OUTPUT_VARIABLE _YR_TAG OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _YR_GIT_RC
            ERROR_QUIET)
        if(NOT _YR_GIT_RC EQUAL 0 OR NOT _YR_TAG)
            message(FATAL_ERROR
                "yetty-rootfs-riscv: cannot resolve version — no yetty-X.Y.Z "
                "tag reachable from HEAD. Run `git fetch --tags`, or stage a "
                "pre-built tarball into ${YETTY_3RDPARTY_CACHE_DIR}.")
        endif()
        string(REGEX REPLACE "^yetty-" "" _YR_VER "${_YR_TAG}")

        set(_YR_FILE   "yetty-rootfs-riscv-${_YR_VER}.tar.gz")
        set(_YR_URL    "${YETTY_3RDPARTY_URL_BASE}/yetty-rootfs-riscv-${_YR_VER}/${_YR_FILE}")
        set(_YR_CACHED "${YETTY_3RDPARTY_CACHE_DIR}/${_YR_FILE}")
        set(_YR_DEST   "${CMAKE_BINARY_DIR}/yetty-assets/yetty-rootfs-riscv")
        set(_YR_STAMP  "${_YR_DEST}/.fetched-${_YR_VER}")

        if(NOT EXISTS "${_YR_STAMP}")
            if(NOT EXISTS "${_YR_CACHED}")
                # In CI the stage-rootfs-riscv composite action drops the
                # workflow-artifact tarball here before configure runs,
                # so the download branch only fires on local builds /
                # external consumers.
                message(STATUS "yetty-rootfs-riscv: downloading ${_YR_FILE}")
                file(DOWNLOAD "${_YR_URL}" "${_YR_CACHED}"
                    SHOW_PROGRESS STATUS _YR_DL TLS_VERIFY ON)
                list(GET _YR_DL 0 _YR_DL_CODE)
                if(NOT _YR_DL_CODE EQUAL 0)
                    file(REMOVE "${_YR_CACHED}")
                    message(FATAL_ERROR
                        "yetty-rootfs-riscv: download failed for ${_YR_URL} "
                        "(${_YR_DL}). Resolved version: ${_YR_VER}. Either "
                        "the release isn't cut yet, or pre-stage the tarball "
                        "into ${YETTY_3RDPARTY_CACHE_DIR}.")
                endif()
            endif()
            file(REMOVE_RECURSE "${_YR_DEST}")
            file(MAKE_DIRECTORY "${_YR_DEST}")
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "${_YR_CACHED}"
                WORKING_DIRECTORY "${_YR_DEST}"
                RESULT_VARIABLE _YR_TAR)
            if(NOT _YR_TAR EQUAL 0)
                message(FATAL_ERROR
                    "yetty-rootfs-riscv: failed to extract ${_YR_CACHED}")
            endif()
            # The tarball ships .img.br only; downstream embed pipeline
            # uses the .br directly, but tinyemu-runtime.cmake's bundle
            # copy expects the raw .img too. Auto-decompress alongside.
            file(GLOB_RECURSE _YR_BR_FILES "${_YR_DEST}/*.br")
            find_program(BROTLI_EXECUTABLE brotli)
            if(_YR_BR_FILES AND BROTLI_EXECUTABLE)
                foreach(_BR ${_YR_BR_FILES})
                    string(REGEX REPLACE "\\.br$" "" _RAW "${_BR}")
                    if(NOT EXISTS "${_RAW}")
                        execute_process(
                            COMMAND "${BROTLI_EXECUTABLE}" -d -k -f
                                    -o "${_RAW}" "${_BR}")
                    endif()
                endforeach()
            endif()
            file(WRITE "${_YR_STAMP}" "${_YR_VER}\n")
        endif()
        set(YETTY_ROOTFS_RISCV_IMG
            "${_YR_DEST}/yetty-rootfs-riscv.img"
            CACHE FILEPATH "" FORCE)
    endif()

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
endif()

# qemu binary — per-host platform (ships per-target tarball).
# Pull qemu.cmake to make qemu_embed_runtime() available; the from-source
# qemu_build() in there is dormant unless explicitly invoked. Gated by
# the *_BINARY flag, not *_LIB_QEMU: iOS/tvOS link the launcher lib (so
# pty-factory/default.c's --qemu branch resolves) but don't have a
# qemu-system-riscv64 tarball to fetch or embed.
if(YETTY_ENABLE_LIB_QEMU_BINARY)
    yetty_3rdparty_fetch(qemu _QEMU_DIR)
    include(${YETTY_ROOT}/build-tools/yetty/qemu.cmake)
endif()

#-----------------------------------------------------------------------------
# Libraries — guarded by YETTY_ENABLE_LIB_*
#-----------------------------------------------------------------------------

if(YETTY_ENABLE_LIB_INCBIN)
    include(${YETTY_ROOT}/build-tools/yetty/incbin.cmake)
endif()

if(YETTY_ENABLE_LIB_ARGS)
    include(${YETTY_ROOT}/build-tools/yetty/libs/args.cmake)
endif()

if(YETTY_ENABLE_LIB_LZ4)
    include(${YETTY_ROOT}/build-tools/yetty/libs/lz4.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBUV)
    include(${YETTY_ROOT}/build-tools/yetty/libs/libuv.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBCO)
    include(${YETTY_ROOT}/build-tools/yetty/libs/co.cmake)
endif()

if(YETTY_ENABLE_LIB_GLM)
    include(${YETTY_ROOT}/build-tools/yetty/libs/glm.cmake)
endif()

if(YETTY_ENABLE_LIB_STB)
    include(${YETTY_ROOT}/build-tools/yetty/libs/stb.cmake)
endif()

if(YETTY_ENABLE_LIB_CGLTF)
    include(${YETTY_ROOT}/build-tools/yetty/libs/cgltf.cmake)
endif()

if(YETTY_ENABLE_LIB_YAML_CPP)
    include(${YETTY_ROOT}/build-tools/yetty/libs/yaml-cpp.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBYAML)
    include(${YETTY_ROOT}/build-tools/yetty/libs/libyaml.cmake)
endif()

if(YETTY_ENABLE_LIB_SPDLOG)
    include(${YETTY_ROOT}/build-tools/yetty/libs/spdlog.cmake)
endif()

if(YETTY_ENABLE_LIB_YTRACE)
    include(${YETTY_ROOT}/build-tools/yetty/libs/ytrace.cmake)
endif()

if(YETTY_ENABLE_LIB_MSGPACK)
    include(${YETTY_ROOT}/build-tools/yetty/libs/msgpack.cmake)
endif()

if(YETTY_ENABLE_LIB_WEBGPU)
    include(${YETTY_ROOT}/build-tools/yetty/libs/webgpu.cmake)
else()
    # Headers-only fallback. Without WebGPU (e.g. the riscv64 cross),
    # we still need <webgpu/webgpu.h> to resolve because <yetty/yetty/
    # yetty.h> uses WGPU* types in struct declarations. The header
    # comes from the committed yrdawn copy — same surface every
    # platform sees. wgpu* function calls won't link; keep them gated
    # on YETTY_ENABLE_LIB_WEBGPU at call sites.
    add_library(webgpu INTERFACE)
    target_include_directories(webgpu INTERFACE
        ${YETTY_ROOT}/include/yetty/yrdawn)
endif()

if(YETTY_ENABLE_LIB_VTERM)
    include(${YETTY_ROOT}/build-tools/yetty/libs/vterm.cmake)
endif()

if(YETTY_ENABLE_LIB_ZLIB)
    include(${YETTY_ROOT}/build-tools/yetty/libs/zlib.cmake)
endif()

if(YETTY_ENABLE_LIB_PDFIO)
    include(${YETTY_ROOT}/build-tools/yetty/libs/pdfio.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBJPEG_TURBO)
    include(${YETTY_ROOT}/build-tools/yetty/libs/libjpeg-turbo.cmake)
endif()

if(YETTY_ENABLE_LIB_NETSURF)
    include(${YETTY_ROOT}/build-tools/yetty/libs/netsurf.cmake)
endif()

if(YETTY_ENABLE_LIB_LEXBOR)
    include(${YETTY_ROOT}/build-tools/yetty/libs/lexbor.cmake)
endif()

if(YETTY_ENABLE_LIB_QUICKJS)
    include(${YETTY_ROOT}/build-tools/yetty/libs/quickjs.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBPNG)
    include(${YETTY_ROOT}/build-tools/yetty/libs/libpng.cmake)
endif()

if(YETTY_ENABLE_LIB_FREETYPE)
    # Pull in brotli + bzip2 first — yetty's freetype consumers (e.g.
    # FREETYPE_ALL_LIBS link order) reference brotlidec/bz2_static, and
    # those static archives now come from their own 3rdparty fetch.
    include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)
    include(${YETTY_ROOT}/build-tools/yetty/libs/bzip2.cmake)
    include(${YETTY_ROOT}/build-tools/yetty/libs/freetype.cmake)
endif()

if(YETTY_ENABLE_LIB_MSDFGEN)
    include(${YETTY_ROOT}/build-tools/yetty/libs/msdfgen.cmake)
endif()

if(YETTY_ENABLE_LIB_CDB)
    include(${YETTY_ROOT}/build-tools/yetty/libs/cdb.cmake)
endif()

if(YETTY_ENABLE_LIB_THORVG)
    include(${YETTY_ROOT}/build-tools/yetty/thorvg.cmake)
endif()

if(YETTY_ENABLE_LIB_TREESITTER)
    include(${YETTY_ROOT}/build-tools/yetty/TreeSitter.cmake)
endif()

if(YETTY_ENABLE_LIB_DAV1D)
    include(${YETTY_ROOT}/build-tools/yetty/Dav1d.cmake)
endif()

if(YETTY_ENABLE_LIB_OPENH264)
    include(${YETTY_ROOT}/build-tools/yetty/openh264.cmake)
endif()

if(YETTY_ENABLE_LIB_MINIMP4)
    include(${YETTY_ROOT}/build-tools/yetty/minimp4.cmake)
endif()

if(YETTY_ENABLE_LIB_MINIAUDIO)
    include(${YETTY_ROOT}/build-tools/yetty/miniaudio.cmake)
    include(${YETTY_ROOT}/build-tools/yetty/yplatform-audio.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBOPUS)
    include(${YETTY_ROOT}/build-tools/yetty/libopus.cmake)
endif()

if(YETTY_ENABLE_LIB_WASM3)
    include(${YETTY_ROOT}/build-tools/yetty/libs/wasm3.cmake)
endif()

if(YETTY_ENABLE_LIB_LIBSSH2)
    include(${YETTY_ROOT}/build-tools/yetty/libs/libssh2.cmake)
endif()

# yclass — tiny class/object runtime + optional binary RPC. Lives at
# the top of src/ (sibling of src/yetty) like ut/uthash on the include
# side. Declared before src/yetty so any module under src/yetty/ can
# link yetty_yclass.
add_subdirectory(${YETTY_ROOT}/src/yetty/yclass ${CMAKE_BINARY_DIR}/src/yetty/yclass)

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

# getopt + the rest of the platform layer live in yetty_yplatform_core
# (declared in src/yetty/yplatform/CMakeLists.txt). Tools link that lib
# directly; the main yetty exec links it via platform/<plat>/cmake.cmake.

# Unit tests (opt-in)
if(YETTY_ENABLE_FEATURE_TESTS)
    enable_testing()
    if(YETTY_ENABLE_FEATURE_YPDF)
        add_subdirectory(${YETTY_ROOT}/test/ut/ypdf ${CMAKE_BINARY_DIR}/test/ut/ypdf)
    endif()
    if(YETTY_ENABLE_FEATURE_YDIAGRAM)
        add_subdirectory(${YETTY_ROOT}/test/ut/ydiagram ${CMAKE_BINARY_DIR}/test/ut/ydiagram)
    endif()
    if(YETTY_ENABLE_FEATURE_YMARKDOWN)
        add_subdirectory(${YETTY_ROOT}/test/ut/ymarkdown ${CMAKE_BINARY_DIR}/test/ut/ymarkdown)
    endif()
    add_subdirectory(${YETTY_ROOT}/test/ut/ygui ${CMAKE_BINARY_DIR}/test/ut/ygui)
    add_subdirectory(${YETTY_ROOT}/test/ut/yfigure ${CMAKE_BINARY_DIR}/test/ut/yfigure)
    add_subdirectory(${YETTY_ROOT}/test/ut/ygrid ${CMAKE_BINARY_DIR}/test/ut/ygrid)
    add_subdirectory(${YETTY_ROOT}/test/ut/ybrowser ${CMAKE_BINARY_DIR}/test/ut/ybrowser)
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

# YETTY_HAS_YMGUI gates yframework's ymgui figure-kind registration. Off on
# webasm (imgui prebuilt unavailable) — see webasm/variables.cmake.
if(YETTY_ENABLE_FEATURE_YMGUI)
    list(APPEND YETTY_DEFINITIONS YETTY_HAS_YMGUI=1)
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
    # yetty's only msgpack consumer (yctl) uses the C API → link the static
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
# Order: yui (composes views) → yterm (concrete view) → yui-core (abstract
# view interface, depends on neither). With the cycle removed there are no
# duplicate archive entries on the link line.
# yetty_yetty is the module wrapping yetty.c + <yetty/yetty/yetty.h>; it
# PUBLIC-links webgpu so anything linking it gets the webgpu include path.
list(APPEND YETTY_LIBS yetty_yetty yetty_yui yetty_yterminal yetty_yvterm yetty_yui_core yetty_yrender yetty_yrender_utils yetty_ywebgpu yetty_yevent)

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
if(YETTY_ENABLE_FEATURE_YDRAW)
    list(APPEND YETTY_LIBS yetty_ydraw)
endif()
if(YETTY_ENABLE_FEATURE_YDIAGRAM)
    list(APPEND YETTY_LIBS yetty_ydiagram)
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
if(YETTY_ENABLE_FEATURE_YCTL)
    list(APPEND YETTY_LIBS yetty_yctl)
endif()
if(YETTY_ENABLE_FEATURE_YVCODEC)
    list(APPEND YETTY_LIBS yetty_yvcodec)
endif()
if(YETTY_ENABLE_FEATURE_YACODEC AND YETTY_ENABLE_LIB_LIBOPUS)
    list(APPEND YETTY_LIBS yetty_yacodec)
endif()
if(YETTY_ENABLE_LIB_MINIAUDIO)
    list(APPEND YETTY_LIBS yetty_yplatform_audio)
endif()
if(YETTY_ENABLE_FEATURE_YVIDEO)
    list(APPEND YETTY_LIBS yetty_yvideo_core)
    if(YETTY_ENABLE_LIB_WEBGPU AND YETTY_ENABLE_LIB_OPENH264)
        list(APPEND YETTY_LIBS yetty_yvideo)
    endif()
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

    # Stamp a per-source version baked into the binary so the runtime's
    # asset-extract marker check (yetty_incbin_assets_needs_extraction)
    # re-extracts whenever the embedded shaders / fonts could have changed.
    # The marker lives under the per-install data dir (~/.local/share/yetty
    # on Linux, %LOCALAPPDATA%\yetty\data on Windows); without a per-build
    # version it stays at "dev" forever and an editor change to text-layer.wgsl
    # never reaches the runtime until the user wipes the data dir by hand.
    #
    # Prefer the git HEAD short hash (+ "-dirty" when there are uncommitted
    # changes) over a wall-clock timestamp: same commit reconfigured a second
    # time keeps the same version, so incidental reconfigures (editing an
    # unrelated CMakeLists, deleting build.ninja) no longer force a 950 MB
    # asset re-extraction. Tied to .git/HEAD + .git/index so CMake reconfigures
    # on commit / checkout / `git add`. Falls back to the timestamp in source
    # tarball builds where .git is absent.
    yetty_compute_build_version()
    target_compile_definitions(${TARGET} PRIVATE
        YETTY_BUILD_VERSION="${YETTY_BUILD_VERSION_STR}")

    # Collect shaders from module locations
    file(COPY "${YETTY_ROOT}/src/yetty/yvterm/text-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yvterm/ydraw-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/ygrid/ygrid.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/ymgui/ymgui-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yterminal/background-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    # Generated SDF dispatcher + sdf_* functions — attached at runtime as a
    # child resource set of ydraw-layer; see src/yetty/ysdf/gen-sdf-code.py.
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
    file(COPY "${YETTY_ROOT}/src/yetty/yvterm/shader-glyph-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
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
    set(DEFAULT_CONFIG_FILE "${YETTY_ROOT}/build-tools/yetty/platform/${YETTY_PLATFORM}/config.yaml")
    if(NOT EXISTS "${DEFAULT_CONFIG_FILE}")
        set(DEFAULT_CONFIG_FILE "${YETTY_ROOT}/build-tools/yetty/platform/config-defaults.yaml")
    endif()
    file(COPY "${DEFAULT_CONFIG_FILE}" DESTINATION "${EMBED_CONFIG_DIR}")
    get_filename_component(CONFIG_FILENAME "${DEFAULT_CONFIG_FILE}" NAME)
    # Rename to config.yaml when extracted
    file(RENAME "${EMBED_CONFIG_DIR}/${CONFIG_FILENAME}" "${EMBED_CONFIG_DIR}/config.yaml")
    # Always embed config-defaults.yaml as defaults.yaml so config.yaml can import it
    file(COPY "${YETTY_ROOT}/build-tools/yetty/platform/config-defaults.yaml"
         DESTINATION "${EMBED_CONFIG_DIR}")
    file(RENAME "${EMBED_CONFIG_DIR}/config-defaults.yaml" "${EMBED_CONFIG_DIR}/defaults.yaml")

    # Stage tinyemu cfgs under yconfig/temu/ so extract-assets drops them at
    # <config_dir>/temu/. tinyemu-pty.c reads <config_dir>/temu/yetty-temu-extended.cfg
    # for --temu (the file is the source of truth — no in-process auto-gen).
    file(MAKE_DIRECTORY "${EMBED_CONFIG_DIR}/temu")
    file(COPY
        "${YETTY_ROOT}/assets/yemu/temu/yetty-temu.cfg"
        "${YETTY_ROOT}/assets/yemu/temu/yetty-temu-extended.cfg"
        DESTINATION "${EMBED_CONFIG_DIR}/temu")

    # Embed config (brotli-compressed; the extractor inflates on the way out)
    incbin_add_directory(${TARGET} "yconfig" "${EMBED_CONFIG_DIR}" "*" TRUE)

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

        # source-dir → file (per-asset). yetty-rootfs-riscv lives under
        # CMAKE_BINARY_DIR/yetty-assets/... — produced by the dedicated
        # fetch block at the top of this file. Its absence at this point
        # (e.g. riscv cross-build that skips the fetch) is silently
        # tolerated by the `if(EXISTS ...)` guard below.
        foreach(_PAIR
                "${YETTY_3RDPARTY_linux_DIR}|kernel-riscv64.bin.br"
                "${YETTY_3RDPARTY_opensbi_DIR}|opensbi-fw_jump.elf.br"
                "${YETTY_3RDPARTY_opensbi_DIR}|opensbi-fw_dynamic.bin.br"
                "${YETTY_3RDPARTY_alpine-disk_DIR}|alpine-rootfs.img.br"
                "${CMAKE_BINARY_DIR}/yetty-assets/yetty-rootfs-riscv|yetty-rootfs-riscv.img.br")
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
    if(YETTY_ENABLE_LIB_QEMU_BINARY)
        qemu_embed_runtime(${TARGET})
    endif()

    # Make manifest headers available
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
