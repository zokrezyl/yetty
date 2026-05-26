add_executable(demo-ygui-03-widgets main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-03-widgets PRIVATE ygui_old)
target_compile_features(demo-ygui-03-widgets PRIVATE c_std_11)
