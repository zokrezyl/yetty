# Taywee/args - command line argument parser
if(TARGET args)
    return()
endif()

CPMAddPackage(
    NAME args
    GITHUB_REPOSITORY Taywee/args
    GIT_TAG 6.4.6
    OPTIONS
        "ARGS_BUILD_EXAMPLE OFF"
        "ARGS_BUILD_UNITTESTS OFF"
)
