add_executable(demo-ygui-20-tree-view main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-20-tree-view PRIVATE ygui_old)
target_compile_features(demo-ygui-20-tree-view PRIVATE c_std_11)
