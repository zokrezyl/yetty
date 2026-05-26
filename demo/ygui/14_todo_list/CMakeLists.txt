add_executable(demo-ygui-14-todo-list main.c $<TARGET_OBJECTS:demo_ygui_support>)
target_link_libraries(demo-ygui-14-todo-list PRIVATE ygui_old)
target_compile_features(demo-ygui-14-todo-list PRIVATE c_std_11)
