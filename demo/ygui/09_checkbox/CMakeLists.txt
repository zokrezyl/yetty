add_executable(demo-ygui-09-checkbox main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-09-checkbox PRIVATE ygui_old)
target_compile_features(demo-ygui-09-checkbox PRIVATE c_std_11)
