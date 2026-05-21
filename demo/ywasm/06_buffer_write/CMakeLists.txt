add_executable(demo-ywasm-06-buffer-write
    main.c
    $<TARGET_OBJECTS:demo_ywasm_support>
)
target_include_directories(demo-ywasm-06-buffer-write PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(demo-ywasm-06-buffer-write PRIVATE
    yetty_ywasm
    yetty_yplatform_core
    Threads::Threads
)
target_compile_features(demo-ywasm-06-buffer-write PRIVATE c_std_11)
