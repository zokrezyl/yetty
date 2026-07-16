include(${CMAKE_CURRENT_LIST_DIR}/../variables-defaults.cmake)

# libmagic / ycat / yless build on macOS via the prebuilt libmagic tarball
# (libmagic-macos-*, published by build-3rdparty-libmagic.yml). The prebuilt
# ships its own magic.h, so the old from-source Homebrew-header struct-layout
# conflict no longer applies — libmagic, ycat, and yless keep their ON
# defaults here. (Windows still disables libmagic: no MSVC port.)

# QA tools hardcode Linux LLVM paths.
set(YETTY_ENABLE_TOOL_QA OFF CACHE BOOL "" FORCE)

# Client CLI tools don't ship on macOS (libraries/modules still build).
set(YETTY_ENABLE_TOOL_YDIAGRAM OFF CACHE BOOL "" FORCE)
