#define LOG_TAG "SampleFD"
#define LOG_LEVEL LOG_LEVEL_INFO

// C++ headers
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <atomic>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
// C system headers with extern "C" linkage

extern "C" {
#include <cvi_comm.h>
#include <rtsp.h>
#include <sample_comm.h>
#include <core/utils/vpss_helper.h>
}

// C++ compatible TDL headers
#include "cvi_tdl.h"

// C utility headers with extern "C" linkage
extern "C" {
#include "middleware_utils.h"
#include "sample_utils.h"
#include "vi_vo_utils.h"
}
#include <fstream>

void *run_venc(void *args);
void *run_tdl_thread(void *pHandle);

// 使用 C++ 原子類型替代 volatile bool
static std::atomic<bool> bExit(false);

static cvtdl_face_t g_stFaceMeta = {0};

static uint32_t g_size = 0;

MUTEXAUTOLOCK_INIT(ResultMutex);

struct SampleTdlVencThreadArg {
  SAMPLE_TDL_MW_CONTEXT *pstMWContext;
  cvitdl_service_handle_t stServiceHandle;
};

static void SampleHandleSig(CVI_S32 signo) {
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  std::cout << "handle signal, signo: " << signo << std::endl;
  if (SIGINT == signo || SIGTERM == signo) {
    bExit = true;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "\nUsage: " << argv[0] << " SCRFDFACE_MODEL_PATH.\n\n"
              << "\tSCRFDFACE_MODEL_PATH, path to scrfdface model.\n" << std::endl;
    return CVI_TDL_FAILURE;
  }

  signal(SIGINT, SampleHandleSig);
  signal(SIGTERM, SampleHandleSig);

  SAMPLE_TDL_MW_CONFIG_S stMWConfig;
  std::memset(&stMWConfig, 0, sizeof(stMWConfig));

  CVI_S32 s32Ret = SAMPLE_TDL_Get_VI_Config(&stMWConfig.stViConfig);

  std::cout << "test:" << stMWConfig.stViConfig.s32WorkingViNum << std::endl;
  if (s32Ret != CVI_SUCCESS || stMWConfig.stViConfig.s32WorkingViNum <= 0) {
    std::cerr << "Failed to get senor infomation from ini file (/mnt/data/sensor_cfg.ini)." << std::endl;
    return -1;
  }

  // Get VI size
  PIC_SIZE_E enPicSize;
  s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(stMWConfig.stViConfig.astViInfo[0].stSnsInfo.enSnsType,
                                          &enPicSize);
  if (s32Ret != CVI_SUCCESS) {
    std::cerr << "Cannot get senor size" << std::endl;
    return -1;
  }

  SIZE_S stSensorSize;
  s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
  if (s32Ret != CVI_SUCCESS) {
    std::cerr << "Cannot get senor size" << std::endl;
    return -1;
  }

  // Setup frame size of video encoder to 1080p
  // SIZE_S stVencSize = {
  //     .u32Width = 1280,
  //     .u32Height = 720,
  // };
  SIZE_S stVencSize = {
      .u32Width = 1920,
      .u32Height = 1080,
  };

  stMWConfig.stVBPoolConfig.u32VBPoolCount = 3;

  // VBPool 0 for VPSS Grp0 Chn0
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].enFormat = VI_PIXEL_FORMAT;

  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = 5;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32Height = stSensorSize.u32Height;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32Width = stSensorSize.u32Width;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].bBind = true;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32VpssChnBinding = VPSS_CHN0;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[0].u32VpssGrpBinding = (VPSS_GRP)0;

  // VBPool 1 for VPSS Grp0 Chn1
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].enFormat = VI_PIXEL_FORMAT;

  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32BlkCount = 5;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32Height = stVencSize.u32Height;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32Width = stVencSize.u32Width;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].bBind = true;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32VpssChnBinding = VPSS_CHN1;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[1].u32VpssGrpBinding = (VPSS_GRP)0;

  // VBPool 2 for TDL preprocessing
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].enFormat = PIXEL_FORMAT_BGR_888_PLANAR;

  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32BlkCount = 3;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Height = 1080;
  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].u32Width = 1920;

  stMWConfig.stVBPoolConfig.astVBPoolSetup[2].bBind = false;

  // Setup VPSS Grp0
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

  // Assign device 1 to VPSS Grp0, because device1 has 3 outputs in dual mode.
  VPSS_GRP_DEFAULT_HELPER2(&pstVpssConfig->stVpssGrpAttr, stSensorSize.u32Width,
                           stSensorSize.u32Height, VI_PIXEL_FORMAT, 1);
  pstVpssConfig->u32ChnCount = 2;
  pstVpssConfig->u32ChnBindVI = 0;
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[0], stVencSize.u32Width,
                          stVencSize.u32Height, VI_PIXEL_FORMAT, true);
  VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[1], stVencSize.u32Width,
                          stVencSize.u32Height, VI_PIXEL_FORMAT, true);

  // Get default VENC configurations
  SAMPLE_TDL_Get_Input_Config(&stMWConfig.stVencConfig.stChnInputCfg);
  stMWConfig.stVencConfig.u32FrameWidth = stVencSize.u32Width;
  stMWConfig.stVencConfig.u32FrameHeight = stVencSize.u32Height;

  // Get default RTSP configurations
  SAMPLE_TDL_Get_RTSP_Config(&stMWConfig.stRTSPConfig.stRTSPConfig);

  SAMPLE_TDL_MW_CONTEXT stMWContext;
  std::memset(&stMWContext, 0, sizeof(stMWContext));
  s32Ret = SAMPLE_TDL_Init_WM(&stMWConfig, &stMWContext);
  if (s32Ret != CVI_SUCCESS) {
    std::cerr << "init middleware failed! ret=" << std::hex << s32Ret << std::endl;
    return -1;
  }

  // Declare all variables before any goto labels
  cvitdl_handle_t stTDLHandle = nullptr;
  cvitdl_service_handle_t stServiceHandle = nullptr;
  pthread_t stVencThread, stTDLThread;
  SampleTdlVencThreadArg args;

  // Create TDL handle and assign VPSS Grp1 Device 0 to TDL SDK
  GOTO_IF_FAILED(CVI_TDL_CreateHandle2(&stTDLHandle, 1, 0), s32Ret, create_tdl_fail);

  GOTO_IF_FAILED(CVI_TDL_SetVBPool(stTDLHandle, 0, 2), s32Ret, create_service_fail);

  CVI_TDL_SetVpssTimeout(stTDLHandle, 1000);

  GOTO_IF_FAILED(CVI_TDL_Service_CreateHandle(&stServiceHandle, stTDLHandle), s32Ret,
                 create_service_fail);

  GOTO_IF_FAILED(CVI_TDL_OpenModel(stTDLHandle, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, argv[1]), s32Ret,
                 setup_tdl_fail);

  args.pstMWContext = &stMWContext;
  args.stServiceHandle = stServiceHandle;

  pthread_create(&stVencThread, nullptr, run_venc, &args);
  pthread_create(&stTDLThread, nullptr, run_tdl_thread, stTDLHandle);

  pthread_join(stVencThread, nullptr);
  pthread_join(stTDLThread, nullptr);

