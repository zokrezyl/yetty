# Shared configuration for all platforms
# Include this before platform-specific target files

# Note: variables.cmake is included from root CMakeLists.txt

# Compute YETTY_BUILD_VERSION_STR once per configure: a stable identifier of
# the current source state. Used by yetty_embed_assets() below to bake the
# value into each target so the runtime asset-extract check can detect a
# version change and re-extract.
#
# Algorithm: git short SHA (12 chars) + "-dirty" if TRACKED files differ
# from HEAD; falls back to a YYYYMMDDHHMMSS timestamp when .git is absent
# (source tarball builds).
#
# Untracked files are deliberately ignored (--untracked-files=no): CI stages
# build inputs/outputs inside the checkout (downloaded rootfs artifacts,
# per-platform staging dirs, generated files not under version control), and
# those must NOT make a pristine release build report "-dirty". Only an actual
# modification to a committed file means the source state differs from HEAD.
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
                            --untracked-files=no
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

# fonts: one noarch tarball with the complete font asset set —
#   *.cdb.br  MSDF CDB atlases for the base DejaVu faces and the Emmentaler
#             music face — ymusic references "Emmentaler" by name (embedded
#             verbatim into the msdf-fonts staging by incbin)
#   *.ttf     the Noto world-coverage set (script faces + CJK + Color Emoji),
#             staged into the runtime fonts dir where the terminal's
#             codepoint-range font routing resolves them by name
if(YETTY_ENABLE_FEATURE_CDB_GEN OR YETTY_ENABLE_FEATURE_MSDF_GEN OR YETTY_ENABLE_NOTO_FONTS)
    yetty_3rdparty_fetch(fonts _FONTS_DIR)
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

# Declared first: yetty_ycore (added via src/yetty below) links
# mimalloc::mimalloc for the memstats sampler, so the imported target
# must exist before the src/yetty subdirectory pass.
if(YETTY_ENABLE_LIB_MIMALLOC)
    include(${YETTY_ROOT}/build-tools/yetty/libs/mimalloc.cmake)
endif()

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

if(YETTY_ENABLE_LIB_HARFBUZZ)
    # Standalone shaper — fed font tables from the FreeType faces at runtime,
    # so it carries no freetype link dependency here.
    include(${YETTY_ROOT}/build-tools/yetty/libs/harfbuzz.cmake)
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

# cpython — a libpython-free SUBSET of the CPython 3.14 parser (Python source
# -> AST). Sibling of src/yetty like src/libvterm / src/tinyemu. Builds the
# yetty_cpython static lib + the py-parse tool, linking libc only. Its runtime
# headers shadow CPython's <Python.h>, so they are kept PRIVATE to the lib.
if(YETTY_ENABLE_FEATURE_CPYTHON)
    add_subdirectory(${YETTY_ROOT}/src/cpython ${CMAKE_BINARY_DIR}/src/cpython)
endif()

# Shared desktop app-bootstrap sources. A standalone GLFW app (a tool/demo that
# subclasses yapp:app) compiles these straight into its executable: the shared
# ymain entry, the glfw_platform yclass (platform + window/clipboard subclasses).
# Each such app appends ${YETTY_APP_BOOTSTRAP_SOURCES} to its own source list
# plus its own annotated app.c + generated app.gen.c. (The main yetty exec keeps
# its own inline list in platform/<plat>/cmake.cmake, which also pulls in the
# yetty:app sources.) Apps needing custom CLI handling provide their own main()
# and OMIT ymain/glfw.c — they run the platform sequence directly.
#
# Defined BEFORE the src/yetty subdir pass below: src/yetty/yrich consumes this
# in its yetty_yrich_app OBJECT library, so the variable must already be set.
set(YETTY_APP_BOOTSTRAP_SOURCES
    ${YETTY_ROOT}/src/yetty/yplatform/yplatform/platform.c
    ${YETTY_ROOT}/src/yetty/yplatform/yplatform/glfw.c
    ${YETTY_ROOT}/src/yetty/yplatform/ywindow/window.c
    ${YETTY_ROOT}/src/yetty/yplatform/ywindow/glfw.c
    ${YETTY_ROOT}/src/yetty/yplatform/yclipboard/glfw.c
)
if(APPLE)
    list(APPEND YETTY_APP_BOOTSTRAP_SOURCES
        ${YETTY_ROOT}/src/yetty/yplatform/ywindow/macos.m)
