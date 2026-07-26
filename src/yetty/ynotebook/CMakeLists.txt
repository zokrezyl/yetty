# ynotebook — native Jupyter notebook model (yclass). First increment: the
# nbformat MIME bundle as the `ynotebook:mime_bundle` class. The MIME layer
# lives here for now and is expected to split into a leaf `ymime` bundle lib +
# a `yrender-mime` registry later (see docs/ynotebook.md).
#
# mime-bundle.c is the only hand-written source; it #includes its generated
# mime-bundle.gen.c at the foot. rpc.gen.c (register + skeletons) is compiled
# alongside. Both generated files come from `make codegen` — never hand-edited.
#
# JSON is parsed/written with the prebuilt yyjson; base64 comes from ycore.

include(${YETTY_ROOT}/build-tools/yetty/libs/yyjson.cmake)

add_library(yetty_api_ynotebook STATIC
    ${YETTY_ROOT}/src/yetty/gen/api/ynotebook/mime-bundle.c
    ${YETTY_ROOT}/src/yetty/gen/api/ynotebook/notebook.c
)
target_include_directories(yetty_api_ynotebook PUBLIC ${YETTY_ROOT}/include)
target_link_libraries(yetty_api_ynotebook PUBLIC yetty_ycore yetty_yclass)

add_library(yetty_ynotebook STATIC
    mime-bundle.c
    notebook.c
)

target_include_directories(yetty_ynotebook PUBLIC
    ${YETTY_ROOT}/include
)

target_link_libraries(yetty_ynotebook PUBLIC
    yetty_api_ynotebook
    yetty_ycore
    yetty_yclass
    yyjson::yyjson
)

# mime-bundle.c carries YETTY_ANNOTATE(...) attributes — same C23 constraint as
# every yclass module.
if(NOT MSVC)
    set_target_properties(yetty_ynotebook PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
    )
endif()
