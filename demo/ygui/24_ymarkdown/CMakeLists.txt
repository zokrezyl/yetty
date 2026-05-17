add_executable(demo-ygui-24-ymarkdown main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-24-ymarkdown PRIVATE ygui ygui_ymarkdown)
target_compile_features(demo-ygui-24-ymarkdown PRIVATE c_std_11)
