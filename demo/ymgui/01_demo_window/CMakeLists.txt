add_executable(demo-ymgui-01-demo-window
    main.cpp
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)
target_link_libraries(demo-ymgui-01-demo-window PRIVATE
    ymgui_frontend
    yetty_yplatform_core
)
target_compile_features(demo-ymgui-01-demo-window PRIVATE cxx_std_17)
