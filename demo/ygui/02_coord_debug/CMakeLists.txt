add_executable(demo-ygui-02-coord main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-02-coord PRIVATE ygui_old)
target_compile_features(demo-ygui-02-coord PRIVATE c_std_11)
