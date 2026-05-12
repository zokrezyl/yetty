add_executable(demo-ygui-23-rich-tabbar main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-23-rich-tabbar PRIVATE ygui)
target_compile_features(demo-ygui-23-rich-tabbar PRIVATE c_std_11)
