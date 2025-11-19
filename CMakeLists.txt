cmake_minimum_required(VERSION 3.10)
project(gmailk-V)

# Project configuration - set CHIP before project()
if(NOT DEFINED CHIP)
    set(CHIP "CV181X" CACHE STRING "Target chip: CV181X or CV180X")
endif()

if(NOT DEFINED BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type: Debug or Release")
endif()


# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Chip-specific configurations
if(CHIP STREQUAL "CV181X")
    add_compile_definitions(CV181X __CV181X__)
    set(IVE_LIB cvi_ive)
    set(CHIP_ARCH "cv181x_riscv64")
    set(SYSTEM_ARCH "musl_riscv64")
elseif(CHIP STREQUAL "CV180X")
    add_compile_definitions(CV180X __CV180X__ USE_TPU_IVE)
    set(IVE_LIB cvi_ive_tpu)
    set(CHIP_ARCH "cv180x_riscv64")
    set(SYSTEM_ARCH "musl_riscv64")
else()
    message(FATAL_ERROR "CHIP ${CHIP} is not supported. Use CV181X or CV180X")
endif()


set(SYSTEM_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/system/${SYSTEM_ARCH}")
set(TDL_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/tdl/${CHIP_ARCH}")
link_directories(
    ${SYSTEM_LIB_DIR}
    ${TDL_LIB_DIR}
)

# Compiler flags
set(COMMON_FLAGS "-fsigned-char -Wno-format-truncation -fdiagnostics-color=always")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")

# Strip symbols in release build
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s")
endif()

# OpenCV and NCNN precompiled library paths
set(OPENCV_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/opencv/build/install/lib")
set(OPENCV_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/lib/opencv/build/install/include")
set(NCNN_LIB_DIR "${CMAKE_SOURCE_DIR}/lib/ncnn/build/install/lib")
set(NCNN_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/lib/ncnn/build/install/include")

# Add library directories
link_directories(
    ${OPENCV_LIB_DIR}
    ${NCNN_LIB_DIR}
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/system
    ${CMAKE_SOURCE_DIR}/include/system/linux
    ${CMAKE_SOURCE_DIR}/include/tdl
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/src/3rdparty
    ${OPENCV_INCLUDE_DIR}
    ${OPENCV_INCLUDE_DIR}/opencv4
    ${NCNN_INCLUDE_DIR}
    ${NCNN_INCLUDE_DIR}/ncnn
)

# Source files
set(COMMON_SOURCES
    common/middleware_utils.c
)

file(GLOB CPP_SOURCES "src/*.cpp")
file(GLOB C_SOURCES "src/*.c")

# Create executable
add_executable(main
    ${CPP_SOURCES}
    ${C_SOURCES}
    ${COMMON_SOURCES}
)

# Link libraries
target_link_libraries(main
    # System libraries
    pthread
    atomic
    
    # IVE library (chip-specific)
    ${IVE_LIB}
    
    # Video and ISP libraries
    ini
    sns_full
    sample
    isp
    vdec
    venc
    awb
    ae
    af
    cvi_bin
    cvi_bin_isp
    misc
    isp_algo
    sys
    vi
    vo
    vpss
    rgn
    gdc
    
    # TDL library
    cvi_tdl
    
    # OpenCV libraries
    opencv_core
    opencv_imgproc
    opencv_imgcodecs
    
    # NCNN library
    ncnn
    
    # CVI kernel libraries
    cvikernel
    cvimath
    cviruntime
    
    # RTSP library
    cvi_rtsp
    
    # WiringX library
    wiringx
)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Print configuration information
message(STATUS "===============================================")
message(STATUS "Build Configuration:")
message(STATUS "  Project: ${PROJECT_NAME}")
message(STATUS "  Chip: ${CHIP}")
message(STATUS "  Architecture: ${CHIP_ARCH}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  System Lib: ${SYSTEM_LIB_DIR}")
message(STATUS "  TDL Lib: ${TDL_LIB_DIR}")
message(STATUS "===============================================")