setup_tdl_fail:
  CVI_TDL_Service_DestroyHandle(stServiceHandle);
create_service_fail:
  CVI_TDL_DestroyHandle(stTDLHandle);
create_tdl_fail:
  SAMPLE_TDL_Destroy_MW(&stMWContext);

  return 0;
}






void *run_venc(void *args) {
  std::cout << "Enter encoder thread" << std::endl;
  SampleTdlVencThreadArg *pstArgs = static_cast<SampleTdlVencThreadArg *>(args);
  VIDEO_FRAME_INFO_S stFrame;
  CVI_S32 s32Ret;
  cvtdl_face_t stFaceMeta = {0};

  while (!bExit) {
    s32Ret = CVI_VPSS_GetChnFrame(0, 0, &stFrame, 2000);
    if (s32Ret != CVI_SUCCESS) {
      std::cerr << "CVI_VPSS_GetChnFrame chn0 failed with " << std::hex << s32Ret << std::endl;
      break;
    }

    {
      MutexAutoLock(ResultMutex, lock);
      std::memset(&stFaceMeta, 0, sizeof(cvtdl_face_t));
      if (g_stFaceMeta.info != nullptr) {
        CVI_TDL_CopyFaceMeta(&g_stFaceMeta, &stFaceMeta);
      }
      CVI_TDL_Free(&g_stFaceMeta);
    }

    s32Ret = CVI_TDL_Service_FaceDrawRect(pstArgs->stServiceHandle, &stFaceMeta, &stFrame, false,
                                          CVI_TDL_Service_GetDefaultBrush());
    if (s32Ret != CVI_TDL_SUCCESS) {
      CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
      std::cerr << "Draw fame fail!, ret=" << std::hex << s32Ret << std::endl;
      goto error;
    }

    s32Ret = SAMPLE_TDL_Send_Frame_RTSP(&stFrame, pstArgs->pstMWContext);
    if (s32Ret != CVI_SUCCESS) {
      CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
      std::cerr << "Send Output Frame NG, ret=" << std::hex << s32Ret << std::endl;
      goto error;
    }

  error:
    CVI_TDL_Free(&stFaceMeta);
    CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
    if (s32Ret != CVI_SUCCESS) {
      bExit = true;
    }
  }
  std::cout << "Exit encoder thread" << std::endl;
  pthread_exit(nullptr);
}

