add_executable(demo-yrdawn-12-nbody
    main.c
    $<TARGET_OBJECTS:demo_yrdawn_support>
)
target_include_directories(demo-yrdawn-12-nbody PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-yrdawn-12-nbody PRIVATE
    yetty_yrdawn
    yetty_yplatform_core
    Threads::Threads
    m
)
target_compile_features(demo-yrdawn-12-nbody PRIVATE c_std_11)
