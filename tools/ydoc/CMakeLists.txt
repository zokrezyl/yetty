# ydoc — rich-text editor. Thin entry; the shared host (window bring-up +
# ygui-decorated editor) lives in the yetty_yrich_app OBJECT library under
# src/yetty/yrich. We pull its objects in and link the same archive set
# (YRICH_APP_LINK_LIBS) wrapped in one GNU-ld group, since the inlined
# platform objects and the archives cross-reference.
if(NOT TARGET yetty_yrich_app)
    return()
endif()

add_executable(ydoc main.c $<TARGET_OBJECTS:yetty_yrich_app>)

target_include_directories(ydoc PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# ${YETTY_LIBS} (libuv / ymsdf / yctl / vnc / …) is only fully populated
# after shared.cmake finishes the src/yetty subdir pass, so it is empty in
# the src-scope cache var — add it here in tools/ scope where it is set.
if(NOT MSVC)
    target_link_options(ydoc PRIVATE "-Wl,--start-group")
    target_link_libraries(ydoc PRIVATE ${YRICH_APP_LINK_LIBS} ${YETTY_LIBS} "-Wl,--end-group")
else()
    target_link_libraries(ydoc PRIVATE ${YRICH_APP_LINK_LIBS} ${YETTY_LIBS})
endif()

# The WebGPU/Dawn stack is C++; the final link needs the C++ runtime.
set_target_properties(ydoc PROPERTIES LINKER_LANGUAGE CXX)

if(NOT MSVC)
    target_compile_options(ydoc PRIVATE -Wall -Wextra)
endif()
