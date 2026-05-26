add_executable(demo-ygui-06-hello-button main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-06-hello-button PRIVATE ygui_old)
target_compile_features(demo-ygui-06-hello-button PRIVATE c_std_11)
