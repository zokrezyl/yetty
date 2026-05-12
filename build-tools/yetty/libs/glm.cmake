# GLM - OpenGL Mathematics library
if(TARGET glm::glm)
    return()
endif()

CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.1
)
