include(${CMAKE_CURRENT_LIST_DIR}/../variables-defaults.cmake)

# libssh2: Homebrew openssl@3 in /usr/local/include shadows our pinned 1.1.1w.
# libssh2's openssl.c picks up the 3.x header, emits EVP_MAC_*/OSSL_PARAM_*
# refs that the pinned libcrypto.a doesn't provide → undefined symbols at link.
# Disable until yetty migrates to openssl-new or libssh2's include order is fixed.
set(YETTY_ENABLE_FEATURE_SSH OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_LIB_LIBSSH2 OFF CACHE BOOL "" FORCE)

# libmagic: autotools-only build fails / mis-detects on macOS (picks up Homebrew
# header with incompatible struct layout). ycat depends on libmagic for MIME dispatch.
set(YETTY_ENABLE_LIB_LIBMAGIC  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_FEATURE_YCAT  OFF CACHE BOOL "" FORCE)
set(YETTY_ENABLE_TOOL_YCAT     OFF CACHE BOOL "" FORCE)

# QA tools hardcode Linux LLVM paths.
set(YETTY_ENABLE_TOOL_QA OFF CACHE BOOL "" FORCE)
