add_executable(demo-ygui-32-splitter main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-32-splitter PRIVATE ygui)
target_compile_features(demo-ygui-32-splitter PRIVATE c_std_11)
