# Top-level test registration.
#
# Included from build-tools/yetty/platform/shared.cmake when
# YETTY_ENABLE_FEATURE_TESTS is ON. Owns the shared yetty_add_c_test() helper
# and the list of test subdirectories, so per-module CMakeLists files no longer
# repeat enable_testing(), include dirs, shim sources, or link/label/timeout
# boilerplate.

include(${CMAKE_CURRENT_LIST_DIR}/cmake/YettyTest.cmake)

# Threads is needed by the yfigure RPC round-trip test; find it once here so
# subdirectories can list Threads::Threads in their LIBS.
find_package(Threads REQUIRED)

# yclass — the object/dispatch/RPC spine. Always built; no feature gate.
add_subdirectory(ut/yclass)

# Core unit / contract / golden C tests. Each is gated on the module feature it
# exercises so a build with that module disabled does not try to link it.
if(YETTY_ENABLE_FEATURE_YPDF)
    add_subdirectory(ut/ypdf)
endif()
if(YETTY_ENABLE_FEATURE_YDIAGRAM)
    add_subdirectory(ut/ydiagram)
endif()
if(YETTY_ENABLE_FEATURE_YMARKDOWN)
    add_subdirectory(ut/ymarkdown)
endif()
add_subdirectory(ut/ygui)
add_subdirectory(ut/yfigure)
add_subdirectory(ut/ygrid)

# ybrowser geometry + headless render tests (the render smoke test carries the
# "network" label and is excluded from the fast lane).
add_subdirectory(ybrowser/ut)

# Integration: ylexbor harness driver. Self-skips when QuickJS or the ylexbor
# target isn't built.
add_subdirectory(integration/ylexbor)
