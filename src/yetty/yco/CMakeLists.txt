add_library(yetty_yco STATIC
    co.c
)

target_include_directories(yetty_yco PUBLIC
    ${YETTY_ROOT}/include
)

# Apple clang searches /usr/local/include before -isystem paths, so the
# bundled libco.h gets shadowed by a Homebrew-installed one (e.g. via
# fluent-bit). Force the imported `co` target's include dir to be added
# as -I instead of -isystem so it wins the search.
set_target_properties(yetty_yco PROPERTIES NO_SYSTEM_FROM_IMPORTED TRUE)

target_link_libraries(yetty_yco PUBLIC
    co
)
