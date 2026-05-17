add_executable(demo-ygui-30-radio main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-30-radio PRIVATE ygui)
target_compile_features(demo-ygui-30-radio PRIVATE c_std_11)
