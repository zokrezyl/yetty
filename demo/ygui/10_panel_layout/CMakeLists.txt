add_executable(demo-ygui-10-panel-layout main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-10-panel-layout PRIVATE ygui_old)
target_compile_features(demo-ygui-10-panel-layout PRIVATE c_std_11)
