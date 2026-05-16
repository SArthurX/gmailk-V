cmake_minimum_required(VERSION 3.10)
project(gmailk-V)

set(PROJECT_ROOT ${CMAKE_SOURCE_DIR})

include(${CMAKE_SOURCE_DIR}/tools/common.cmake)

# BCH library (for BioHash)
add_library(bch_codec STATIC
    src/3rdparty/bch/bch_codec.c
)
target_include_directories(bch_codec PUBLIC ${PROJECT_ROOT}/src/3rdparty/bch)

# Crypto library (SHA-256 + AES for Fuzzy Commitment)
add_library(crypto STATIC
    src/3rdparty/sha256/sha256.c
    src/3rdparty/tiny-AES-C/aes.c
)
target_include_directories(crypto PUBLIC
    ${PROJECT_ROOT}/src/3rdparty/sha256
    ${PROJECT_ROOT}/src/3rdparty/tiny-AES-C
    ${PROJECT_ROOT}/src/3rdparty/crypto
)
target_compile_definitions(crypto PRIVATE CBC=0 ECB=0 CTR=1)

# httplib include (header-only HTTP client)
include_directories(${PROJECT_ROOT}/src/3rdparty/httplib)
include_directories(${PROJECT_ROOT}/src/3rdparty)

file(GLOB CPP_SOURCES "src/*.cpp")
file(GLOB C_SOURCES "src/*.c")

add_executable(main
    ${CPP_SOURCES}
    ${C_SOURCES}
    ${COMMON_SOURCES} 
    ${OLED_1IN51_SOURCES}
)

target_link_common_libraries(main)
target_link_libraries(main bch_codec crypto)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
print_build_config(${PROJECT_NAME})
message(STATUS "  Sources: ${CPP_SOURCES} ${C_SOURCES}")
message(STATUS "  Output: ${CMAKE_BINARY_DIR}/main")
message(STATUS "===============================================")

