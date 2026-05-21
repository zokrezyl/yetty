add_executable(demo-yrdawn-02-gradient
    main.c
    $<TARGET_OBJECTS:demo_yrdawn_support>
)
target_include_directories(demo-yrdawn-02-gradient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-yrdawn-02-gradient PRIVATE
    yetty_yrdawn
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-yrdawn-02-gradient PRIVATE c_std_11)
