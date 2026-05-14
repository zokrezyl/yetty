# yssh - SSH PTY library (openssh forkpty backend)

add_library(yetty_ssh STATIC
    ssh-pty.c
    ssh-binary-locator.c
)

target_include_directories(yetty_ssh
    PUBLIC  ${YETTY_ROOT}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(yetty_ssh
    PRIVATE yetty_ycore
)
