add_executable(test-font-positioning test-font-positioning.cpp)

target_include_directories(test-font-positioning PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test-font-positioning PRIVATE
    yetty_core
    glfw
    glfw3webgpu
    webgpu
)

target_compile_features(test-font-positioning PRIVATE cxx_std_17)
