#=============================================================================
# Yetty Build Variables — Platform-Neutral Defaults
#
# Include this from platform/<name>/variables.cmake, then add overrides.
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
option(YETTY_ENABLE_LIB_CGLTF       "cgltf — glTF 2.0 parser"               ON)
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

# Audio
option(YETTY_ENABLE_LIB_MINIAUDIO   "miniaudio — cross-platform audio I/O"  ON)
option(YETTY_ENABLE_LIB_LIBOPUS     "libopus — Opus audio codec"            ON)

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
option(YETTY_ENABLE_LIB_NETSURF     "netsurf-3.11 — NetSurf core+libs"      ON)
option(YETTY_ENABLE_LIB_LEXBOR      "lexbor — Apache-2.0 HTML/CSS engine"   ON)
option(YETTY_ENABLE_LIB_QUICKJS     "quickjs-ng — MIT JavaScript engine"    ON)

# Virtual machine (--virtual flag: run shell in RISC-V Linux VM)
option(YETTY_ENABLE_LIB_TINYEMU     "tinyemu — RISC-V emulator for --virtual" ON)
option(YETTY_ENABLE_LIB_QEMU        "qemu — QEMU launcher lib (yetty_qemu)"   ON)
option(YETTY_ENABLE_LIB_QEMU_BINARY "qemu — fetch + embed qemu-system-riscv64" ON)

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
option(YETTY_ENABLE_FEATURE_YRDAWN     "yrdawn — WebGPU-over-OSC bridge"        ON)

# Drawing / rendering
option(YETTY_ENABLE_FEATURE_YDRAW_ZOO "ydraw-zoo — ydraw demo shapes"        OFF)
option(YETTY_ENABLE_FEATURE_YDRAW_MAZE "ydraw-maze — ydraw maze demo"        OFF)
option(YETTY_ENABLE_FEATURE_YDRAW    "ydraw — drawing library (canvas, SDF, complex prims)" ON)
option(YETTY_ENABLE_FEATURE_YRICH     "yrich — rich text"                    ON)
option(YETTY_ENABLE_FEATURE_YDIAGRAM   "diagram — diagram rendering"          ON)
option(YETTY_ENABLE_FEATURE_YPLOT     "yplot — plotting"                     ON)
option(YETTY_ENABLE_FEATURE_YFLAME    "yflame — flame graphs"                ON)
option(YETTY_ENABLE_FEATURE_YMUSIC    "ymusic — music notation rendering"    ON)
option(YETTY_ENABLE_FEATURE_YCIRCUIT  "ycircuit — circuit schematic rendering" ON)

# Cards
option(YETTY_ENABLE_FEATURE_CARDS     "cards — card plugin system"           OFF)
option(YETTY_ENABLE_FEATURE_YGRID     "ygrid — grid card"                    OFF)
option(YETTY_ENABLE_FEATURE_YTHORVG   "ythorvg — thorvg card"               ON)

# Video / media
option(YETTY_ENABLE_FEATURE_YVCODEC   "yvcodec — H.264 video codec support"  OFF)
option(YETTY_ENABLE_FEATURE_YACODEC   "yacodec — audio codec support (Opus)" ON)
option(YETTY_ENABLE_FEATURE_YAUDIO    "yaudio — offline audio analysis (WAV, intervals)" ON)

# Network / connectivity
option(YETTY_ENABLE_FEATURE_YVNC       "vnc — VNC client/server"              ON)
option(YETTY_ENABLE_FEATURE_YDVNC      "ydvnc — desktop VNC client (RFB)"     ON)
option(YETTY_ENABLE_FEATURE_TELNET    "telnet — telnet connectivity"         ON)
option(YETTY_ENABLE_FEATURE_SSH       "ssh — SSH connectivity"               ON)
option(YETTY_ENABLE_FEATURE_YCTL      "yctl — msgpack-RPC interface"         ON)

