add_executable(demo-ygui-34-textarea main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-34-textarea PRIVATE ygui_old)
target_compile_features(demo-ygui-34-textarea PRIVATE c_std_11)
