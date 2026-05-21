add_executable(demo-ywasm-04-animation
    main.c
    $<TARGET_OBJECTS:demo_ywasm_support>
)
target_include_directories(demo-ywasm-04-animation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-ywasm-04-animation PRIVATE
    yetty_ywasm
    yetty_yplatform_core
    Threads::Threads
    m
)
target_compile_features(demo-ywasm-04-animation PRIVATE c_std_11)
