# yrdawn demos — programs that link libyetty_yrdawn and talk to the local
# yetty's yrdawn-layer over the PTY (stdin / stdout). Each demo runs as
# `./yetty -e demo-yrdawn-<NN>-<name>`.
#
# Shared support: ytrace + the few platform glue symbols ytrace needs.
# An OBJECT library compiled once and linked into every demo exec, same
# pattern demo/ygui uses.
#
# Skipped on Windows: every demo's main.c puts stdin into POSIX raw mode
# via termios so OSC envelopes from the parent yetty arrive byte-for-byte.
# Windows has no termios; the corresponding ConPTY-aware variant is not
# in scope for the cross-build matrix.
if(WIN32)
    return()
endif()

add_library(demo_yrdawn_support OBJECT
    common.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)

target_include_directories(demo_yrdawn_support PUBLIC
    ${YETTY_ROOT}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Inherit the codegen include dir (methods.gen.h) via yetty_yrdawn's
# PUBLIC include dirs — every demo links yetty_yrdawn anyway, but the
# support lib's translation units don't, so wire it in here too.
target_include_directories(demo_yrdawn_support PUBLIC
    $<TARGET_PROPERTY:yetty_yrdawn,INTERFACE_INCLUDE_DIRECTORIES>
)

find_package(Threads REQUIRED)

# Only demos that put something on screen are kept. 01_dawn_info prints the
# remote adapter metadata into the pane; the rest present pixels. The former
# headless bridge-exercisers (async-init, adapter-info, buffer-write,
# submit-chain, glyph-shaders, and the old hello no-op) drew nothing and were
# removed. All surviving demos quit on 'q' or Ctrl-C (see common.c).
add_subdirectory(01_dawn_info)
add_subdirectory(02_gradient)
add_subdirectory(04_animation)
add_subdirectory(09_glyph_render)
add_subdirectory(10_glyph_animate)
add_subdirectory(11_two_canvases)
