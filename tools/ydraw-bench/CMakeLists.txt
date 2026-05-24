add_executable(ydraw-bench
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)
target_link_libraries(ydraw-bench PRIVATE yetty_yface yetty_ycore yetty_yplatform_core)
target_compile_options(ydraw-bench PRIVATE -Wall -Wextra)
