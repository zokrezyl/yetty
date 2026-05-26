add_executable(demo-ygui-07-label-and-button main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-07-label-and-button PRIVATE ygui_old)
target_compile_features(demo-ygui-07-label-and-button PRIVATE c_std_11)
