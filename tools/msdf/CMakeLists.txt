# MSDF tooling — generators, viewers, and diff utilities for yetty .cdb
# files. Grouped together so the whole MSDF toolbox can be enabled or
# disabled with a single switch.

# Desktop-only: these are host-side CLI/GUI utilities (some run at build
# time to generate CDBs, the atlas viewer needs GLFW). They are never
# shipped with mobile/web builds.
if(EMSCRIPTEN OR YETTY_ANDROID OR YETTY_IOS OR YETTY_TVOS OR YETT_WINDOWS)
    return()
endif()

# Read-only walker for .cdb files (pure C, no library deps — parses the
# CDB file format directly). Always built.
add_subdirectory(cdb-viewer)

# Pairwise diff of two .cdb files, ranks glyphs by per-pixel divergence.
# Same direct CDB walk as cdb-viewer; always built.
add_subdirectory(cdb-diff)

# CPU MSDF generator CLI (msdfgen library). Skipped where the
# msdfgen lib is disabled (e.g. Windows: prebuilt msdfgen ships /MT
# while the rest of the Windows 3rdparty libs are /MD).
if(YETTY_ENABLE_FEATURE_YMSDF_GEN)
    add_subdirectory(gen-msdf)
endif()

# GPU MSDF generator CLI (WGSL compute shader). Headless — no window.
add_subdirectory(gen-msdf-gpu)

# Atlas viewer — opens a window and renders all glyphs in a CDB so you
# can visually inspect quality. Reuses the same CDB walker.
add_subdirectory(render-atlas)
