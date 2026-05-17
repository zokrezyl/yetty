add_executable(demo-ygui-25-ypdf main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-25-ypdf PRIVATE ygui ygui_ypdf)
target_compile_features(demo-ygui-25-ypdf PRIVATE c_std_11)
