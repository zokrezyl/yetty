add_executable(demo-ygui-33-dialog main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-33-dialog PRIVATE ygui_old)
target_compile_features(demo-ygui-33-dialog PRIVATE c_std_11)
