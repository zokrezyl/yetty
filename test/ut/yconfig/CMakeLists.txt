# yconfig contract test — YAML loader sequence handling (flat scalar lists +
# structured mapping entries) and slash-path navigation.
#
# yconfig/config.c is compiled straight into the yetty binary (no standalone
# lib target), so the test pulls the source in directly, plus the platform
# paths/getopt sources it references — same pattern as yvterm's memory-pty.c.

if(APPLE)
    set(YCONFIG_TEST_PATHS_SOURCE ${YETTY_ROOT}/src/yetty/yplatform/paths/macos.c)
else()
    set(YCONFIG_TEST_PATHS_SOURCE ${YETTY_ROOT}/src/yetty/yplatform/paths/linux.c)
endif()
set(YCONFIG_TEST_FS_SOURCE ${YETTY_ROOT}/src/yetty/yplatform/fs/default.c)

yetty_add_c_test(
    NAME    yconfig_config
    SOURCES config-test.c
            ${YETTY_ROOT}/src/yetty/yconfig/config.c
            ${YETTY_ROOT}/src/yetty/yplatform/paths/create.c
            ${YCONFIG_TEST_PATHS_SOURCE}
            ${YCONFIG_TEST_FS_SOURCE}
            ${YETTY_ROOT}/src/yetty/yplatform/getopt.c
    LIBS    yaml yetty_ycore
    SHIMS   trace platform_thread platform_term
    LABELS  contract config
    TIMEOUT 30)
