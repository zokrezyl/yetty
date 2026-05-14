# yssh - SSH PTY library (libssh2 backend)

add_library(yetty_ssh STATIC
    ssh-pty.c
    openssh-config-parser.c
)

target_include_directories(yetty_ssh
    PUBLIC  ${YETTY_ROOT}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
            ${LIBSSH2_INCLUDE_DIR}
)

target_link_libraries(yetty_ssh
    PRIVATE yetty_ycore
            libssh2_static
)
