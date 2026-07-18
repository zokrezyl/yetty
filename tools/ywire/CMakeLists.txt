# ywire tool — yetty-native consumer for the Chromium paint-bridge wire format.
# Reads the wire stream, builds a ydraw drawable-list, emits an OSC 666674
# envelope that a running yetty renders natively. Pure C, strictly client-side.

add_executable(ywire
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ywire PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ywire PRIVATE
    yetty_ycat
    Threads::Threads
    yetty_yplatform_core
)

# Match ycat: re-link brotli at the tail so libcurl (pulled transitively)
# resolves Brotli* symbols.
if(YETTY_ENABLE_LIB_LIBCURL AND TARGET CURL::libcurl AND TARGET brotlidec)
    target_link_libraries(ywire PRIVATE brotlidec brotlicommon)
endif()

if(NOT MSVC)
    target_compile_options(ywire PRIVATE -Wall -Wextra)
endif()
