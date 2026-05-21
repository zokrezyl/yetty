add_executable(demo-yrdawn-04-animation
    main.c
    $<TARGET_OBJECTS:demo_yrdawn_support>
)
target_include_directories(demo-yrdawn-04-animation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-yrdawn-04-animation PRIVATE
    yetty_yrdawn
    yetty_yplatform_core
    Threads::Threads
    m
)
target_compile_features(demo-yrdawn-04-animation PRIVATE c_std_11)
