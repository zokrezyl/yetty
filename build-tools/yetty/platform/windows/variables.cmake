include(${CMAKE_CURRENT_LIST_DIR}/../variables-defaults.cmake)

# RISC-V emulators: both enabled on Windows.
set(YETTY_ENABLE_LIB_TINYEMU ON CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_QEMU    ON CACHE BOOL "" FORCE)

# msdfgen prebuilt ships /MT CRT; rest of 3rdparty uses /MD — mixing causes LNK2038.
set(YETTY_ENABLE_FEATURE_YMSDF_GEN OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_MSDFGEN       OFF CACHE BOOL "" FORCE)

# Tool drivers using POSIX termios/select — not ported to Windows.
set(YETTY_ENABLE_TOOL_YMESH OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YMAZE OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YZOO  OFF CACHE BOOL "" FORCE)

# Features/libs whose sources use POSIX APIs not yet ported to Windows.
set(YETTY_ENABLE_FEATURE_SSH       OFF CACHE BOOL "" FORCE)  # <netdb.h>, <pthread.h>, <poll.h>
set(YETTY_ENABLE_LIB_LIBSSH2       OFF CACHE BOOL "" FORCE)  # yssh wrapper not ported
set(YETTY_ENABLE_FEATURE_YTHORVG   OFF CACHE BOOL "" FORCE)  # C99 compound literals (MSVC C++ rejects)
set(YETTY_ENABLE_LIB_THORVG        OFF CACHE BOOL "" FORCE)  # only consumer is FEATURE_YTHORVG
set(YETTY_ENABLE_TOOL_YTHORVG      OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YPAINT_BENCH OFF CACHE BOOL "" FORCE)  # passes -Wextra unconditionally (MSVC: D8021)
set(YETTY_ENABLE_FEATURE_DEMO      OFF CACHE BOOL "" FORCE)  # hardcodes shared/{thread,term}.c (POSIX)
set(YETTY_ENABLE_TOOL_YDOC         OFF CACHE BOOL "" FORCE)  # poll.h, termios.h, unistd.h
set(YETTY_ENABLE_TOOL_YSHEET       OFF CACHE BOOL "" FORCE)  # uses yrich-runner (POSIX TTY)
set(YETTY_ENABLE_TOOL_YSLIDE       OFF CACHE BOOL "" FORCE)  # uses yrich-runner (POSIX TTY)
set(YETTY_ENABLE_TOOL_YGREETER     OFF CACHE BOOL "" FORCE)  # STDOUT/STDERR_FILENO + struct stat (unistd.h, sys/stat.h)
set(YETTY_ENABLE_LIB_LIBMAGIC      OFF CACHE BOOL "" FORCE)  # autotools-only; no MSVC port
set(YETTY_ENABLE_LIB_LIBCURL       OFF CACHE BOOL "" FORCE)  # no Windows consumer yet
set(YETTY_ENABLE_FEATURE_YLEXBOR   OFF CACHE BOOL "" FORCE)  # GCC array initializers + POSIX <strings.h>
set(YETTY_ENABLE_TOOL_YLEXBOR      OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YBROWSER  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YBROWSER     OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YNETSURF  OFF CACHE BOOL "" FORCE)  # NetSurf prebuilt is non-Windows
set(YETTY_ENABLE_TOOL_YNETSURF     OFF CACHE BOOL "" FORCE)

# QA tools hardcode Linux LLVM paths.
set(YETTY_ENABLE_TOOL_QA OFF CACHE BOOL "" FORCE)
