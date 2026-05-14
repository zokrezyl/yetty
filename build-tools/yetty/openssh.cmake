# openssh — prebuilt ssh client binary, built from openssh-portable linked
# against openssl-new (4.x).
#
# Fetches the per-platform tarball published by build-3rdparty-openssh.yml,
# tagged lib-openssh-<ver>. Version source of truth:
# build-tools/3rdparty/openssh/version.
#
# Exposes:
#   YETTY_OPENSSH_SSH_BINARY  — FILEPATH cache variable pointing at the
#                               extracted ssh binary. Consumed by:
#                               1. The install rule below (stages into the
#                                  yetty install tree).
#                               2. src/yetty/yssh/ssh-binary-locator.c
#                                  falls back to YETTY_SSH_BINARY env var
#                                  at runtime (set it to this path in dev).

include_guard(GLOBAL)
include(${YETTY_ROOT}/build-tools/yetty/3rdparty-fetch.cmake)

yetty_3rdparty_fetch(openssh _OPENSSH_DIR)

set(YETTY_OPENSSH_SSH_BINARY "${_OPENSSH_DIR}/bin/ssh"
    CACHE FILEPATH "Path to bundled openssh client binary" FORCE)

if(NOT EXISTS "${YETTY_OPENSSH_SSH_BINARY}")
    message(FATAL_ERROR
        "openssh: ssh binary not found at ${YETTY_OPENSSH_SSH_BINARY} — \
tarball layout changed? (check build-tools/3rdparty/openssh/_build.sh)")
endif()

# Stage the binary into the yetty install tree so the runtime locator in
# ssh-binary-locator.c finds it at <exe_dir>/../libexec/yetty/openssh/ssh.
install(PROGRAMS "${YETTY_OPENSSH_SSH_BINARY}"
        DESTINATION libexec/yetty/openssh
        RENAME ssh)

message(STATUS "openssh: prebuilt v${YETTY_3RDPARTY_openssh_VERSION} (${YETTY_OPENSSH_SSH_BINARY})")
