add_executable(yetty-msdf-gen main.c)
target_include_directories(yetty-msdf-gen PRIVATE ${YETTY_ROOT}/include)
target_link_libraries(yetty-msdf-gen PRIVATE yetty_ymsdf_gen)
