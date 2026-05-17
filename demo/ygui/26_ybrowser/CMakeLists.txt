add_executable(demo-ygui-26-ybrowser main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-26-ybrowser PRIVATE ygui ygui_ybrowser)
target_compile_features(demo-ygui-26-ybrowser PRIVATE c_std_11)
