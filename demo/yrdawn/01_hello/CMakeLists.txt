add_executable(demo-yrdawn-01-hello
    main.c
    $<TARGET_OBJECTS:demo_yrdawn_support>
)
target_include_directories(demo-yrdawn-01-hello PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-yrdawn-01-hello PRIVATE
    yetty_yrdawn
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-yrdawn-01-hello PRIVATE c_std_11)
