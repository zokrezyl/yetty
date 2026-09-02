# ygreeter2 — the ygui2 widget-catalog showcase on the yguiapp2 host.
add_executable(ygreeter2
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
    ${YETTY_ROOT}/src/yetty/yplatform/thread/default.c
    ${YETTY_ROOT}/src/yetty/yplatform/term/default.c
)
target_include_directories(ygreeter2 PRIVATE ${YETTY_ROOT}/include)
target_link_libraries(ygreeter2 PRIVATE yetty_yguiapp2)
