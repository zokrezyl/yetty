#=============================================================================
# Yetty Build Variables
#
# Central place to enable/disable all libraries and features.
# Include this BEFORE any library or feature includes.
#
# YETTY_ENABLE_LIB_*     — third-party library dependencies
# YETTY_ENABLE_FEATURE_* — internal source directories / modules
#=============================================================================

#-----------------------------------------------------------------------------
# Libraries (third-party dependencies)
#-----------------------------------------------------------------------------

# Core / always needed
option(YETTY_ENABLE_LIB_INCBIN      "incbin — binary embedding"             ON)
option(YETTY_ENABLE_LIB_ARGS        "args — command line parser"             ON)
option(YETTY_ENABLE_LIB_LZ4         "lz4 — compression"                      ON)
option(YETTY_ENABLE_LIB_LIBUV       "libuv — event loop"                    ON)
option(YETTY_ENABLE_LIB_LIBCO       "libco — stackful coroutines"           ON)
option(YETTY_ENABLE_LIB_GLM         "glm — math"                            OFF)
option(YETTY_ENABLE_LIB_STB         "stb — image loading"                   ON)
option(YETTY_ENABLE_LIB_YAML_CPP    "yaml-cpp — config parsing (C++)"       OFF)
option(YETTY_ENABLE_LIB_LIBYAML     "libyaml — config parsing (C)"          ON)
# spdlog + the upstream zokrezyl/ytrace C++ lib are unused — yetty's own
# pure-C implementation in src/yetty/ytrace.c (driven by include/yetty/ytrace.h)
# provides the trace/log macros. Keeping these OFF avoids pulling C++ into
# the build for nothing.
option(YETTY_ENABLE_LIB_SPDLOG      "spdlog — logging backend (UNUSED)"     OFF)
option(YETTY_ENABLE_LIB_YTRACE      "ytrace — external C++ tracing lib (UNUSED)" OFF)
option(YETTY_ENABLE_LIB_MSGPACK     "msgpack — serialization"               ON)
option(YETTY_ENABLE_LIB_WEBGPU      "webgpu/dawn — GPU backend"             ON)
option(YETTY_ENABLE_LIB_VTERM       "libvterm — terminal emulation"          ON)
option(YETTY_ENABLE_LIB_ZLIB        "zlib — compression"                    ON)
option(YETTY_ENABLE_LIB_LIBPNG      "libpng — PNG support"                  ON)
option(YETTY_ENABLE_LIB_FREETYPE    "freetype — font rendering"             ON)
option(YETTY_ENABLE_LIB_MSDFGEN     "msdfgen — MSDF font generation"        OFF)
option(YETTY_ENABLE_LIB_CDB         "cdb — constant database"               OFF)

# Media / codecs
option(YETTY_ENABLE_LIB_LIBJPEG_TURBO "libjpeg-turbo — JPEG support"       ON)
option(YETTY_ENABLE_LIB_DAV1D       "dav1d — AV1 decoder"                   OFF)
set(YETTY_ENABLE_LIB_OPENH264 ON CACHE BOOL "openh264 — H.264 codec" FORCE)
set(YETTY_ENABLE_LIB_MINIMP4  ON CACHE BOOL "minimp4 — MP4 container" FORCE)

# Misc
option(YETTY_ENABLE_LIB_TREESITTER  "tree-sitter — source code parsing"     ON)
option(YETTY_ENABLE_LIB_WASM3       "wasm3 — WASM interpreter"              OFF)
option(YETTY_ENABLE_LIB_LIBSSH2     "libssh2 — SSH protocol"                ON)
option(YETTY_ENABLE_LIB_THORVG      "thorvg — SVG/Lottie rendering"         ON)

# Platform-conditional (desktop only: linux, macos, windows)
option(YETTY_ENABLE_LIB_GLFW        "glfw — windowing (desktop only)"       ON)
option(YETTY_ENABLE_LIB_LIBMAGIC    "libmagic — file type detection"         ON)
option(YETTY_ENABLE_LIB_LIBCURL     "libcurl — HTTP(S) fetching"             ON)
option(YETTY_ENABLE_LIB_PDFIO       "pdfio — PDF parsing (C)"                ON)

# Virtual machine (--virtual flag: run shell in RISC-V Linux VM)
option(YETTY_ENABLE_LIB_TINYEMU     "tinyemu — RISC-V emulator for --virtual" ON)
option(YETTY_ENABLE_LIB_QEMU        "qemu — QEMU RISC-V emulator (via telnet)" ON)

