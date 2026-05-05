add_executable(demo-ygui-22-tree-complex main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-22-tree-complex PRIVATE ygui)
target_compile_features(demo-ygui-22-tree-complex PRIVATE c_std_11)
