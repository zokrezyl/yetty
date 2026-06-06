include(${CMAKE_CURRENT_LIST_DIR}/../variables-defaults.cmake)

# libmagic: autotools-only build fails / mis-detects on macOS (picks up Homebrew
# header with incompatible struct layout). ycat depends on libmagic for MIME dispatch.
set(YETTY_ENABLE_LIB_LIBMAGIC  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YCAT  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YCAT     OFF CACHE BOOL "" FORCE)
# yless links yetty_ycat (disabled above) — the pager can't build without it.
set(YETTY_ENABLE_TOOL_YLESS    OFF CACHE BOOL "" FORCE)

# QA tools hardcode Linux LLVM paths.
set(YETTY_ENABLE_TOOL_QA OFF CACHE BOOL "" FORCE)

# Client CLI tools don't ship on macOS (libraries/modules still build).
set(YETTY_ENABLE_TOOL_YDIAGRAM OFF CACHE BOOL "" FORCE)
