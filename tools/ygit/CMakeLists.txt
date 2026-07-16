# ygit — read-only Git history explorer CLI (structured/textual mode).
#
# Thin entry over the reusable ygit:repo yclass component (src/yetty/ygit).
# libgit2 rides in transitively as a PRIVATE dep of yetty_ygit (propagated to
# this executable for the final link only — its headers stay hidden).
#
# ytrace is compiled per-final-target (not a standalone library), same as the
# other tools; the rest comes from yetty_ycore / yetty_yclass.

add_executable(ygit
    main.c
    render.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ygit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(ygit PRIVATE
    yetty_ygit
    yetty_yclass
    yetty_ycore
    yetty_ycat            # render.c reuses ycat's handlers + OSC envelope encoder
    yetty_yplatform_core  # ytrace.c uses yetty_yplatform_ymutex_* (ytop gets this via ygui)
    Threads::Threads
)

# Re-link brotli at the very end so libcurl.a (pulled in transitively by
# yetty_ycat) can resolve its Content-Encoding: br symbols — same reason and
# fix as tools/ycat/CMakeLists.txt.
if(YETTY_ENABLE_LIB_LIBCURL AND TARGET CURL::libcurl AND TARGET brotlidec)
    target_link_libraries(ygit PRIVATE brotlidec brotlicommon)
endif()

if(NOT MSVC)
    target_compile_options(ygit PRIVATE -Wall -Wextra)
endif()
