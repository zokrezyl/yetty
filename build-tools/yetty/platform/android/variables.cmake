include(${CMAKE_CURRENT_LIST_DIR}/../variables-defaults.cmake)

# No windowing system on Android (SurfaceView owns the window).
set(YETTY_ENABLE_LIB_GLFW OFF CACHE BOOL "" FORCE)

# ybrowser: netsurf/libcss is unsupported on Android.
set(YETTY_ENABLE_FEATURE_YBROWSER OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YBROWSER    OFF CACHE BOOL "" FORCE)

# QA tools require host LLVM/Clang libs and hardcode Linux paths.
set(YETTY_ENABLE_TOOL_QA OFF CACHE BOOL "" FORCE)