CVI_S32 CVI_TDL_DumpVpssFrame_a(const char *filepath, VIDEO_FRAME_INFO_S *frame) {
  VIDEO_FRAME_S *pstVFSrc = &frame->stVFrame;
  int channels = 1;
  int num_pixels = 1;
  switch (pstVFSrc->enPixelFormat) {
    case PIXEL_FORMAT_YUV_400: {
      channels = 1;
    } break;
    case PIXEL_FORMAT_RGB_888_PLANAR: {
      channels = 3;
    } break;
    case PIXEL_FORMAT_RGB_888: {
      channels = 1;
      num_pixels = 3;
    } break;
    case PIXEL_FORMAT_NV21: {
      channels = 2;
    } break;
    default: {
      printf("Unsupported conversion type: %u.\n", pstVFSrc->enPixelFormat);
      return CVI_FAILURE;
    } break;
  }

  FILE *fp = fopen(filepath, "wb");
  if (fp == nullptr) {
    printf("failed to open: %s.\n", filepath);
    return CVI_FAILURE;
  }
  uint32_t width = pstVFSrc->u32Width;
  uint32_t height = pstVFSrc->u32Height;
  fwrite(&width, sizeof(uint32_t), 1, fp);
  fwrite(&height, sizeof(uint32_t), 1, fp);
  uint32_t pix_format = (uint32_t)pstVFSrc->enPixelFormat;
  fwrite(&pix_format, sizeof(pix_format), 1, fp);
  for (int c = 0; c < channels; c++) {
    uint8_t *ptr_plane = pstVFSrc->pu8VirAddr[c];
    if (width * num_pixels == pstVFSrc->u32Stride[c]) {
      fwrite(pstVFSrc->pu8VirAddr[c], pstVFSrc->u32Stride[c] * height, 1, fp);
    } else {
      for (uint32_t r = 0; r < height; r++) {
        fwrite(ptr_plane, width * num_pixels, 1, fp);
        ptr_plane += pstVFSrc->u32Stride[c];
      }
    }
  }
  fclose(fp);
  return CVI_SUCCESS;
}

