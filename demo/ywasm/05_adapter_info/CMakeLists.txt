add_executable(demo-ywasm-05-adapter-info
    main.c
    $<TARGET_OBJECTS:demo_ywasm_support>
)
target_include_directories(demo-ywasm-05-adapter-info PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-ywasm-05-adapter-info PRIVATE
    yetty_ywasm
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-ywasm-05-adapter-info PRIVATE c_std_11)
