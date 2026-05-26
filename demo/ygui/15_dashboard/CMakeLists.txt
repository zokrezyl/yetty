add_executable(demo-ygui-15-dashboard main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-15-dashboard PRIVATE ygui_old)
target_compile_features(demo-ygui-15-dashboard PRIVATE c_std_11)
