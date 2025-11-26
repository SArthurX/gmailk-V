cmake_minimum_required(VERSION 3.10)
project(gmailk-V-test)

get_filename_component(PROJECT_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)

include(${PROJECT_ROOT}/tools/common.cmake)

include_directories(${CMAKE_SOURCE_DIR})

file(GLOB TEST_C_SOURCES "${CMAKE_SOURCE_DIR}/*.c")
file(GLOB TEST_CPP_SOURCES "${CMAKE_SOURCE_DIR}/*.cpp")

add_executable(test
    ${TEST_C_SOURCES}
    ${TEST_CPP_SOURCES}
    ${COMMON_SOURCES} 
    ${SSD1306_SOURCES}
)

target_link_common_libraries(test)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

print_build_config(${PROJECT_NAME})
message(STATUS "  Test Sources: ${TEST_C_SOURCES} ${TEST_CPP_SOURCES}")
message(STATUS "  Output: ${CMAKE_BINARY_DIR}/test")
message(STATUS "===============================================")
