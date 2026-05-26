add_executable(demo-ygui-05-debug-events main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-05-debug-events PRIVATE ygui_old)
target_compile_features(demo-ygui-05-debug-events PRIVATE c_std_11)
