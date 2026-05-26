add_executable(demo-ygui-00-simple main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-00-simple PRIVATE ygui_old)
target_compile_features(demo-ygui-00-simple PRIVATE c_std_11)
