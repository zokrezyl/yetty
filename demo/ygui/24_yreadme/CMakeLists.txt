add_executable(demo-ygui-24-yreadme main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-24-yreadme PRIVATE ygui ygui_yreadme)
target_compile_features(demo-ygui-24-yreadme PRIVATE c_std_11)