endif()
# The shared GLFW entry (provides int main()). Class 1 apps add this; Class 2
# apps (custom CLI) provide their own main and leave this out.
set(YETTY_APP_ENTRY_GLFW
    ${YETTY_ROOT}/src/yetty/yplatform/ymain/glfw.c
)

# Add src/yetty (populates YETTY_SOURCES, YETTY_CORE_SOURCES, builds feature libraries)
add_subdirectory(${YETTY_ROOT}/src/yetty ${CMAKE_BINARY_DIR}/src/yetty)

# Public API facades (yetty_api_*). Declared after src/yetty because they depend
# on the implementation modules (e.g. yetty_api_yplot -> yetty_yplot_core).
add_subdirectory(${YETTY_ROOT}/src/api/yplot ${CMAKE_BINARY_DIR}/src/api/yplot)

# When building libyetty_ffi.so, only that shared object and its dependency
# closure must be position-independent. Scope PIC to exactly those targets here
# (they all exist by now) so the rest of the application build stays non-PIC —
# building one small shared library never turns the whole project into a PIC
# build. Keep this list in sync with src/yetty/yffi/CMakeLists.txt.
if(YETTY_BUILD_FFI_SHARED)
    foreach(ffi_pic_target
            yetty_api_yplot yetty_yplot_core yetty_ydraw_list yetty_yface
            yetty_ysdf yetty_yfsvm_core yetty_yexpr yetty_yclass yetty_ywire
            yetty_ycore yetty_ydrawlist2 yetty_api_ydrawlist2 yetty_ysdf2
            yetty_api_ysdf2 yetty_ycomplex2 yetty_api_ycomplex2
            yetty_ygui2 yetty_api_ygui2
            yetty_yimage_core yetty_ymesh_core yetty_yshadertoy_core
            yetty_yvideo_core)
        if(TARGET ${ffi_pic_target})
            set_target_properties(${ffi_pic_target} PROPERTIES
                POSITION_INDEPENDENT_CODE ON)
        endif()
    endforeach()
endif()

# getopt + the rest of the platform layer live in yetty_yplatform_core
# (declared in src/yetty/yplatform/CMakeLists.txt). Tools link that lib
# directly; the main yetty exec links it via platform/<plat>/cmake.cmake.

# Unit tests (opt-in). The single test/CMakeLists.txt owns the helper and the
# subdirectory list; see test/cmake/YettyTest.cmake.
if(YETTY_ENABLE_FEATURE_TESTS)
    enable_testing()
    add_subdirectory(${YETTY_ROOT}/test ${CMAKE_BINARY_DIR}/test)
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

if(YETTY_ENABLE_LIB_MIMALLOC)
    list(APPEND YETTY_DEFINITIONS YETTY_HAS_MIMALLOC=1)
endif()

# YETTY_HAS_YMGUI gates yframework's ymgui figure-kind registration. Off on
# webasm (imgui prebuilt unavailable) — see webasm/variables.cmake.
if(YETTY_ENABLE_FEATURE_YMGUI)
    list(APPEND YETTY_DEFINITIONS YETTY_HAS_YMGUI=1)
endif()

# Common libraries to link (only include what's enabled)
set(YETTY_LIBS "")

# Third-party library link platforms
if(YETTY_ENABLE_LIB_MIMALLOC)
    # First in the list: the archive defines malloc/free/realloc, and the
    # linker must see those definitions before it falls through to libc so
    # every allocation in the process routes through mimalloc.
    list(APPEND YETTY_LIBS mimalloc::mimalloc)
