# ytop2 — live system monitor on the ygui2 drawable-contract toolkit.
# Pure PTY producer: renders through DCS drawable envelopes only.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return() # /proc readers are Linux-only for now
endif()

add_executable(ytop2
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)
target_include_directories(ytop2 PRIVATE ${YETTY_ROOT}/include)
target_link_libraries(ytop2 PRIVATE yetty_ygui2)
