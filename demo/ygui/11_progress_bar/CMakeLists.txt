add_executable(demo-ygui-11-progress-bar main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-11-progress-bar PRIVATE ygui_old)
target_compile_features(demo-ygui-11-progress-bar PRIVATE c_std_11)
