include(${CMAKE_CURRENT_LIST_DIR}/../variables-defaults.cmake)

# No windowing system (UIKit owns the window).
set(YETTY_ENABLE_LIB_GLFW OFF CACHE BOOL "" FORCE)

# No qemu-system-riscv64 tarball for iOS; sandbox forbids exec anyway.
# The QEMU launcher lib stays ON so pty-factory/default.c's --qemu branch resolves.
set(YETTY_ENABLE_LIB_QEMU_BINARY OFF CACHE BOOL "" FORCE)

# ylexbor/ybrowser: lexbor uses POSIX file I/O and threading not available on iOS.
set(YETTY_ENABLE_FEATURE_YLEXBOR  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YLEXBOR     OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YBROWSER OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YBROWSER    OFF CACHE BOOL "" FORCE)

# ycat: yetty_ycat library is skipped on iOS (desktop-only POSIX bits).
set(YETTY_ENABLE_TOOL_YCAT OFF CACHE BOOL "" FORCE)

# ygreeter / ytop drive their own libuv event loops via ygui (full). On
# iOS the host process is the UIKit-owned yetty app — standalone tools
# don't make sense here.
set(YETTY_ENABLE_TOOL_YGREETER OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YINSTALL OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YTOP     OFF CACHE BOOL "" FORCE)

# QA tools require host LLVM/Clang libs and hardcode Linux paths.
set(YETTY_ENABLE_TOOL_QA OFF CACHE BOOL "" FORCE)

# Client CLI tools don't ship on iOS (libraries/modules still build).
set(YETTY_ENABLE_TOOL_YDIAGRAM OFF CACHE BOOL "" FORCE)
