add_executable(demo-yrdawn-07-submit-chain
    main.c
    $<TARGET_OBJECTS:demo_yrdawn_support>
)
target_include_directories(demo-yrdawn-07-submit-chain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-yrdawn-07-submit-chain PRIVATE
    yetty_yrdawn
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-yrdawn-07-submit-chain PRIVATE c_std_11)
