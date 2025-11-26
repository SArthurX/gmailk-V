cmake_minimum_required(VERSION 3.10)
project(gmailk-V)

set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})

include(${CMAKE_SOURCE_DIR}/tools/common.cmake)



file(GLOB CPP_SOURCES "src/*.cpp")
file(GLOB C_SOURCES "src/*.c")

add_executable(main
    ${CPP_SOURCES}
    ${C_SOURCES}
    ${COMMON_SOURCES} 
    ${SSD1306_SOURCES}
)

target_link_common_libraries(main)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
print_build_config(${PROJECT_NAME})
message(STATUS "  Sources: ${CPP_SOURCES} ${C_SOURCES}")
message(STATUS "  Output: ${CMAKE_BINARY_DIR}/main")
message(STATUS "===============================================")