# Misc
option(YETTY_ENABLE_FEATURE_YCDB      "ycdb — constant database wrapper"     ON)
option(YETTY_ENABLE_FEATURE_YEXPR     "yexpr — expression parser"            ON)
option(YETTY_ENABLE_FEATURE_YFSVM     "yfsvm — fragment shader VM"           ON)
option(YETTY_ENABLE_FEATURE_YIMAGE    "yimage — image composite"     ON)
option(YETTY_ENABLE_FEATURE_YMAZE     "ymaze — animated maze ydraw demo"    ON)
option(YETTY_ENABLE_FEATURE_YZOO      "yzoo — animated control-point zoo demo" ON)
option(YETTY_ENABLE_FEATURE_YJUNGLE   "yjungle — incremental scene-canvas test" ON)
option(YETTY_ENABLE_FEATURE_YMESH     "ymesh — 3D mesh composite"    ON)
option(YETTY_ENABLE_FEATURE_YVIDEO    "yvideo — video composite"     ON)
option(YETTY_ENABLE_FEATURE_YMSDF_GEN "ymsdf-gen — MSDF glyph generator"    ON)
option(YETTY_ENABLE_FEATURE_YMSDF_WGSL "ymsdf-wgsl — GPU MSDF glyph generator" ON)
option(YETTY_ENABLE_FEATURE_YCAT      "ycat — MIME-dispatched cat for cards"  ON)
option(YETTY_ENABLE_FEATURE_YPDF      "ypdf — PDF to ydraw buffer"          ON)
option(YETTY_ENABLE_FEATURE_YMARKDOWN "ymarkdown — Markdown to ydraw buffer" ON)
option(YETTY_ENABLE_FEATURE_YSVG      "ysvg — SVG (Tiny 1.2) to ydraw buffer" ON)
option(YETTY_ENABLE_FEATURE_YLOTTIE   "ylottie — Lottie (single frame) to ydraw buffer" ON)
option(YETTY_ENABLE_FEATURE_YMAP      "ymap — slippy-map model + tile engine" ON)

# Build pipeline
option(YETTY_ENABLE_FEATURE_ASSETS    "assets — runtime asset copying"       ON)
option(YETTY_ENABLE_FEATURE_CDB_GEN   "cdb-gen — CDB font generation"       ON)
# Default OFF — platform/linux/variables.cmake enables for Linux desktop builds.
option(YETTY_ENABLE_FEATURE_TESTS     "tests — unit tests"                   OFF)
option(YETTY_ENABLE_FEATURE_DEMO      "demo — demo programs"                 ON)

# Tools (each tool has its own option)
option(YETTY_ENABLE_TOOL_GPU_INFO        "gpu-info tool"                     OFF)
option(YETTY_ENABLE_TOOL_CARD_RUNNER     "card-runner tool"                  OFF)
option(YETTY_ENABLE_TOOL_YDRAW_GENERATOR "ydraw-generator tool"              OFF)
option(YETTY_ENABLE_TOOL_YDRAW_BENCH    "ydraw-bench tool"                 ON)
option(YETTY_ENABLE_TOOL_YCAT            "ycat tool"                         ON)
option(YETTY_ENABLE_TOOL_YLESS           "yless pager tool"                  ON)

# FFI shared library (libyetty_ffi.so). OFF for the normal static build so the
# fast non-PIC application archives stay uncompromised; turned ON only in the
# dedicated PIC "double build" tree (make build-desktop-ffi-release), which
# also sets CMAKE_POSITION_INDEPENDENT_CODE=ON so the linked module archives
# are position-independent. The bindings under bindings/<lang>/ dlopen it.
option(YETTY_BUILD_FFI_SHARED            "Build libyetty_ffi.so for language bindings (PIC)" OFF)
option(YETTY_ENABLE_TOOL_YECHO           "yecho tool"                        ON)
option(YETTY_ENABLE_TOOL_GEN_ERROR       "gen-error tool (malformed OSC test) " ON)
option(YETTY_ENABLE_TOOL_YRDAWN_DEMO       "yrdawn-demo tool (bridge smoke test)" ON)
option(YETTY_ENABLE_TOOL_YPLOT           "yplot tool"                        ON)
option(YETTY_ENABLE_TOOL_YMAP            "ymap tool (slippy-map frontend)"   ON)
option(YETTY_ENABLE_TOOL_YTOP            "ytop tool (CPU monitor)"           ON)
option(YETTY_ENABLE_TOOL_CCC             "ccc tool (claude CLI loop)"        ON)
option(YETTY_ENABLE_TOOL_YAI             "yai tool (AI engine loop)"         ON)
option(YETTY_ENABLE_TOOL_YGREETER        "ygreeter first-contact tool"       ON)
option(YETTY_ENABLE_TOOL_YINSTALL        "yinstall installer"                ON)

