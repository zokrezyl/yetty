add_executable(demo-ygui-21-tree-with-panes main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-21-tree-with-panes PRIVATE ygui_old)
target_compile_features(demo-ygui-21-tree-with-panes PRIVATE c_std_11)