#-----------------------------------------------------------------------------
# Features (internal source directories / modules)
# All OFF by default — enable as they are ported to the new architecture
#-----------------------------------------------------------------------------

# Core modules
option(YETTY_ENABLE_FEATURE_YCORE      "yetty_ycore — core utilities"         ON)
option(YETTY_ENABLE_FEATURE_YFACE      "yetty_yface — streaming OSC pipe"     ON)
option(YETTY_ENABLE_FEATURE_YFONT      "yetty_yfont — font subsystem"          ON)
option(YETTY_ENABLE_FEATURE_YSHADERS   "shaders — WGSL shader sources"        ON)
option(YETTY_ENABLE_FEATURE_MSDF_WGSL "ymsdf-wgsl — MSDF shader lib"         OFF)
option(YETTY_ENABLE_FEATURE_MSDF_GEN  "ymsdf-gen — MSDF font generator"       OFF)

# Terminal / display
option(YETTY_ENABLE_FEATURE_YECHO     "yecho — echo/display"                 ON)
option(YETTY_ENABLE_FEATURE_YGUI      "ygui — pure-C widget library"          ON)
option(YETTY_ENABLE_FEATURE_YMGUI     "ymgui — Dear ImGui ↔ yetty bridge"    ON)

# Drawing / rendering
option(YETTY_ENABLE_FEATURE_YDRAW     "ydraw — 2D vector drawing"            OFF)
option(YETTY_ENABLE_FEATURE_YDRAW_ZOO "ydraw-zoo — ydraw demo shapes"        OFF)
option(YETTY_ENABLE_FEATURE_YDRAW_MAZE "ydraw-maze — ydraw maze demo"        OFF)
option(YETTY_ENABLE_FEATURE_YPAINT    "ypaint — painting"                    ON)
option(YETTY_ENABLE_FEATURE_YRICH     "yrich — rich text"                    ON)
option(YETTY_ENABLE_FEATURE_DIAGRAM   "diagram — diagram rendering"          OFF)
option(YETTY_ENABLE_FEATURE_YPLOT     "yplot — plotting"                     ON)

# Cards
option(YETTY_ENABLE_FEATURE_CARDS     "cards — card plugin system"           OFF)
option(YETTY_ENABLE_FEATURE_YGRID     "ygrid — grid card"                    OFF)
option(YETTY_ENABLE_FEATURE_YTHORVG   "ythorvg — thorvg card"               ON)

# Video / media
option(YETTY_ENABLE_FEATURE_YVIDEO    "yvideo — video codec support"         OFF)

# Network / connectivity
option(YETTY_ENABLE_FEATURE_YVNC       "vnc — VNC client/server"              ON)
option(YETTY_ENABLE_FEATURE_TELNET    "telnet — telnet connectivity"         ON)
option(YETTY_ENABLE_FEATURE_SSH       "ssh — SSH connectivity"               ON)
option(YETTY_ENABLE_FEATURE_YRPC      "yrpc — msgpack-RPC interface"         ON)

# Misc
option(YETTY_ENABLE_FEATURE_YCDB      "ycdb — constant database wrapper"     ON)
option(YETTY_ENABLE_FEATURE_YEXPR     "yexpr — expression parser"            ON)
option(YETTY_ENABLE_FEATURE_YFSVM     "yfsvm — fragment shader VM"           ON)
option(YETTY_ENABLE_FEATURE_YIMAGE    "yimage — image complex primitive"     ON)
option(YETTY_ENABLE_FEATURE_YMSDF_GEN "ymsdf-gen — MSDF glyph generator"    ON)
option(YETTY_ENABLE_FEATURE_YCAT      "ycat — MIME-dispatched cat for cards"  ON)
option(YETTY_ENABLE_FEATURE_YPDF      "ypdf — PDF to ypaint buffer"          ON)
option(YETTY_ENABLE_FEATURE_YMARKDOWN "ymarkdown — Markdown to ypaint buffer" ON)

# Build pipeline
option(YETTY_ENABLE_FEATURE_ASSETS    "assets — runtime asset copying"       ON)
option(YETTY_ENABLE_FEATURE_CDB_GEN   "cdb-gen — CDB font generation"       ON)
# Tests on by default for Linux desktop builds.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN AND NOT YETTY_ANDROID AND NOT YETTY_IOS)
    option(YETTY_ENABLE_FEATURE_TESTS "tests — unit tests"                   ON)
else()
    option(YETTY_ENABLE_FEATURE_TESTS "tests — unit tests"                   OFF)
