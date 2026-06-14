# yfspy tool — renders a Python-subset yfsvm shader as a yplot composite.
# Compiles the .py via the pyc/ frontend (client-side), then emits the same
# yplot OSC envelope as tools/yplot. Pure C frontend over yetty_yplot.

add_executable(yfspy
    main.c
    ${YETTY_ROOT}/src/yetty/ytrace/ytrace.c
)

target_include_directories(yfspy PRIVATE
    ${YETTY_ROOT}/include
    ${YETTY_ROOT}/src
)

# Default path to this tool's bundled Python-subset compiler. Overridable at
# runtime via --compiler=PATH or $YFSPY_COMPILER.
target_compile_definitions(yfspy PRIVATE
    YFSPY_COMPILER_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/assets/cli.py"
)

# Threads::Threads picks the right thing per platform — same pattern as
# tools/yplot and tools/yecho.
find_package(Threads REQUIRED)
target_link_libraries(yfspy PRIVATE
    yetty_yplot_core
    Threads::Threads
    yetty_yplatform_core
)

if(NOT MSVC)
    target_compile_options(yfspy PRIVATE -Wall -Wextra)
endif()
