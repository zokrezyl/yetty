# yaudio — offline audio analysis. v1: mmap-backed PCM WAV reader and
# energy-based interval detector. The eventual WebGPU compute path will
# live as a sibling TU; for now everything is pure C / scalar math.

add_library(yetty_yaudio STATIC
    wav.c
    envelope.c
    intervals.c
)

target_include_directories(yetty_yaudio PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yaudio
    PUBLIC  yetty_ycore
)

# libm for sqrt/log10/pow (no-op on macOS / Windows MSVC where math is
# in the C runtime).
if(NOT MSVC AND NOT APPLE)
    target_link_libraries(yetty_yaudio PUBLIC m)
endif()

if(NOT MSVC)
    target_compile_options(yetty_yaudio PRIVATE -Wall -Wextra)
endif()
