add_executable(demo-ygui-28-yzoo main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-28-yzoo PRIVATE ygui_old ygui_old_yzoo)
target_compile_features(demo-ygui-28-yzoo PRIVATE c_std_11)
