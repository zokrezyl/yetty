# ygreeter — the "first contact" tool shown on the RISCV Linux VM (also
# loadable as a regular yetty tool). Demos the ygui tabbar + rich widgets:
# four tabs (Welcome / Plots / Images / Code), each with a small tree
# nav on the left and a rich content surface on the right.
#
# ytrace + yplatform/shared/{thread,term} are compiled per-final-target
# (they're not standalone libraries) — pull them in directly, same shape
# as tools/ytop.

add_executable(ygreeter
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

target_include_directories(ygreeter PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# ygui carries ypaint-core + ysdf + ycore + yface + yfont_core + the
# ypaint-yaml dep needed by rich's create_from_yaml. yetty_yimage_core
# is added on top so we can call yetty_yimage_render_path() — the
# yimage YAML factory isn't wired (yaml_factory: none) so we build the
# image-bearing buffer programmatically and hand it to rich_set_buffer.
target_link_libraries(ygreeter PRIVATE ygui yetty_yimage_core)

if(NOT MSVC)
    target_compile_options(ygreeter PRIVATE -Wall -Wextra)
endif()
