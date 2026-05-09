# yetty RPC module

add_library(yetty_yrpc STATIC
    rpc-message.c
    rpc-server.c
)

target_include_directories(yetty_yrpc PUBLIC
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

target_link_libraries(yetty_yrpc PUBLIC
    msgpack-c
)

# Platform-specific IPC socket implementation
if(WIN32)
    target_sources(yetty_yrpc PRIVATE
        ${YETTY_ROOT}/src/yetty/yplatform/ipc-socket/windows.c
    )
else()
    target_sources(yetty_yrpc PRIVATE
        ${YETTY_ROOT}/src/yetty/yplatform/ipc-socket/default.c
    )
endif()
