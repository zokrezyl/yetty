# yscene-demo tool — minimal producer for the `yscene` retained-scene
# figure kind (#691): ships a background run + nested groups as a card
# via yview. The reference producer shape for the yscene wire adapter,
# and the live smoke test for the scene's GPU path.

add_executable(yscene-demo
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yscene-demo PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

find_package(Threads REQUIRED)
target_link_libraries(yscene-demo PRIVATE
    yetty_yview
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yscene-demo PRIVATE -Wall -Wextra)
endif()
