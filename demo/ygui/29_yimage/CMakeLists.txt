add_executable(demo-ygui-29-yimage main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-29-yimage PRIVATE ygui_old)
target_compile_features(demo-ygui-29-yimage PRIVATE c_std_11)
