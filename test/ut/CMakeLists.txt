#=============================================================================
# Yetty Unit Tests
#=============================================================================

# Coverage option
option(YETTY_COVERAGE "Enable code coverage" OFF)

# Boost UT - Modern C++20 unit testing framework
CPMAddPackage(
    NAME ut
    GITHUB_REPOSITORY boost-ext/ut
    GIT_TAG v2.1.0
)

#-----------------------------------------------------------------------------
# Test library - shared test utilities
#-----------------------------------------------------------------------------
add_library(yetty_test_lib STATIC
    harness/terminal_harness.cpp
)

target_include_directories(yetty_test_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(yetty_test_lib PUBLIC
    vterm
    ut
    webgpu
)

#-----------------------------------------------------------------------------
# Unit test executable
#-----------------------------------------------------------------------------
add_executable(yetty_tests
    main.cpp
    terminal_scroll_test.cpp
    terminal_moverect_test.cpp
    terminal_content_test.cpp
    terminal_selection_test.cpp
    plugin_layer_test.cpp
    plugin_test.cpp
)

target_link_libraries(yetty_tests PRIVATE
    yetty_test_lib
    ut
)

# Coverage support
if(YETTY_COVERAGE)
    target_compile_options(yetty_test_lib PUBLIC --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(yetty_test_lib PUBLIC --coverage)
    target_compile_options(yetty_tests PRIVATE --coverage -fprofile-arcs -ftest-coverage)
    target_link_options(yetty_tests PRIVATE --coverage)
endif()

# Register with CTest
add_test(NAME yetty_tests COMMAND yetty_tests)

# Custom target for running tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS yetty_tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
