add_executable(demo-ygui-13-color-mixer main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-13-color-mixer PRIVATE ygui_old)
target_compile_features(demo-ygui-13-color-mixer PRIVATE c_std_11)
