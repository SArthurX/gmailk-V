if(NOT DEFINED CHIP)
    set(CHIP "CV181X" CACHE STRING "Target chip: CV181X or CV180X")
endif()

if(NOT DEFINED CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type: Debug or Release")
endif()


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


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

if(NOT DEFINED PROJECT_ROOT)
    get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

set(SYSTEM_LIB_DIR "${PROJECT_ROOT}/lib/system/${SYSTEM_ARCH}")
set(TDL_LIB_DIR "${PROJECT_ROOT}/lib/tdl/${CHIP_ARCH}")
link_directories(
    ${SYSTEM_LIB_DIR}
    ${TDL_LIB_DIR}
)

set(COMMON_FLAGS "-fsigned-char -Wno-format-truncation -fdiagnostics-color=always")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -s")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s")
endif()

set(OPENCV_LIB_DIR "${PROJECT_ROOT}/lib/opencv/build/install/lib")
set(OPENCV_INCLUDE_DIR "${PROJECT_ROOT}/lib/opencv/build/install/include")
set(NCNN_LIB_DIR "${PROJECT_ROOT}/lib/ncnn/build/install/lib")
set(NCNN_INCLUDE_DIR "${PROJECT_ROOT}/lib/ncnn/build/install/include")

link_directories(
    ${OPENCV_LIB_DIR}
    ${NCNN_LIB_DIR}
)

include_directories(
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/include/system
    ${PROJECT_ROOT}/include/system/linux
    ${PROJECT_ROOT}/include/tdl
    ${PROJECT_ROOT}/common
    ${PROJECT_ROOT}/src/3rdparty
    ${OPENCV_INCLUDE_DIR}
    ${OPENCV_INCLUDE_DIR}/opencv4
    ${NCNN_INCLUDE_DIR}
    ${NCNN_INCLUDE_DIR}/ncnn
)

set(COMMON_SOURCES
    ${PROJECT_ROOT}/common/middleware_utils.c
    ${PROJECT_ROOT}/common/sample_utils.c
)

function(target_link_common_libraries target_name)
    target_link_libraries(${target_name}
        pthread
        atomic
        
       
        ${IVE_LIB}

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
        
        cvi_tdl
        
        opencv_core
        opencv_imgproc
        opencv_imgcodecs
        ncnn
 
        cvikernel
        cvimath
        cviruntime
        
        cvi_rtsp
        
        wiringx
    )
endfunction()

function(print_build_config project_name)
    message(STATUS "===============================================")
    message(STATUS "Build Configuration:")
    message(STATUS "  Project: ${project_name}")
    message(STATUS "  Chip: ${CHIP}")
    message(STATUS "  Architecture: ${CHIP_ARCH}")
    message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
    message(STATUS "  C Compiler: ${CMAKE_C_COMPILER}")
    message(STATUS "  CXX Compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "  System Lib: ${SYSTEM_LIB_DIR}")
    message(STATUS "  TDL Lib: ${TDL_LIB_DIR}")
    message(STATUS "  Project Root: ${PROJECT_ROOT}")
    message(STATUS "===============================================")
endfunction()
