# yetty RPC module

add_library(yetty_yctl STATIC
    rpc-message.c
    rpc-server.c
)

target_include_directories(yetty_yctl PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yctl PUBLIC
    msgpack-c
)

# Platform-specific IPC socket implementation
if(WIN32)
    target_sources(yetty_yctl PRIVATE
        ${YETTY_ROOT}/src/yetty/yplatform/ipc-socket/windows.c
    )
else()
    target_sources(yetty_yctl PRIVATE
        ${YETTY_ROOT}/src/yetty/yplatform/ipc-socket/default.c
    )
endif()
