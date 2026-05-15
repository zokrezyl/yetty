# Card Runner - standalone interactive card renderer
# Usage: card-runner <card-name> "<args>"
# Example: card-runner yplot "{f=sin(x) @range=-pi..pi,-1..1 @grid=on}"

add_executable(card-runner
    main.cpp
    # Core sources needed for card infrastructure
    ${CMAKE_SOURCE_DIR}/src/yetty/card-factory.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/gpu-memory-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/gpu-buffer-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/gpu-texture-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/card.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/gpu-allocator.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/shader-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/font-manager.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/shader-font.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/msdf-atlas.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/ms-msdf-font.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/vector-sdf-font.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/vector-coverage-font.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/raster-font.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/bm-font.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/config.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/shared-memory.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/name-generator.cpp
    # Zoo/Maze renderers
    ${CMAKE_SOURCE_DIR}/src/yetty/ydraw-zoo/zoo-renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/yetty/ydraw-maze/maze-renderer.cpp
    # YGrid card source
    ${CMAKE_SOURCE_DIR}/src/yetty/cards/ygrid/ygrid.cpp
)

target_include_directories(card-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/yetty/ydraw-zoo
    ${CMAKE_SOURCE_DIR}/src/yetty/ydraw-maze
)

target_compile_definitions(card-runner PRIVATE
    YETTY_SHADERS_DIR="${CMAKE_BINARY_DIR}/assets/shaders"
    YETTY_ASSETS_DIR="${CMAKE_BINARY_DIR}/assets"
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(card-runner PRIVATE
    # WebGPU + GLFW
    webgpu
    glfw
    glfw3webgpu

    # Cards and core
    yetty_cards
    yetty_base
    yetty_font
    yetty_ydraw
    yetty_ydraw
    yetty_diagram
    ygrid
    cdb-wrapper

    # Utils
    ytrace::ytrace
    args
    glm::glm
    yaml-cpp
    stb
    lz4_static
)

target_compile_features(card-runner PRIVATE cxx_std_23)
