add_executable(ydraw-bench main.c)
target_link_libraries(ydraw-bench PRIVATE yetty_yface yetty_ycore)
target_compile_options(ydraw-bench PRIVATE -Wall -Wextra)
