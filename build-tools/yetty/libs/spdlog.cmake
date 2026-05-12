# spdlog - fast C++ logging library
if(TARGET spdlog::spdlog)
    return()
endif()

CPMAddPackage(
    NAME spdlog
    GITHUB_REPOSITORY gabime/spdlog
    GIT_TAG v1.16.0
    OPTIONS "CMAKE_POSITION_INDEPENDENT_CODE ON"
)
