# Top-level test registration.
#
# Included from build-tools/yetty/platform/shared.cmake when
# YETTY_ENABLE_FEATURE_TESTS is ON. Owns the shared yetty_add_c_test() helper
# and the list of test subdirectories, so per-module CMakeLists files no longer
# repeat enable_testing(), include dirs, shim sources, or link/label/timeout
# boilerplate.
#
# Every subdirectory is guarded on the presence of the module target(s) it links
# (if(TARGET ...)) or the module feature flag, so a build that disables a module
# simply drops its tests instead of failing to configure/link.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/YettyTest.cmake)

# Threads is needed by the yfigure/yclass RPC round-trip tests; find it once
# here so subdirectories can list Threads::Threads in their LIBS.
find_package(Threads REQUIRED)

# ycore — foundational data structures (buffer, base64, hex colour, hash map,
# error cause chain). Pure, headless.
if(TARGET yetty_ycore)
    add_subdirectory(ut/ycore)
endif()

# yexpr — expression parser (precedence, calls, buffer refs). Pure, headless.
if(TARGET yetty_yexpr)
    add_subdirectory(ut/yexpr)
endif()

# yplot resolver — expression↔options merge, precedence, presence, validation.
# GPU-less (yetty_yplot_core only).
if(TARGET yetty_yplot_core)
    add_subdirectory(ut/yplot)
endif()

# yrich — pure geometry/style helpers (document model needs a context, so it is
# out of scope for the pure lane).
if(TARGET yetty_yrich)
    add_subdirectory(ut/yrich)
endif()

# yclass — the object/dispatch/RPC spine.
if(TARGET yetty_yclass)
    add_subdirectory(ut/yclass)
endif()

# yctl — external-control msgpack-RPC message layer (pure wire contract).
if(TARGET yetty_yctl)
    add_subdirectory(ut/yctl)
endif()

# yconfig — YAML config tree: flat + structured sequence entries, slash-path
# navigation. config.c has no lib target of its own, so the test compiles it
# directly; gate on the yaml 3rd-party target it parses with.
if(TARGET yaml)
    add_subdirectory(ut/yconfig)
endif()

# ywire — OSC/DCS wire framing + codecs.
if(TARGET yetty_ywire)
    add_subdirectory(ut/ywire)
endif()

# ydraw-core — serialized draw-command layer.
if(TARGET yetty_ydraw_core AND TARGET yetty_ysdf)
    add_subdirectory(ut/ydraw-core)
endif()

# ymux — independent daemon-side terminal engine (#695), headless, no GPU.
if(TARGET yetty_ymux)
    add_subdirectory(ut/ymux)
endif()

# yvterm — terminal-core grid model (libvterm-backed), headless.
if(TARGET yetty_yvterm)
    add_subdirectory(ut/yvterm)
endif()

# ynotebook — nbformat MIME bundle model (text/binary/json payloads). Pure.
if(TARGET yetty_ynotebook)
    add_subdirectory(ut/ynotebook)
endif()

# yjupyter — Jupyter v5 message protocol + session correlation. Pure.
if(TARGET yetty_yjupyter)
    add_subdirectory(ut/yjupyter)
endif()

# yvnc — VNC capture protocol: pure tile-grid math + wire-format headers.
if(TARGET yetty_vnc)
    add_subdirectory(ut/yvnc)
endif()

# yfont — complex-script shaping (HarfBuzz). Only meaningful when the shaper is
# built in; the test self-skips when the Noto fonts are not staged.
if(YETTY_ENABLE_LIB_HARFBUZZ AND TARGET yetty_yfont_core)
    add_subdirectory(ut/yfont)
endif()

# Parser/render modules — gated on their feature flag (which controls the target).
if(YETTY_ENABLE_FEATURE_YPDF AND TARGET yetty_ypdf)
    add_subdirectory(ut/ypdf)
endif()
if(YETTY_ENABLE_FEATURE_YDIAGRAM AND TARGET yetty_ydiagram)
    add_subdirectory(ut/ydiagram)
endif()
if(YETTY_ENABLE_FEATURE_YMARKDOWN AND TARGET yetty_ymarkdown)
    add_subdirectory(ut/ymarkdown)
endif()
if(YETTY_ENABLE_FEATURE_YMSOFFICE AND TARGET yetty_ymsoffice)
    add_subdirectory(ut/ymsoffice)
endif()

# Parser/render modules — golden parse/IR/ydraw tests, gated on their target.
if(TARGET yetty_ychart)
    add_subdirectory(ut/ychart)
endif()
if(TARGET yetty_ycircuit)
    add_subdirectory(ut/ycircuit)
endif()
if(TARGET yetty_ymusic)
    add_subdirectory(ut/ymusic)
endif()
if(TARGET yetty_ysvg)
    add_subdirectory(ut/ysvg)
endif()
if(TARGET yetty_ysixel_core)
    add_subdirectory(ut/ysixel)
endif()

if(TARGET yetty_ygui)
    add_subdirectory(ut/ygui)
endif()
if(TARGET yetty_yfigure)
    add_subdirectory(ut/yfigure)
endif()
# yscene — retained scene graph (#691): dom tree contract + scene
# behavioral tests, both headless.
if(TARGET yetty_yscene)
    add_subdirectory(ut/yscene)
endif()

# ybrowser geometry + headless render tests (the render smoke test carries the
# "network" label and is excluded from the fast lane).
if(TARGET yetty_ybrowser)
    add_subdirectory(ybrowser/ut)
endif()

# Integration: ylexbor harness driver. Its own CMake self-skips when the
# yetty_ylexbor or qjs targets aren't built, but gate the entry too for symmetry.
if(TARGET yetty_ylexbor)
    add_subdirectory(integration/ylexbor)
endif()

# E2E: launched-yetty + yctl smoke (nightly/manual; self-skips headless). The
# yetty executable target is created after this file runs, so the e2e test
# points at the build-dir binary path and its script self-skips if it is absent
# rather than gating on if(TARGET yetty) here.
add_subdirectory(e2e)

# yclass codegen: pure-function units + generation-behavior tests for the
# codegen refactor (headless; the generation lane self-skips without clang).
add_subdirectory(codegen)