endif()
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
    # The SDF handler lib plus the complex-drawable factories terminal.c
    # registers at startup (yplot / yimage / yshadertoy prim / ymesh).
    # Nothing else on YETTY_LIBS links these, so the app names them
    # directly. The gates mirror where each target is created.
    list(APPEND YETTY_LIBS yetty_ysdf yetty_yshadertoy_prim)
    if(YETTY_ENABLE_FEATURE_YIMAGE AND YETTY_ENABLE_LIB_WEBGPU)
        list(APPEND YETTY_LIBS yetty_yimage)
    endif()
    if(YETTY_ENABLE_FEATURE_YPLOT AND YETTY_ENABLE_LIB_WEBGPU)
        list(APPEND YETTY_LIBS yetty_yplot)
    endif()
    if(YETTY_ENABLE_FEATURE_YMESH AND YETTY_ENABLE_LIB_WEBGPU)
        list(APPEND YETTY_LIBS yetty_ymesh)
    endif()
endif()
if(YETTY_ENABLE_FEATURE_YDIAGRAM)
    list(APPEND YETTY_LIBS yetty_ydiagram)
endif()
if(YETTY_ENABLE_FEATURE_YCHART)
    list(APPEND YETTY_LIBS yetty_ychart)
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
# yetty_stage_embed_assets()
#
# Configure-time staging of the embeddable asset trees under the build dir.
# Copies shaders / fonts / MSDF CDBs, the default config and the shared
# RISC-V runtime into one staging directory each, ready for
# incbin_add_directory(). Idempotent — safe to call from several targets.
# Sets in the caller's scope:
#   YETTY_EMBED_DATA_DIR    shaders/, fonts/, msdf-fonts/
#   YETTY_EMBED_CONFIG_DIR  config.yaml, defaults.yaml, temu/
#   YETTY_EMBED_YEMU_DIR    kernel / firmware / rootfs images, or "" when
#                           neither TinyEMU nor QEMU is enabled
# yetty_embed_assets() below stages + embeds everything into one target; the
# installer variants (tools/yinstall) stage once and embed per-variant subsets.
#-----------------------------------------------------------------------------
function(yetty_stage_embed_assets)
    # Collect ALL data assets into one build directory
    set(EMBED_DATA_DIR "${CMAKE_BINARY_DIR}/embed-data")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}/shaders")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}/fonts")
    file(MAKE_DIRECTORY "${EMBED_DATA_DIR}/msdf-fonts")

    # Collect shaders from module locations. Scene figures (chrome, rich
    # content) render through yscene.wgsl; the vterm figure's text grid uses
    # grid-text.wgsl below.
    file(COPY "${YETTY_ROOT}/src/yetty/yscene/yscene.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    # Pointwise post-color effects library — attached at runtime as a child
    # resource set of the scene layer (and the vterm text shader); provides the
    # fx_post_apply() the layer shaders call. See src/yetty/yshaders/effects-lib.wgsl.
    file(COPY "${YETTY_ROOT}/src/yetty/yshaders/effects-lib.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    # grid-text.wgsl — the vterm figure's text-grid shader (paints yvterm's
    # grid.c cell model). vterm.c loads it from <paths/shaders> at GPU init
    # and prepends effects-lib.wgsl. Mandatory: without it packaged here the
    # text pipeline fails hard and the terminal renders no text.
    file(COPY "${YETTY_ROOT}/src/yetty/yvterm/grid-text.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/ymgui/ymgui-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    file(COPY "${YETTY_ROOT}/src/yetty/yterminal/background-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    # Generated SDF dispatcher + sdf_* functions — attached at runtime as a
    # child resource set of ydraw-layer; see src/yetty/ysdf/gen-sdf-code.py.
    file(COPY "${YETTY_ROOT}/src/yetty/ysdf/ysdf.gen.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
    # grid-sdf-layer.wgsl — the yvterm "vterm-as-figure" SDF pass that
    # renders per-line rich records (ycat / ybrowser --osc envelopes).
    # grid-sdf-layer.c loads it from <paths/shaders>; without it packaged here
    # sdf_layer_create fails ("load_layer_shader") and every inline rich
    # envelope is silently dropped on desktop. The webasm staging already
    # ships it — this is the desktop counterpart.
    file(COPY "${YETTY_ROOT}/src/yetty/yvterm/grid-sdf-layer.wgsl" DESTINATION "${EMBED_DATA_DIR}/shaders")
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

    # Per-glyph procedural shaders (glyph-shaders/*.wgsl). The shader-glyph
    # layer template itself was retired with the yvterm consolidation; the
    # per-glyph sources still ship for any consumer that assembles them.
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

    # The file(COPY) calls above stage the embedded shaders at CONFIGURE
    # time only. Without a dependency on the source files, editing a shader
    # (e.g. grid-sdf-layer.wgsl) does NOT re-stage embed-data on a plain
    # rebuild, so incbin keeps embedding — and yinstall keeps shipping — the
    # stale copy while the runtime silently runs an old shader. Register
    # every staged shader source as a configure dependency so any edit
    # re-triggers configure, which re-runs the copies and re-embeds. The
    # glyph glob is already CONFIGURE-time; add its results too.
    set(_YETTY_EMBED_SHADER_SOURCES
        "${YETTY_ROOT}/src/yetty/yscene/yscene.wgsl"
        "${YETTY_ROOT}/src/yetty/yshaders/effects-lib.wgsl"
        "${YETTY_ROOT}/src/yetty/yvterm/grid-text.wgsl"
        "${YETTY_ROOT}/src/yetty/ymgui/ymgui-layer.wgsl"
        "${YETTY_ROOT}/src/yetty/yterminal/background-layer.wgsl"
        "${YETTY_ROOT}/src/yetty/ysdf/ysdf.gen.wgsl"
        "${YETTY_ROOT}/src/yetty/yvterm/grid-sdf-layer.wgsl"
        "${YETTY_ROOT}/src/yetty/yrender/blend.wgsl"
        "${YETTY_ROOT}/src/yetty/yfont/ms-msdf-font.wgsl"
        "${YETTY_ROOT}/src/yetty/yfont/msdf-font.wgsl"
        "${YETTY_ROOT}/src/yetty/yfont/ms-raster-font.wgsl"
        "${YETTY_ROOT}/src/yetty/yfont/raster-font.wgsl"
        "${YETTY_ROOT}/src/yetty/ymsdf-wgsl/shaders/msdf_gen.wgsl"
        ${GLYPH_SHADER_FILES})
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS ${_YETTY_EMBED_SHADER_SOURCES})

    # Copy fonts (.ttf text faces + .otf — e.g. Emmentaler, the music font ymusic
    # renders with — so they extract to the runtime fonts dir alongside DejaVu).
    file(GLOB FONT_FILES
        "${YETTY_ROOT}/assets/fonts/*.ttf"
        "${YETTY_ROOT}/assets/fonts/*.otf")
    foreach(FONT_FILE ${FONT_FILES})
        file(COPY "${FONT_FILE}" DESTINATION "${EMBED_DATA_DIR}/fonts")
    endforeach()

    # World-coverage Noto set (from the fetched lib-fonts noarch tarball):
    # script faces + CJK + Color Emoji, consumed by the range routing.
    if(YETTY_ENABLE_NOTO_FONTS AND YETTY_3RDPARTY_fonts_DIR)
        file(GLOB NOTO_FONT_FILES "${YETTY_3RDPARTY_fonts_DIR}/*.ttf")
        foreach(NOTO_FONT_FILE ${NOTO_FONT_FILES})
            file(COPY "${NOTO_FONT_FILE}" DESTINATION "${EMBED_DATA_DIR}/fonts")
        endforeach()
    endif()

    # Copy msdf-fonts (shipped pre-brotli'd as *.cdb.br; incbin's
    # already-compressed path embeds the bytes as-is and strips .br from
    # the in-binary asset name). Source dir comes from yetty_3rdparty_fetch(fonts).
    file(GLOB MSDF_FILES "${YETTY_3RDPARTY_fonts_DIR}/*.cdb.br")
    foreach(MSDF_FILE ${MSDF_FILES})
        file(COPY "${MSDF_FILE}" DESTINATION "${EMBED_DATA_DIR}/msdf-fonts")
    endforeach()

    set(YETTY_EMBED_DATA_DIR "${EMBED_DATA_DIR}" PARENT_SCOPE)

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
    # The file(COPY) calls above run at configure time, so an edited config
    # yaml stays stale in embed-config/ until the next reconfigure — make
    # the configs configure dependencies so editing them triggers one.
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS
        "${DEFAULT_CONFIG_FILE}"
        "${YETTY_ROOT}/build-tools/yetty/platform/config-defaults.yaml")

    # Stage tinyemu cfgs under yconfig/temu/ so extract-assets drops them at
    # <config_dir>/temu/. tinyemu-pty.c reads <config_dir>/temu/yetty-temu-extended.cfg
    # for --temu (the file is the source of truth — no in-process auto-gen).
    file(MAKE_DIRECTORY "${EMBED_CONFIG_DIR}/temu")
    file(COPY
        "${YETTY_ROOT}/assets/yemu/temu/yetty-temu.cfg"
        "${YETTY_ROOT}/assets/yemu/temu/yetty-temu-extended.cfg"
        DESTINATION "${EMBED_CONFIG_DIR}/temu")

    set(YETTY_EMBED_CONFIG_DIR "${EMBED_CONFIG_DIR}" PARENT_SCOPE)

    # Embed shared RISC-V runtime (kernel, opensbi, rootfs) under yemu/ prefix.
    # Used by both --temu (TinyEMU, in-process) and --qemu (external QEMU via
    # telnet). After the per-asset 3rdparty split, files come from three
    # separate fetched dirs (linux, opensbi, alpine-disk). Producer ships
    # them brotli-q11 (*.br); 3rdparty-fetch auto-decompresses raw copies
    # side-by-side for runtime path mode (see tinyemu_copy_runtime_to_bundle).
    # incbin's already-compressed path embeds the .br bytes as-is and strips
    # the .br suffix from the in-binary asset name.
    set(YETTY_EMBED_YEMU_DIR "" PARENT_SCOPE)
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

        set(YETTY_EMBED_YEMU_DIR "${EMBED_YEMU_DIR}" PARENT_SCOPE)
    endif()
endfunction()

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

    yetty_stage_embed_assets()

    # Embed ALL data assets (brotli compressed)
    incbin_add_directory(${TARGET} "data" "${YETTY_EMBED_DATA_DIR}" "*" TRUE)

    # Embed config (brotli-compressed; the extractor inflates on the way out)
    incbin_add_directory(${TARGET} "yconfig" "${YETTY_EMBED_CONFIG_DIR}" "*" TRUE)

    # Shared RISC-V runtime under yemu/ — files are shipped pre-brotli'd, so
    # embed as-is (incbin strips the .br from the in-binary asset name).
    if(YETTY_EMBED_YEMU_DIR)
        incbin_add_directory(${TARGET} "yemu" "${YETTY_EMBED_YEMU_DIR}" "*" FALSE)
    endif()

    # Embed QEMU binary if enabled (fetched by yetty_3rdparty_fetch(qemu))
    if(YETTY_ENABLE_LIB_QEMU_BINARY)
        qemu_embed_runtime(${TARGET})
    endif()

    # Make manifest headers available
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
