#include "init.h"
#include <core/utils/vpss_helper.h>
#include <sample_comm.h>
#include <stdio.h>
#include <string.h>

#define GOTO_IF_FAILED(func, ret, label) \
  do { \
    if ((ret = (func)) != CVI_SUCCESS) { \
      printf(#func " failed with %#x!\n", ret); \
      goto label; \
    } \
  } while (0)

CVI_S32 APP_Init(APP_CONTEXT_S *pstAppCtx, const char *pszModelPath) {
  if (NULL == pstAppCtx || NULL == pszModelPath) {
    printf("Invalid parameters\n");
    return CVI_FAILURE;
  }

  memset(pstAppCtx, 0, sizeof(APP_CONTEXT_S));

  SAMPLE_TDL_MW_CONFIG_S stMWConfig = {0};
  CVI_S32 s32Ret = CVI_SUCCESS;

  s32Ret = SAMPLE_TDL_Get_VI_Config(&stMWConfig.stViConfig);
  if (s32Ret != CVI_SUCCESS || stMWConfig.stViConfig.s32WorkingViNum <= 0) {
    printf("Failed to get sensor information from ini file (/mnt/data/sensor_cfg.ini).\n");
    return CVI_FAILURE;
  }

  PIC_SIZE_E enPicSize;
  s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(stMWConfig.stViConfig.astViInfo[0].stSnsInfo.enSnsType,
                                          &enPicSize);
  if (s32Ret != CVI_SUCCESS) {
    printf("Cannot get sensor size\n");
    return CVI_FAILURE;
  }

  SIZE_S stSensorSize;
  s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
  if (s32Ret != CVI_SUCCESS) {
    printf("Cannot get sensor size\n");
    return CVI_FAILURE;
  }

  SIZE_S stVencSize = {
      .u32Width = 1280,
      .u32Height = 720,
  };

  stMWConfig.stVBPoolConfig.u32VBPoolCount = 3;

  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].enFormat = VI_PIXEL_FORMAT;
#ifdef CV180X
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = 2;
#else
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = 5;
#endif
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32Height = stSensorSize.u32Height;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32Width = stSensorSize.u32Width;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].bBind = true;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32VpssChnBinding = VPSS_CHN0;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32VpssGrpBinding = (VPSS_GRP)0;

  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].enFormat = VI_PIXEL_FORMAT;
#ifdef CV180X
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32BlkCount = 2;
#else
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32BlkCount = 5;
#endif
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32Height = stVencSize.u32Height;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32Width = stVencSize.u32Width;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].bBind = true;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32VpssChnBinding = VPSS_CHN1;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32VpssGrpBinding = (VPSS_GRP)0;

  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].enFormat = PIXEL_FORMAT_BGR_888_PLANAR;
#ifdef CV180X
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32BlkCount = 1;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Height = 256;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Width = 320;
#else
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32BlkCount = 3;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Height = 720;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Width = 1280;
#endif
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].bBind = false;

  stMWConfig.stVPSSPoolConfig.u32VpssGrpCount = 1;
#ifndef CV186X
  stMWConfig.stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
  stMWConfig.stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_DUAL;
  stMWConfig.stVPSSPoolConfig.stVpssMode.ViPipe[0] = 0;
  stMWConfig.stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;
  stMWConfig.stVPSSPoolConfig.stVpssMode.ViPipe[1] = 0;
#endif

  SAMPLE_TDL_VPSS_CONFIG_S *pstVpssConfig = &stMWConfig.stVPSSPoolConfig.astVpssConfig[0];
  pstVpssConfig->bBindVI = true;

  VPSS_GRP_DEFAULT_HELPER2(&pstVpssConfig->stVpssGrpAttr, stSensorSize.u32Width,
                           stSensorSize.u32Height, VI_PIXEL_FORMAT, 1);
  pstVpssConfig->u32ChnCount = 2;
  pstVpssConfig->u32ChnBindVI = 0;
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[0], stVencSize.u32Width,
                          stVencSize.u32Height, VI_PIXEL_FORMAT, true);
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[1], stVencSize.u32Width,
                          stVencSize.u32Height, VI_PIXEL_FORMAT, true);

  SAMPLE_TDL_Get_Input_Config(&stMWConfig.stVencConfig.stChnInputCfg);
  stMWConfig.stVencConfig.u32FrameWidth = stVencSize.u32Width;
  stMWConfig.stVencConfig.u32FrameHeight = stVencSize.u32Height;

  SAMPLE_TDL_Get_RTSP_Config(&stMWConfig.stRTSPConfig.stRTSPConfig);

  s32Ret = SAMPLE_TDL_Init_WM(&stMWConfig, &pstAppCtx->stMWContext);
  if (s32Ret != CVI_SUCCESS) {
    printf("init middleware failed! ret=%x\n", s32Ret);
    return CVI_FAILURE;
  }

  GOTO_IF_FAILED(CVI_TDL_CreateHandle2(&pstAppCtx->stTDLHandle, 1, 0), s32Ret, create_tdl_fail);

  GOTO_IF_FAILED(CVI_TDL_SetVBPool(pstAppCtx->stTDLHandle, 0, 2), s32Ret, create_service_fail);

  CVI_TDL_SetVpssTimeout(pstAppCtx->stTDLHandle, 1000);

  GOTO_IF_FAILED(CVI_TDL_Service_CreateHandle(&pstAppCtx->stServiceHandle, pstAppCtx->stTDLHandle),
                 s32Ret, create_service_fail);

  GOTO_IF_FAILED(CVI_TDL_OpenModel(pstAppCtx->stTDLHandle, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE,
                                   pszModelPath),
                 s32Ret, setup_tdl_fail);

  printf("Application initialized successfully\n");
  return CVI_SUCCESS;

setup_tdl_fail:
  CVI_TDL_Service_DestroyHandle(pstAppCtx->stServiceHandle);
  pstAppCtx->stServiceHandle = NULL;
create_service_fail:
  CVI_TDL_DestroyHandle(pstAppCtx->stTDLHandle);
  pstAppCtx->stTDLHandle = NULL;
create_tdl_fail:
  SAMPLE_TDL_Destroy_MW(&pstAppCtx->stMWContext);
  return s32Ret;
}

void APP_Deinit(APP_CONTEXT_S *pstAppCtx) {
  if (NULL == pstAppCtx) {
    return;
  }

  if (pstAppCtx->stServiceHandle) {
    CVI_TDL_Service_DestroyHandle(pstAppCtx->stServiceHandle);
    pstAppCtx->stServiceHandle = NULL;
  }

  if (pstAppCtx->stTDLHandle) {
    CVI_TDL_DestroyHandle(pstAppCtx->stTDLHandle);
    pstAppCtx->stTDLHandle = NULL;
  }

  SAMPLE_TDL_Destroy_MW(&pstAppCtx->stMWContext);

  printf("Application deinitialized\n");
}