# Whether the desktop app binaries (yetty, ygreeter, …) bake their assets
# (shaders / fonts / config / VM runtime / greeter logos) into themselves.
# OFF by default: on desktop the assets — and the binaries themselves — ship
# via the yinstall installer, which carries every tool's assets once and
# deploys them, so a baked-in copy would only bloat both the binary and the
# installer that embeds it. Mobile (ios/android/tvos) and webasm ignore this
# — they have no installer and always bundle their assets; the riscv VM
# rootfs sets it ON so its tools are self-contained. Set ON to build
# standalone, self-extracting desktop binaries.
option(YETTY_EMBED_ASSETS_IN_BINARIES "Bake assets into the desktop app binaries" OFF)
option(YETTY_ENABLE_TOOL_YMESH           "ymesh tool (.glb → OSC)"           ON)
option(YETTY_ENABLE_TOOL_YVIDEO          "yvideo tool (.h264 → OSC stream)"  ON)
option(YETTY_ENABLE_TOOL_YAUDIO_INTERVALS "yaudio-intervals tool (WAV energy interval finder)" ON)
option(YETTY_ENABLE_TOOL_YAUDIO    "yaudio analyzer GUI" ON)
option(YETTY_ENABLE_TOOL_YCOMPOSITOR "ycompositor standalone test harness" ON)
option(YETTY_ENABLE_TOOL_YMAZE           "ymaze tool (animated maze → OSC)"  ON)
option(YETTY_ENABLE_TOOL_YZOO            "yzoo tool (control-point zoo → OSC)" ON)
option(YETTY_ENABLE_TOOL_YJUNGLE         "yjungle tool (scene-canvas test → OSC)" ON)
option(YETTY_ENABLE_TOOL_YDRAW_MAZE      "ydraw-maze tool"                   OFF)
option(YETTY_ENABLE_TOOL_YMUX            "ymux tool"                         OFF)
option(YETTY_ENABLE_TOOL_YFLAME          "yflame tool"                       ON)
option(YETTY_ENABLE_TOOL_PDF2YDRAW       "pdf2ydraw tool"                    OFF)
option(YETTY_ENABLE_TOOL_HTML2YDRAW      "html2ydraw tool"                   OFF)
option(YETTY_ENABLE_TOOL_YHTML_MACHINE   "yhtml-machine tool"                OFF)
option(YETTY_ENABLE_TOOL_YDOC            "ydoc tool"                         ON)
option(YETTY_ENABLE_TOOL_YSHEET          "ysheet tool"                       ON)
option(YETTY_ENABLE_TOOL_YSLIDE          "yslide tool"                       ON)
option(YETTY_ENABLE_TOOL_QA              "qa static analysis tools"          ON)
option(YETTY_ENABLE_TOOL_YTHORVG         "yetty-ythorvg CLI (SVG/Lottie -> OSC)" ON)
option(YETTY_ENABLE_TOOL_YLOTTIE         "yetty-ylottie CLI (Lottie animation -> OSC)" ON)
option(YETTY_ENABLE_TOOL_DECODE_YDRAW    "decode-ydraw diagnostic tool"     ON)
option(YETTY_ENABLE_TOOL_YDIAGRAM         "ydiagram CLI (Mermaid → ydraw OSC)" ON)
option(YETTY_ENABLE_FEATURE_YNETSURF     "ynetsurf — NetSurf frontend lib"   ON)
option(YETTY_ENABLE_TOOL_YNETSURF        "ynetsurf browser tool"             ON)
option(YETTY_ENABLE_FEATURE_YLEXBOR      "ylexbor — lexbor-based HTML lib"   ON)
option(YETTY_ENABLE_TOOL_YLEXBOR    "ylexbor-demo CLI tool"             ON)
option(YETTY_ENABLE_FEATURE_YBROWSER     "ybrowser — lexbor HTML + libcss cascade" ON)
option(YETTY_ENABLE_TOOL_YBROWSER        "ybrowser CLI tool"                 ON)

# Desktop-only features (default OFF; linux/macos/windows cmake may enable)
option(YETTY_ENABLE_FEATURE_GPU       "gpu — GPU tools (desktop)"            OFF)
option(YETTY_ENABLE_FEATURE_CLIENT    "client — client tools (desktop)"      OFF)
option(YETTY_ENABLE_FEATURE_YTOP      "ytop — system monitor (desktop)"      OFF)
