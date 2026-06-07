# yless tool — `less`-semantics pager rendering into a server-side scrollable
# figure (yview). Reuses ycat's MIME-dispatched rendering for the content.

add_executable(yless
    main.c
    ${YETTY_ROOT}/src/yetty/yconfig/config.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yless PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(yless PRIVATE
    yetty_yview
    yetty_ycat
    yetty_ymusic   # LilyPond (.ly) scores render directly via ymusic
    yaml           # config.c parses the yetty config (brand colours)
    Threads::Threads
    yetty_yplatform_core
)

# libcurl pulls Brotli transitively (see tools/ycat for the rationale);
# re-link it at the end so libcurl resolves Brotli* symbols.
if(YETTY_ENABLE_LIB_LIBCURL AND TARGET CURL::libcurl AND TARGET brotlidec)
    target_link_libraries(yless PRIVATE brotlidec brotlicommon)
endif()

if(NOT MSVC)
    target_compile_options(yless PRIVATE -Wall -Wextra)
endif()
