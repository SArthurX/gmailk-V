#include <iostream>
#include <cstring>
#include "system_init.h"
#include "sample_utils.h"

extern "C" {
#include <core/utils/vpss_helper.h>
#include "vi_vo_utils.h"
}



CVI_S32 SystemInit_GetSensorConfig(SystemConfig_t *pstConfig) {
    std::memset(&pstConfig->stMWConfig, 0, sizeof(pstConfig->stMWConfig));
    
    CVI_S32 s32Ret = SAMPLE_TDL_Get_VI_Config(&pstConfig->stMWConfig.stViConfig);
    
    std::cout << "Working VI Num: " << pstConfig->stMWConfig.stViConfig.s32WorkingViNum << std::endl;
    
    if (s32Ret != CVI_SUCCESS || pstConfig->stMWConfig.stViConfig.s32WorkingViNum <= 0) {
        std::cerr << "Failed to get sensor information from ini file (/mnt/data/sensor_cfg.ini)." << std::endl;
        return CVI_FAILURE;
    }
    
    // Get VI size
    PIC_SIZE_E enPicSize;
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
        pstConfig->stMWConfig.stViConfig.astViInfo[0].stSnsInfo.enSnsType,
        &enPicSize);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Cannot get sensor size" << std::endl;
        return CVI_FAILURE;
    }
    
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &pstConfig->stSensorSize);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Cannot get sensor size" << std::endl;
        return CVI_FAILURE;
    }
    
    // Setup frame size of video encoder to 1080p
    pstConfig->stVencSize.u32Width = 1920;
    pstConfig->stVencSize.u32Height = 1080;
    
    std::cout << "Sensor size: " << pstConfig->stSensorSize.u32Width << "x" 
              << pstConfig->stSensorSize.u32Height << std::endl;
    std::cout << "VENC size: " << pstConfig->stVencSize.u32Width << "x" 
              << pstConfig->stVencSize.u32Height << std::endl;
    
    return CVI_SUCCESS;
}

CVI_S32 SystemInit_SetupVBPool(SystemConfig_t *pstConfig) {
    pstConfig->stMWConfig.stVBPoolConfig.u32VBPoolCount = 3;
    
    // VBPool 0 for VPSS Grp0 Chn0
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[0].enFormat = VI_PIXEL_FORMAT;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = 5;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32Height = pstConfig->stSensorSize.u32Height;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32Width = pstConfig->stSensorSize.u32Width;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[0].bBind = true;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32VpssChnBinding = VPSS_CHN0;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32VpssGrpBinding = (VPSS_GRP)0;
    
    // VBPool 1 for VPSS Grp0 Chn1
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[1].enFormat = VI_PIXEL_FORMAT;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32BlkCount = 5;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32Height = pstConfig->stVencSize.u32Height;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32Width = pstConfig->stVencSize.u32Width;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[1].bBind = true;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32VpssChnBinding = VPSS_CHN1;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32VpssGrpBinding = (VPSS_GRP)0;
    
    // VBPool 2 for TDL preprocessing
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[2].enFormat = PIXEL_FORMAT_BGR_888_PLANAR;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32BlkCount = 3;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Height = 1080;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Width = 1920;
    pstConfig->stMWConfig.stVBPoolConfig.astVBPoolSetup[2].bBind = false;
    
    std::cout << "VBPool configured: 3 pools" << std::endl;
    return CVI_SUCCESS;
}

