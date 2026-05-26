add_executable(demo-ygui-19-flex-dashboard main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-19-flex-dashboard PRIVATE ygui_old)
target_compile_features(demo-ygui-19-flex-dashboard PRIVATE c_std_11)
