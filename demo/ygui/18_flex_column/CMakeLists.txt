add_executable(demo-ygui-18-flex-column main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-18-flex-column PRIVATE ygui_old)
target_compile_features(demo-ygui-18-flex-column PRIVATE c_std_11)
