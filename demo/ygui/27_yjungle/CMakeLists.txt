add_executable(demo-ygui-27-yjungle main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-27-yjungle PRIVATE ygui_old ygui_old_yjungle)
target_compile_features(demo-ygui-27-yjungle PRIVATE c_std_11)
