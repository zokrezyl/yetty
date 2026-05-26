add_executable(demo-ygui-12-calculator main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-12-calculator PRIVATE ygui_old)
target_compile_features(demo-ygui-12-calculator PRIVATE c_std_11)
