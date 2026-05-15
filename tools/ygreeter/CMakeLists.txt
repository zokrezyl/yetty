# ygreeter — the "first contact" tool shown on the RISCV Linux VM (also
# loadable as a regular yetty tool). Demos the ygui tabbar + rich widgets:
# four tabs (Welcome / Plots / Images / Code), each with a small tree
# nav on the left and a rich content surface on the right.
#
# ytrace + the per-platform yplatform impls (thread/term/fs/time) are
# compiled per-final-target — they're not standalone libraries. Use
# YETTY_YPLATFORM_THREAD_SOURCES which shared.cmake populates with the
# correct {default,windows}.c picks so the build stays portable. (The
# variable name predates fs/ being lumped in; same shape as the main
# yetty target.)

add_executable(ygreeter
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(ygreeter PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# ygui carries ydraw-core + ysdf + ycore + yface + yfont_core + the
# ydraw-yaml dep needed by rich's create_from_yaml. yetty_yimage_core
# is added on top so we can call yetty_yimage_render_path() — the
# yimage YAML factory isn't wired (yaml_factory: none) so we build the
# image-bearing buffer programmatically and hand it to rich_set_buffer.
target_link_libraries(ygreeter PRIVATE ygui yetty_yimage_core yetty_yplatform_core)

if(NOT MSVC)
    target_compile_options(ygreeter PRIVATE -Wall -Wextra)
endif()
