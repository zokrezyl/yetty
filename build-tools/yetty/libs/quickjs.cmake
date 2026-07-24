# quickjs — MIT-licensed JavaScript engine (quickjs-ng). Built IN-TREE
# from the vendored source under src/quickjs/, the same way vterm.cmake
# builds src/libvterm-0.3.3 and libcss.cmake builds src/libcss/. The
# engine is vendored (not fetched) so it can be edited directly — engine
# instrumentation and the bytecode JIT land as source commits on top of
# the pristine drop (provenance: src/quickjs/UPSTREAM).
#
# Exposes `qjs` (STATIC) — the same target name the old prebuilt import
# used, so consumers (src/yetty/ybrowser) keep linking `qjs` and gating
# on `if(TARGET qjs)` unchanged. The public include directory is the
# vendored source root (quickjs.h lives at the top level upstream).
#
# Compile shape matches what the retired prebuilt was built with: static
# library only, PIC, no CLI, no quickjs-libc, parser enabled. The library
# is exactly four translation units (upstream CMakeLists `qjs_sources`);
# unicode/atom tables ship pre-generated as headers, so there are no host
# generators and no configure-time codegen.

include_guard(GLOBAL)

if(TARGET qjs)
    return()
endif()

set(QJS_DIR ${YETTY_ROOT}/src/quickjs)
if(NOT EXISTS ${QJS_DIR}/quickjs.c)
    message(STATUS "quickjs: vendored source not found at ${QJS_DIR} — skipping (JavaScript disabled)")
    return()
endif()

add_library(qjs STATIC
    ${QJS_DIR}/dtoa.c
    ${QJS_DIR}/libregexp.c
    ${QJS_DIR}/libunicode.c
    ${QJS_DIR}/quickjs.c
)
target_include_directories(qjs PUBLIC ${QJS_DIR})
set_target_properties(qjs PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Upstream's own define set for the library target.
target_compile_definitions(qjs PRIVATE _GNU_SOURCE)
if(WIN32)
    target_compile_definitions(qjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()

# Stage 0 JIT profiler (see quickjs-jit.h): compiled in on the Linux
# measurement target, runtime-off until JS_ProfileStart. Other
# platforms keep the API as stubs.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(qjs PRIVATE QJS_ENABLE_PROFILE)
    target_link_libraries(qjs PUBLIC rt)
endif()

# Baseline bytecode JIT (SLJIT backend; design: src/quickjs/quickjs-jit.h,
# tmp/qjs-sljit-jit.md). Compiled in only on the validated Linux x86-64
# development target and default-OFF at runtime (JS_JITSetMode). Every
# other platform builds interpreter-only with the JIT-API stubs
# (JS_JITAvailable() == 0), so hosts need no conditional code.
#
# Per-platform enablement checklist (extend the guard below only after
# the target's item is validated — the design's platform table):
#   Linux/Android AArch64 : SLJIT ARM64 backend + cache-flush + W^X audit
#   Windows x86-64        : MSVC ABI, SEH/unwind, exec-mem policy
#   macOS x86-64/ARM64    : W^X + hardened-runtime (MAP_JIT) validation
#   Linux RISC-V 64       : SLJIT RISC-V backend validation
#   WebAssembly           : interpreter only (no runtime executable memory)
#   iOS/tvOS              : interpreter only unless JIT entitlement permits
# Opt out entirely with -DQJS_ENABLE_JIT=OFF.
option(QJS_ENABLE_JIT "quickjs baseline JIT (SLJIT), Linux x86-64" ON)
set(QJS_SLJIT_DIR ${QJS_DIR}/sljit/sljit_src)
if(QJS_ENABLE_JIT
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64"
   AND EXISTS ${QJS_SLJIT_DIR}/sljitLir.c)
    target_sources(qjs PRIVATE ${QJS_SLJIT_DIR}/sljitLir.c)
    target_include_directories(qjs PRIVATE ${QJS_SLJIT_DIR})
    # W^X: the dual-mapping protected allocator keeps a writable and an
    # executable view of the code as separate mappings — generated code
    # is never simultaneously writable and executable (design: Native-
    # code memory and security).
    target_compile_definitions(qjs PRIVATE QJS_ENABLE_JIT SLJIT_CONFIG_AUTO=1
                               SLJIT_PROT_EXECUTABLE_ALLOCATOR=1)
    message(STATUS "quickjs: baseline JIT enabled (SLJIT, linux-x86_64, W^X)")
else()
    message(STATUS "quickjs: baseline JIT disabled (interpreter only)")
endif()

# Third-party source: not held to yetty warning levels.
if(MSVC)
    target_compile_options(qjs PRIVATE /w)
else()
    target_compile_options(qjs PRIVATE -w)
endif()

# quickjs-ng's library links libm + (on POSIX) dl + pthread.
# find_package(Threads) resolves per platform (pthread on linux/macos,
# nothing on android where it's in libc) instead of a bare -lpthread the
# NDK has no archive for.
if(NOT WIN32)
    find_package(Threads QUIET)
    target_link_libraries(qjs PUBLIC m ${CMAKE_DL_LIBS})
    if(TARGET Threads::Threads)
        target_link_libraries(qjs PUBLIC Threads::Threads)
    endif()
endif()

message(STATUS "quickjs: vendored in-tree (src/quickjs, see UPSTREAM)")
