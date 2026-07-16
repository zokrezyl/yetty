if(YETTY_ENABLE_TOOL_GPU_INFO)
    add_subdirectory(gpu-info)
endif()

if(YETTY_ENABLE_TOOL_CARD_RUNNER)
    add_subdirectory(card-runner)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_GENERATOR)
    add_subdirectory(ydraw-generator)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_BENCH)
    add_subdirectory(ydraw-bench)
endif()

if(YETTY_ENABLE_TOOL_YCAT)
    add_subdirectory(ycat)
endif()

if(YETTY_ENABLE_TOOL_YMCAT)
    add_subdirectory(ymcat)
endif()

# ynb-cat — render a Jupyter .ipynb notebook to the terminal: cell sources plus
# each output's richest MIME representation, shipped as rich figures. Drives the
# yetty_ynotebook model; needs ymime + yface for the file-envelope emitter.
if(YETTY_ENABLE_TOOL_YNB_CAT AND TARGET yetty_ynotebook
   AND TARGET yetty_ymime AND TARGET yetty_yface)
    add_subdirectory(ynb-cat)
endif()

if(YETTY_ENABLE_TOOL_YLESS)
    add_subdirectory(yless)
endif()

if(YETTY_ENABLE_TOOL_YECHO)
    add_subdirectory(yecho)
endif()

if(YETTY_ENABLE_TOOL_GEN_ERROR)
    add_subdirectory(gen-error)
endif()

if(YETTY_ENABLE_TOOL_YRDAWN_DEMO)
    add_subdirectory(yrdawn-demo)
endif()

if(YETTY_ENABLE_TOOL_YPLOT)
    add_subdirectory(yplot)
    add_subdirectory(yplot-stream)
endif()

if(YETTY_ENABLE_FEATURE_YMATH)
    add_subdirectory(ymath)
endif()

if(YETTY_ENABLE_TOOL_YFSPY)
    add_subdirectory(yfspy)
endif()

if(YETTY_ENABLE_TOOL_YVIDEO AND YETTY_ENABLE_FEATURE_YVIDEO)
    add_subdirectory(yvideo)
endif()

# Tool gate mirrors the library gate in src/yetty/CMakeLists.txt — the
# yetty_ymap target only exists when curl + yimage are on.
if(YETTY_ENABLE_TOOL_YMAP AND TARGET yetty_ymap)
    add_subdirectory(ymap)
endif()

if(YETTY_ENABLE_TOOL_YAUDIO_INTERVALS AND YETTY_ENABLE_FEATURE_YAUDIO)
    add_subdirectory(yaudio-intervals)
endif()

if(YETTY_ENABLE_TOOL_YAUDIO AND YETTY_ENABLE_FEATURE_YAUDIO)
    add_subdirectory(yaudio)
endif()

if(YETTY_ENABLE_TOOL_YCOMPOSITOR)
    add_subdirectory(ycompositor)
    add_subdirectory(ycompositor-ygui)
endif()

if(YETTY_ENABLE_TOOL_YTOP)
    add_subdirectory(ytop)
endif()

# yetty_ygit (the reusable component) only exists on desktop — gate on the
# target so non-desktop builds skip the tool. The CLI is POSIX-TTY: main.c/
# render.c use sys/ioctl.h + unistd.h (absent on Windows) and link yetty_ycat,
# so it builds on linux + macOS but not Windows. Mirrors the yai gate.
if(YETTY_ENABLE_TOOL_YGIT AND TARGET yetty_ygit
   AND (YETTY_PLATFORM STREQUAL "linux" OR YETTY_PLATFORM STREQUAL "macos"))
    add_subdirectory(ygit)
endif()

if(YETTY_ENABLE_TOOL_YNET)
    add_subdirectory(ynet)
endif()

if(YETTY_ENABLE_TOOL_YDU)
    add_subdirectory(ydu)
endif()

# yperf links the yetty_yflame static lib, which only exists when
# FEATURE_YFLAME is ON — gate on both so a FEATURE-off build doesn't try to
# link a non-existent target.
if(YETTY_ENABLE_TOOL_YPERF AND YETTY_ENABLE_FEATURE_YFLAME)
    add_subdirectory(yperf)
endif()

