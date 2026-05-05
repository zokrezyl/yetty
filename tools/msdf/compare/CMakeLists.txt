# CDB comparison tool - works on all platforms via CdbReader wrapper
add_executable(msdf-compare-cdb main.cpp)

target_link_libraries(msdf-compare-cdb PRIVATE
    cdb-wrapper
)

target_include_directories(msdf-compare-cdb PRIVATE
    ${YETTY_ROOT}/include
)
