add_executable(demo-ygui-31-spinner main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-31-spinner PRIVATE ygui)
target_compile_features(demo-ygui-31-spinner PRIVATE c_std_11)
