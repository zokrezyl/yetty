# ycat tool — pure C.
#
# Several sources live outside any exported static library target (they are
# appended to YETTY_SOURCES for the yetty executable only): ytrace.c,
# yplatform/getopt.c, and the yplatform/shared helpers ytrace depends on
# (thread for ymutex_*, term for yplatform_format_timestamp). Pull them in
# directly.

add_executable(ycat
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    ${YETTY_YPLATFORM_THREAD_SOURCES}
)

target_include_directories(ycat PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Use Threads::Threads so we get the right thing per platform:
# -lpthread on linux/macos, nothing on android (bionic has pthread in libc).
find_package(Threads REQUIRED)
target_link_libraries(ycat PRIVATE
    yetty_ycat
    Threads::Threads
)

if(NOT MSVC)
    target_compile_options(ycat PRIVATE -Wall -Wextra)
endif()

# Emscripten: ycat links libs that use C++ exceptions internally (pdfio,
# tree-sitter, freetype consumers), and binaryen's wasm-opt rejects the
# combined wasm with "popping from empty stack" unless the executable was
# compiled+linked with -fexceptions. Allow heap growth so a large input
# (PDF / markdown) doesn't OOM the default 16 MiB initial heap.
if(EMSCRIPTEN)
    # libpng / freetype prebuilts (build-tools/3rdparty/{libpng,freetype})
    # were compiled with the legacy SUPPORT_LONGJMP=emscripten ABI — they
    # reference emscripten_longjmp by name. Modern emcc defaults to the
    # wasm-native setjmp/longjmp model (different runtime symbol), so a
    # plain `emcc -o ycat.js` mixes the two and wasm-opt rejects the
    # output ("popping from empty stack"). Pin both the longjmp model
    # and exception model to the legacy values that match the prebuilt
    # libs. Until the libpng / freetype 3rdparty producers move to the
    # wasm model, all webasm consumers (yetty target included) need this.
    target_link_options(ycat PRIVATE
        -sSUPPORT_LONGJMP=emscripten
        -sDISABLE_EXCEPTION_CATCHING=1
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=64MB
    )
endif()
