add_executable(demo-ygui-35-collapsing-header-open main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-35-collapsing-header-open PRIVATE ygui)
target_compile_features(demo-ygui-35-collapsing-header-open PRIVATE c_std_11)
