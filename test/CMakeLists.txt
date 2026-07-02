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

# yclass — the object/dispatch/RPC spine.
if(TARGET yetty_yclass)
    add_subdirectory(ut/yclass)
endif()

# ywire — OSC/DCS wire framing + codecs.
if(TARGET yetty_ywire)
    add_subdirectory(ut/ywire)
endif()

# ydraw-core — serialized draw-command layer.
if(TARGET yetty_ydraw AND TARGET yetty_ydraw_core)
    add_subdirectory(ut/ydraw-core)
endif()

# yvterm — terminal-core grid model (libvterm-backed), headless.
if(TARGET yetty_yvterm)
    add_subdirectory(ut/yvterm)
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

if(TARGET yetty_ygui)
    add_subdirectory(ut/ygui)
endif()
if(TARGET yetty_yfigure)
    add_subdirectory(ut/yfigure)
endif()
if(TARGET yetty_ygrid)
    add_subdirectory(ut/ygrid)
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