# ccc is POSIX-only (fork/execl child spawn, termios raw mode, poll,
# ioctl): tvOS prohibits fork outright and Windows lacks the headers.
# Linux-only for the moment — macOS likely works but is unverified.
if(YETTY_ENABLE_TOOL_CCC AND YETTY_PLATFORM STREQUAL "linux")
    add_subdirectory(ai/ccc)
endif()

# yai shares ccc's POSIX requirements (fork/execl, termios, poll, ioctl) —
# both linux and macos provide these.
if(YETTY_ENABLE_TOOL_YAI AND (YETTY_PLATFORM STREQUAL "linux" OR YETTY_PLATFORM STREQUAL "macos"))
    add_subdirectory(ai/yai)
endif()

if(YETTY_ENABLE_TOOL_YGREETER)
    add_subdirectory(ygreeter)
endif()

if(YETTY_ENABLE_TOOL_YHELLO)
    add_subdirectory(yhello)
endif()

if(YETTY_ENABLE_TOOL_YMESH)
    add_subdirectory(ymesh)
endif()

if(YETTY_ENABLE_TOOL_YMAZE)
    add_subdirectory(ymaze)
endif()

if(YETTY_ENABLE_TOOL_YZOO)
    add_subdirectory(yzoo)
endif()

if(YETTY_ENABLE_TOOL_YJUNGLE)
    add_subdirectory(yjungle)
endif()

if(YETTY_ENABLE_TOOL_YDRAW_MAZE)
    add_subdirectory(ydraw-maze)
endif()

if(YETTY_ENABLE_TOOL_YMUX)
    add_subdirectory(ymux)
endif()

if(YETTY_ENABLE_TOOL_YFLAME)
    add_subdirectory(yflame)
endif()

if(YETTY_ENABLE_TOOL_PDF2YDRAW)
    add_subdirectory(pdf2ydraw)
endif()

if(YETTY_ENABLE_TOOL_HTML2YDRAW)
    add_subdirectory(html2ydraw)
endif()

if(YETTY_ENABLE_TOOL_YHTML_MACHINE)
    add_subdirectory(yhtml-machine)
endif()

if(YETTY_ENABLE_TOOL_YBROWSER)
    add_subdirectory(ybrowser)
endif()

if(YETTY_ENABLE_TOOL_YDOC)
    add_subdirectory(ydoc)
endif()

if(YETTY_ENABLE_TOOL_YSHEET)
    add_subdirectory(ysheet)
endif()

if(YETTY_ENABLE_TOOL_YSLIDE)
    add_subdirectory(yslide)
endif()

if(YETTY_ENABLE_TOOL_YTHORVG AND YETTY_ENABLE_FEATURE_YTHORVG AND YETTY_ENABLE_LIB_THORVG)
    add_subdirectory(ythorvg)
endif()

if(YETTY_ENABLE_TOOL_YLOTTIE AND YETTY_ENABLE_FEATURE_YLOTTIE)
    add_subdirectory(ylottie)
endif()

if(YETTY_ENABLE_TOOL_DECODE_YDRAW)
    add_subdirectory(decode-ydraw)
    add_subdirectory(osc-analyzer)
endif()

# ydiagram CLI links the yetty_ydiagram static lib (built in
# src/yetty/ydiagram) — that lib only exists when FEATURE_YDIAGRAM is ON.
# Without this AND clause, default builds (TOOL=ON, FEATURE=OFF) try to
# link `-lyetty_ydiagram` against a non-existent target → ld error.
if(YETTY_ENABLE_TOOL_YDIAGRAM AND YETTY_ENABLE_FEATURE_YDIAGRAM)
    add_subdirectory(ydiagram)
endif()

# ychart CLI links the yetty_ychart static lib (built in src/yetty/ychart)
# — that lib only exists when FEATURE_YCHART is ON.
if(YETTY_ENABLE_TOOL_YCHART AND YETTY_ENABLE_FEATURE_YCHART)
    add_subdirectory(ychart)
endif()

# MSDF toolbox — CDB viewers/diff/generators/atlas-viewer. Pure C
# tooling, always built (the bits that need WGPU bring it in themselves).
add_subdirectory(msdf)

if(YETTY_ENABLE_TOOL_YNETSURF)
    add_subdirectory(ynetsurf)
endif()

if(YETTY_ENABLE_TOOL_YLEXBOR)
    add_subdirectory(ylexbor)
endif()
