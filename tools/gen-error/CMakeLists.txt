# gen-error — emits malformed OSC sequences for testing wire-statemachine
# error paths and the post_fatal_error → ynotify card surfacing.

add_executable(gen-error main.c)

target_include_directories(gen-error PRIVATE
    ${YETTY_ROOT}/include
)

if(NOT MSVC)
    target_compile_options(gen-error PRIVATE -Wall -Wextra)
endif()
