# yslide — presentation editor. Thin entry over the shared yetty_yrich_app
# host (see tools/ydoc/CMakeLists.txt for the link-group rationale).
if(NOT TARGET yetty_yrich_app)
    return()
endif()

add_executable(yslide main.c $<TARGET_OBJECTS:yetty_yrich_app>)

target_include_directories(yslide PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

if(NOT MSVC)
    target_link_options(yslide PRIVATE "-Wl,--start-group")
    target_link_libraries(yslide PRIVATE ${YRICH_APP_LINK_LIBS} ${YETTY_LIBS} "-Wl,--end-group")
else()
    target_link_libraries(yslide PRIVATE ${YRICH_APP_LINK_LIBS} ${YETTY_LIBS})
endif()

set_target_properties(yslide PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(yslide PRIVATE -Wall -Wextra)
endif()
