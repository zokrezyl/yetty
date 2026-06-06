# yinstall — the installer module/library.
#
# Platform-independent glue: the component model, the install loop, brotli
# decompression and the install log (install.c). It is deliberately thin on
# dependencies — just yplatform_core (paths / fs) and the brotli decoder.
#
# Two symbols it references are resolved by the *tool* target, not here:
#   - yetty_yplatform_install_foreach_asset — provided by the platform
#     backend (yplatform/install/{incbin,winres}.c), which must be compiled
#     where the generated asset manifests live (i.e. the tool).
#   - the ytrace ydebug/yerror entry points — the tool compiles ytrace.c.
# A static library is allowed to carry such unresolved externals until the
# final link, exactly as ygreeter's module sources do.
add_library(yetty_yinstall STATIC
    install.c
)

target_include_directories(yetty_yinstall PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yinstall PUBLIC
    yetty_yplatform_core
)

# Brotli decoder for the compressed components (data / yemu).
include(${YETTY_ROOT}/build-tools/yetty/libs/brotli.cmake)
target_link_libraries(yetty_yinstall PUBLIC brotlidec brotlicommon)

if(NOT MSVC)
    target_compile_options(yetty_yinstall PRIVATE -Wall -Wextra)
endif()