endif()
option(YETTY_ENABLE_FEATURE_DEMO      "demo — demo programs"                 ON)

# Tools (each tool has its own option)
option(YETTY_ENABLE_TOOL_GPU_INFO        "gpu-info tool"                     OFF)
option(YETTY_ENABLE_TOOL_CARD_RUNNER     "card-runner tool"                  OFF)
option(YETTY_ENABLE_TOOL_YDRAW_GENERATOR "ydraw-generator tool"              OFF)
option(YETTY_ENABLE_TOOL_YPAINT_BENCH    "ypaint-bench tool"                 ON)
option(YETTY_ENABLE_TOOL_YCAT            "ycat tool"                         ON)
option(YETTY_ENABLE_TOOL_YECHO           "yecho tool"                        ON)
option(YETTY_ENABLE_TOOL_YPLOT           "yplot tool"                        ON)
option(YETTY_ENABLE_TOOL_YDRAW_MAZE      "ydraw-maze tool"                   OFF)
option(YETTY_ENABLE_TOOL_YDRAW_ZOO       "ydraw-zoo tool"                    OFF)
option(YETTY_ENABLE_TOOL_YMUX            "ymux tool"                         OFF)
option(YETTY_ENABLE_TOOL_YFLAME          "yflame tool"                       OFF)
option(YETTY_ENABLE_TOOL_PDF2YDRAW       "pdf2ydraw tool"                    OFF)
option(YETTY_ENABLE_TOOL_HTML2YDRAW      "html2ydraw tool"                   OFF)
option(YETTY_ENABLE_TOOL_YHTML_MACHINE   "yhtml-machine tool"                OFF)
option(YETTY_ENABLE_TOOL_YBROWSER        "ybrowser tool"                     OFF)
option(YETTY_ENABLE_TOOL_YDOC            "ydoc tool"                         ON)
option(YETTY_ENABLE_TOOL_YSHEET          "ysheet tool"                       ON)
option(YETTY_ENABLE_TOOL_YSLIDE          "yslide tool"                       ON)
option(YETTY_ENABLE_TOOL_QA              "qa static analysis tools"          ON)
option(YETTY_ENABLE_TOOL_YTHORVG         "yetty-ythorvg CLI (SVG/Lottie -> OSC)" ON)
option(YETTY_ENABLE_TOOL_DECODE_YPAINT    "decode-ypaint diagnostic tool"     ON)

# Auto-disable QA tools for cross-compilation (requires host LLVM/Clang libs)
# Also disabled on macOS and Windows — qa-tools/custom/result-checker/CMakeLists.txt
# hardcodes Linux LLVM paths.
if(YETTY_ENABLE_TOOL_QA)
    if(YETTY_ANDROID OR YETTY_IOS OR EMSCRIPTEN OR CMAKE_CROSSCOMPILING OR APPLE OR WIN32)
        message(STATUS "Disabling QA tools (cross-compilation, macOS, or Windows)")
        set(YETTY_ENABLE_TOOL_QA OFF CACHE BOOL "" FORCE)
    endif()
endif()

# Auto-disable features whose source still uses POSIX networking / pipe APIs
# directly and hasn't been ported to the yetty/platform/* abstractions.
# Remove a feature from this list once its sources are ported.
if(WIN32)
    foreach(_f
        YETTY_ENABLE_FEATURE_SSH    # src/yetty/yssh: <netdb.h>, <pthread.h>, <poll.h>
        YETTY_ENABLE_LIB_LIBSSH2    # libssh2 itself supports Windows but yssh wrapper doesn't
        YETTY_ENABLE_FEATURE_YTHORVG # src/yetty/ythorvg/ythorvg.cpp: C99 compound literals (MSVC C++ rejects)
        YETTY_ENABLE_LIB_THORVG     # only consumer is FEATURE_YTHORVG
        YETTY_ENABLE_TOOL_YTHORVG   # tool depends on LIB_THORVG / FEATURE_YTHORVG
        YETTY_ENABLE_TOOL_YPAINT_BENCH # tools/ypaint-bench: passes -Wextra unconditionally (MSVC: D8021)
        YETTY_ENABLE_FEATURE_DEMO      # demo/ygui/CMakeLists.txt hardcodes shared/{thread,term}.c (POSIX)
        YETTY_ENABLE_TOOL_YDOC         # tools/ydoc + yrich-runner: poll.h, termios.h, unistd.h (POSIX TTY)
        YETTY_ENABLE_TOOL_YSHEET       # uses yrich-runner — same POSIX TTY deps
        YETTY_ENABLE_TOOL_YSLIDE       # uses yrich-runner — same POSIX TTY deps
        YETTY_ENABLE_LIB_LIBMAGIC      # upstream autotools-only; no MSVC port (libmagic.cmake's ExternalProject runs ./configure + make).
    )
        if(${_f})
            message(STATUS "Disabling ${_f} on Windows (sources not ported)")
            set(${_f} OFF CACHE BOOL "" FORCE)
        endif()
    endforeach()
