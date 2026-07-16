include(${CMAKE_CURRENT_LIST_DIR}/../variables-defaults.cmake)

# No native windowing; coroutine uses Asyncify instead of stack-switching.
set(YETTY_ENABLE_LIB_GLFW  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_LIBCO OFF CACHE BOOL "" FORCE)

# emscripten owns the wasm malloc (dlmalloc/emmalloc baked into its libc);
# a statically linked mimalloc cannot interpose it there.
set(YETTY_ENABLE_LIB_MIMALLOC OFF CACHE BOOL "" FORCE)

# WebAssembly uses in-process TinyEMU — no external QEMU binary.
set(YETTY_ENABLE_LIB_QEMU        OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_QEMU_BINARY OFF CACHE BOOL "" FORCE)

# C++ prebuilt libs: tarballs not compatible with this emcc version
# (caller/callee type-signature mismatches wasm-emscripten-finalize rejects).
set(YETTY_ENABLE_LIB_THORVG   OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_LIBSSH2  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_OPENH264 OFF CACHE BOOL "" FORCE)
# fdk-aac is CPM-built from source; only mp4 recording uses it, and
# recording is off on webasm anyway (no OpenH264). Skip the build cost.
set(YETTY_ENABLE_LIB_FDK_AAC  OFF CACHE BOOL "" FORCE)

# pdfio, tree-sitter, imgui: webasm prebuilts not yet published.
set(YETTY_ENABLE_LIB_PDFIO      OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_TREESITTER OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YPDF   OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YCAT   OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YMGUI  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YCAT      OFF CACHE BOOL "" FORCE)

# msdfgen + ymsdf-gen must stay ON: yetty_ydraw hard-depends on yetty_ymsdf_gen.
set(YETTY_ENABLE_LIB_MSDFGEN       ON CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YMSDF_GEN ON CACHE BOOL "" FORCE)

set(YETTY_ENABLE_FEATURE_YTHORVG OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_SSH     OFF CACHE BOOL "" FORCE)

# ylexbor/ybrowser: lexbor uses POSIX file I/O and threading not available on webasm.
set(YETTY_ENABLE_FEATURE_YLEXBOR  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YLEXBOR     OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YBROWSER OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YBROWSER    OFF CACHE BOOL "" FORCE)

# QA tools require host LLVM/Clang libs and hardcode Linux paths.
set(YETTY_ENABLE_TOOL_QA OFF CACHE BOOL "" FORCE)

# ygreeter / ytop drive their own libuv event loops — that path lives in
# the full `ygui` static library, which only builds
# when libuv is available. Webasm uses ygui_core (libuv-free) for in-app
# embedders; the standalone-interactive tools don't make sense in wasm.
set(YETTY_ENABLE_TOOL_YGREETER OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YINSTALL OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YTOP     OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YDU      OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YPERF    OFF CACHE BOOL "" FORCE)
