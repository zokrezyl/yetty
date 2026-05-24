# yetty_yface — semantic / message-type layer.
#
# Wire mechanics (envelope framing, b64, LZ4F) live in yetty_ywire;
# yface is a thin wrapper that delegates to ywire and adds typed
# helpers for higher-level message kinds (bin meta today; mouse,
# resize, key in upcoming passes).

add_library(yetty_yface STATIC yface.c)

target_include_directories(yetty_yface PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_yface PUBLIC
    yetty_ycore
    yetty_ywire
)

set_target_properties(yetty_yface PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

install(FILES ${YETTY_ROOT}/include/yetty/yface/yface.h
        DESTINATION include/yetty/yface)
install(TARGETS yetty_yface ARCHIVE DESTINATION lib)