endif()

# Auto-disable on macOS (host build): libssh2 1.11.1's CMakeLists.txt calls
# find_package(OpenSSL), which on macOS pulls Apple's clang default include
# search path that has /usr/local/include FIRST (Homebrew prefix) — ahead
# of our `-isystem .../3rdparty/openssl/include`. With Homebrew openssl@3
# installed (the common case on Intel macOS), libssh2's openssl.c picks up
# the 3.x opensslv.h, defines USE_OPENSSL_3, and emits EVP_MAC_*/OSSL_PARAM_*
# references that the pinned 1.1.1w libcrypto.a (which yetty deliberately
# uses to stay compatible with cpr / libcurl) does not provide → undefined
# symbols at link time. Disable the SSH feature + libssh2 lib on APPLE
# host builds until either (a) yetty migrates to openssl-new (4.x) or
# (b) libssh2's include order is forced to put 3rdparty/openssl ahead of
# the system /usr/local/include. iOS / tvOS go through different SDK
# search paths so this only hits the host-macOS build (APPLE && NOT
# YETTY_IOS).
if(APPLE AND NOT YETTY_IOS)
    foreach(_f
        YETTY_ENABLE_FEATURE_SSH    # src/yetty/yssh consumer of libssh2
        YETTY_ENABLE_LIB_LIBSSH2    # see comment above
    )
        if(${_f})
            message(STATUS "Disabling ${_f} on macOS (Homebrew openssl@3 in /usr/local/include shadows pinned 1.1.1w; see variables.cmake)")
            set(${_f} OFF CACHE BOOL "" FORCE)
        endif()
    endforeach()
endif()

# Auto-disable on macOS (host build): libmagic + ycat. libmagic is built from
# upstream file(1) sources via cmake/Libmagic.cmake which assumes Linux-style
# libtool/autotools paths and either picks up Homebrew's libmagic header
# (incompatible struct layouts vs the bundled magic.mgc) or fails its
# external-project configure step on macOS. ycat consumes libmagic for
# MIME-dispatching, so it goes off with it. Both are disabled here until the
# Libmagic.cmake recipe is reworked for the macOS environment.
if(APPLE AND NOT YETTY_IOS)
    foreach(_f
        YETTY_ENABLE_LIB_LIBMAGIC    # cmake/Libmagic.cmake fails / mis-finds on macOS
        YETTY_ENABLE_FEATURE_YCAT    # src/yetty/ycat — MIME dispatch needs libmagic
        YETTY_ENABLE_TOOL_YCAT       # tool links libyetty_ycat (gone with FEATURE_YCAT)
    )
        if(${_f})
            message(STATUS "Disabling ${_f} on macOS (libmagic build/detection unreliable on macOS host; see variables.cmake)")
            set(${_f} OFF CACHE BOOL "" FORCE)
        endif()
    endforeach()
endif()

# Auto-disable on iOS / tvOS: tools that depend on the YCAT library, which
# src/yetty/CMakeLists.txt deliberately skips for YETTY_IOS (the lib uses
# desktop-only POSIX bits — strings.h / unistd.h scattered throughout). The
# tool unconditionally links libyetty_ycat.a → undefined library at link time.
if(YETTY_IOS OR YETTY_TVOS)
    foreach(_f
        YETTY_ENABLE_TOOL_YCAT       # links yetty_ycat (lib not built on iOS/tvOS)
    )
        if(${_f})
            message(STATUS "Disabling ${_f} on iOS/tvOS (yetty_ycat library not built — see src/yetty/CMakeLists.txt)")
            set(${_f} OFF CACHE BOOL "" FORCE)
        endif()
    endforeach()
endif()

option(YETTY_ENABLE_FEATURE_JSLINUX   "jslinux — JSLinux integration"        ON)

# Desktop-only features
option(YETTY_ENABLE_FEATURE_GPU       "gpu — GPU tools (desktop)"            OFF)
option(YETTY_ENABLE_FEATURE_CLIENT    "client — client tools (desktop)"      OFF)
option(YETTY_ENABLE_FEATURE_YTOP      "ytop — system monitor (desktop)"      OFF)