CVI_S32 SystemInit_SetupVPSS(SystemConfig_t *pstConfig) {
    pstConfig->stMWConfig.stVPSSPoolConfig.u32VpssGrpCount = 1;
    
#ifndef CV186X
    pstConfig->stMWConfig.stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
    pstConfig->stMWConfig.stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_DUAL;
    pstConfig->stMWConfig.stVPSSPoolConfig.stVpssMode.ViPipe[0] = 0;
    pstConfig->stMWConfig.stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;
    pstConfig->stMWConfig.stVPSSPoolConfig.stVpssMode.ViPipe[1] = 0;
#endif
    
    SAMPLE_TDL_VPSS_CONFIG_S *pstVpssConfig = &pstConfig->stMWConfig.stVPSSPoolConfig.astVpssConfig[0];
    pstVpssConfig->bBindVI = true;
    
    // Assign device 1 to VPSS Grp0
    VPSS_GRP_DEFAULT_HELPER2(&pstVpssConfig->stVpssGrpAttr, 
                             pstConfig->stSensorSize.u32Width,
                             pstConfig->stSensorSize.u32Height, 
                             VI_PIXEL_FORMAT, 1);
    
    pstVpssConfig->u32ChnCount = 2;
    pstVpssConfig->u32ChnBindVI = 0;
    
    VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[0], 
                            pstConfig->stVencSize.u32Width,
                            pstConfig->stVencSize.u32Height, 
                            VI_PIXEL_FORMAT, true);
    
    VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[1], 
                            pstConfig->stVencSize.u32Width,
                            pstConfig->stVencSize.u32Height, 
                            VI_PIXEL_FORMAT, true);
    
    std::cout << "VPSS configured: 1 group, 2 channels" << std::endl;
    return CVI_SUCCESS;
}

CVI_S32 SystemInit_SetupVENC(SystemConfig_t *pstConfig) {
    SAMPLE_TDL_Get_Input_Config(&pstConfig->stMWConfig.stVencConfig.stChnInputCfg);
    pstConfig->stMWConfig.stVencConfig.u32FrameWidth = pstConfig->stVencSize.u32Width;
    pstConfig->stMWConfig.stVencConfig.u32FrameHeight = pstConfig->stVencSize.u32Height;
    
    std::cout << "VENC configured: " << pstConfig->stVencSize.u32Width << "x" 
              << pstConfig->stVencSize.u32Height << std::endl;
    return CVI_SUCCESS;
}

CVI_S32 SystemInit_SetupRTSP(SystemConfig_t *pstConfig) {
    SAMPLE_TDL_Get_RTSP_Config(&pstConfig->stMWConfig.stRTSPConfig.stRTSPConfig);
    std::cout << "RTSP configured" << std::endl;
    return CVI_SUCCESS;
}

CVI_S32 SystemInit_InitMiddleware(SystemConfig_t *pstConfig, SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
    std::memset(pstMWContext, 0, sizeof(SAMPLE_TDL_MW_CONTEXT));
    
    CVI_S32 s32Ret = SAMPLE_TDL_Init_WM(&pstConfig->stMWConfig, pstMWContext);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Init middleware failed! ret=0x" << std::hex << s32Ret << std::endl;
        return CVI_FAILURE;
    }
    
    std::cout << "Middleware initialized successfully" << std::endl;
    return CVI_SUCCESS;
}

CVI_S32 SystemInit_All(SystemConfig_t *pstConfig, SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
    CVI_S32 s32Ret;
    
    std::cout << "=== Starting System Initialization ===" << std::endl;
    
    // 1. Get sensor configuration
    s32Ret = SystemInit_GetSensorConfig(pstConfig);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed at GetSensorConfig" << std::endl;
        return s32Ret;
    }
    
    // 2. Setup VBPool
    s32Ret = SystemInit_SetupVBPool(pstConfig);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed at SetupVBPool" << std::endl;
        return s32Ret;
    }
    
    // 3. Setup VPSS
    s32Ret = SystemInit_SetupVPSS(pstConfig);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed at SetupVPSS" << std::endl;
        return s32Ret;
    }
    
    // 4. Setup VENC
    s32Ret = SystemInit_SetupVENC(pstConfig);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed at SetupVENC" << std::endl;
        return s32Ret;
    }
    
    // 5. Setup RTSP
    s32Ret = SystemInit_SetupRTSP(pstConfig);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed at SetupRTSP" << std::endl;
        return s32Ret;
    }
    
    // 6. Initialize middleware
    s32Ret = SystemInit_InitMiddleware(pstConfig, pstMWContext);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed at InitMiddleware" << std::endl;
        return s32Ret;
    }
    
    std::cout << "=== System Initialization Complete ===" << std::endl;
    return CVI_SUCCESS;
}

void SystemInit_Cleanup(SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
    SAMPLE_TDL_Destroy_MW(pstMWContext);
    std::cout << "System resources cleaned up" << std::endl;
}
