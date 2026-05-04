add_executable(demo-ygui-17-flex-row main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-17-flex-row PRIVATE ygui)
target_compile_features(demo-ygui-17-flex-row PRIVATE c_std_11)
