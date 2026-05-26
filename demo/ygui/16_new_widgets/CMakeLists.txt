add_executable(demo-ygui-16-new-widgets main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-16-new-widgets PRIVATE ygui_old)
target_compile_features(demo-ygui-16-new-widgets PRIVATE c_std_11)