void *run_tdl_thread(void *pHandle) {
  std::cout << "Enter TDL thread" << std::endl;
  cvitdl_handle_t pstTDLHandle = static_cast<cvitdl_handle_t>(pHandle);

  VIDEO_FRAME_INFO_S stFrame;
  cvtdl_face_t stFaceMeta = {0};
  struct timeval t0, t1;
  unsigned long execution_time;

  CVI_S32 s32Ret;
  while (!bExit) {
    s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);
    if (s32Ret == CVI_SUCCESS ) {
      // 輸出畫面資訊
      std::cout << "=== Frame Information ===" << std::endl;
      std::cout << "Width: " << stFrame.stVFrame.u32Width << std::endl;
      std::cout << "Height: " << stFrame.stVFrame.u32Height << std::endl;
      std::cout << "Pixel Format: " << stFrame.stVFrame.enPixelFormat << std::endl;
      std::cout << "Stride[0]: " << stFrame.stVFrame.u32Stride[0] << std::endl;
      std::cout << "Stride[1]: " << stFrame.stVFrame.u32Stride[1] << std::endl;
      std::cout << "Stride[2]: " << stFrame.stVFrame.u32Stride[2] << std::endl;
      std::cout << "PhyAddr[0]: 0x" << std::hex << stFrame.stVFrame.u64PhyAddr[0] << std::endl;
      std::cout << "PhyAddr[1]: 0x" << std::hex << stFrame.stVFrame.u64PhyAddr[1] << std::endl;
      std::cout << "PhyAddr[2]: 0x" << std::hex << stFrame.stVFrame.u64PhyAddr[2] << std::endl;
      std::cout << "Length[0]: " << std::dec << stFrame.stVFrame.u32Length[0] << std::endl;
        std::cout << "=========================" << std::endl;

bool bNeedUnmap = false;
      if (stFrame.stVFrame.pu8VirAddr[0] == NULL) {
          // 1. Mmap - 取得虛擬地址
          stFrame.stVFrame.pu8VirAddr[0] = (CVI_U8 *)CVI_SYS_Mmap(
              stFrame.stVFrame.u64PhyAddr[0], stFrame.stVFrame.u32Length[0]);
          stFrame.stVFrame.pu8VirAddr[1] = (CVI_U8 *)CVI_SYS_Mmap(
              stFrame.stVFrame.u64PhyAddr[1], stFrame.stVFrame.u32Length[1]);
          bNeedUnmap = true;
      }

      // 2. (!!! 關鍵修正 !!!) 刷新 CPU 快取
      // 告訴 CPU，這兩塊記憶體已被硬體更新，請拋棄快取
      std::cout << "Flushing cache for Plane 0..." << std::endl;
      CVI_SYS_IonFlushCache(stFrame.stVFrame.u64PhyAddr[0], 
                            stFrame.stVFrame.pu8VirAddr[0], 
                            stFrame.stVFrame.u32Length[0]);
                            
      std::cout << "Flushing cache for Plane 1..." << std::endl;
      CVI_SYS_IonFlushCache(stFrame.stVFrame.u64PhyAddr[1], 
                            stFrame.stVFrame.pu8VirAddr[1], 
                            stFrame.stVFrame.u32Length[1]);

      std::cout << "=== Dumping First Frame ===" << std::endl;

      // 3. Dump - 現在才從記憶體讀取
      CVI_S32 dumpRet = CVI_TDL_DumpVpssFrame("lol.bin", &stFrame);
      if (dumpRet == CVI_SUCCESS) {
        std::cout << "Successfully dumped first frame to lol.bin" << std::endl;
      } else {
        std::cerr << "Failed to dump first frame!" << std::endl;
      }

      // 4. Munmap - 解除映射
      if (bNeedUnmap) {
          CVI_SYS_Munmap((void *)stFrame.stVFrame.pu8VirAddr[0], stFrame.stVFrame.u32Length[0]);
          CVI_SYS_Munmap((void *)stFrame.stVFrame.pu8VirAddr[1], stFrame.stVFrame.u32Length[1]);
          stFrame.stVFrame.pu8VirAddr[0] = NULL;
          stFrame.stVFrame.pu8VirAddr[1] = NULL;
      }
    }


    if (s32Ret != CVI_SUCCESS) {
      std::cerr << "CVI_VPSS_GetChnFrame failed with " << std::hex << s32Ret << std::endl;
      goto get_frame_failed;
    }

    std::memset(&stFaceMeta, 0, sizeof(cvtdl_face_t));
    
    gettimeofday(&t0, NULL);
    s32Ret = CVI_TDL_FaceDetection(pstTDLHandle, &stFrame, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE,
                                   &stFaceMeta);
    gettimeofday(&t1, NULL);
    
    if (s32Ret != CVI_TDL_SUCCESS) {
      std::cerr << "inference failed!, ret=" << std::hex << s32Ret << std::endl;
      goto inf_error;
    }

    execution_time = ((t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec);
    
    // 輸出人臉檢測結果
    if (stFaceMeta.size > 0) {
      std::cout << "=== Face Detection Results ===" << std::endl;
      std::cout << "Face count: " << stFaceMeta.size << std::endl;
      std::cout << "Inference time: " << (float)execution_time / 1000 << " ms" << std::endl;
      std::cout << "Frame size: " << stFrame.stVFrame.u32Width << "x" 
                << stFrame.stVFrame.u32Height << std::endl;
      
      for (uint32_t i = 0; i < stFaceMeta.size; i++) {
        std::cout << "Face[" << i << "] bbox: "
                  << "x1=" << stFaceMeta.info[i].bbox.x1 << ", "
                  << "y1=" << stFaceMeta.info[i].bbox.y1 << ", "
                  << "x2=" << stFaceMeta.info[i].bbox.x2 << ", "
                  << "y2=" << stFaceMeta.info[i].bbox.y2 << ", "
                  << "score=" << stFaceMeta.info[i].bbox.score << std::endl;
      }
      std::cout << "=============================" << std::endl;
    } else if (stFaceMeta.size != g_size) {
      std::cout << "No face detected" << std::endl;
    }
    
    g_size = stFaceMeta.size;

    {
      MutexAutoLock(ResultMutex, lock);
      std::memset(&g_stFaceMeta, 0, sizeof(cvtdl_face_t));
      if (stFaceMeta.info != nullptr) {
        CVI_TDL_CopyFaceMeta(&stFaceMeta, &g_stFaceMeta);
      }
    }

  inf_error:
    CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
  get_frame_failed:
    CVI_TDL_Free(&stFaceMeta);
    if (s32Ret != CVI_SUCCESS) {
      bExit = true;
    }
  }

  std::cout << "Exit TDL thread" << std::endl;
  pthread_exit(nullptr);
}

