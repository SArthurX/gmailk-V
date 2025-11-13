#define LOG_TAG "SampleFD"
#define LOG_LEVEL LOG_LEVEL_INFO

#include "middleware_utils.h"
#include "sample_utils.h"
#include "vi_vo_utils.h"
#include "src/init.h"

#include <cvi_comm.h>
#include <rtsp.h>
#include "cvi_tdl.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile bool bExit = false;

static cvtdl_face_t g_stFaceMeta = {0};

static uint32_t g_size = 0;

MUTEXAUTOLOCK_INIT(ResultMutex);

typedef struct {
  APP_CONTEXT_S *pstAppCtx;
} SAMPLE_TDL_VENC_THREAD_ARG_S;

void *run_venc(void *args) {
  printf("Enter encoder thread\n");
  SAMPLE_TDL_VENC_THREAD_ARG_S *pstArgs = (SAMPLE_TDL_VENC_THREAD_ARG_S *)args;
  VIDEO_FRAME_INFO_S stFrame;
  CVI_S32 s32Ret;
  cvtdl_face_t stFaceMeta = {0};

  while (bExit == false) {
    s32Ret = CVI_VPSS_GetChnFrame(0, 0, &stFrame, 2000);
    if (s32Ret != CVI_SUCCESS) {
      printf("CVI_VPSS_GetChnFrame chn0 failed with %#x\n", s32Ret);
      break;
    }

    {
      MutexAutoLock(ResultMutex, lock);
      memset(&stFaceMeta, 0, sizeof(cvtdl_face_t));
      if (NULL != g_stFaceMeta.info) {
        CVI_TDL_CopyFaceMeta(&g_stFaceMeta, &stFaceMeta);
      }
      CVI_TDL_Free(&g_stFaceMeta);
    }

    s32Ret = CVI_TDL_Service_FaceDrawRect(pstArgs->pstAppCtx->stServiceHandle, &stFaceMeta, 
                                          &stFrame, false, CVI_TDL_Service_GetDefaultBrush());
    if (s32Ret != CVI_TDL_SUCCESS) {
      CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
      printf("Draw fame fail!, ret=%x\n", s32Ret);
      goto error;
    }

    s32Ret = SAMPLE_TDL_Send_Frame_RTSP(&stFrame, &pstArgs->pstAppCtx->stMWContext);
    if (s32Ret != CVI_SUCCESS) {
      CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
      printf("Send Output Frame NG, ret=%x\n", s32Ret);
      goto error;
    }

  error:
    CVI_TDL_Free(&stFaceMeta);
    CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
    if (s32Ret != CVI_SUCCESS) {
      bExit = true;
    }
  }
  printf("Exit encoder thread\n");
  pthread_exit(NULL);
}

void *run_tdl_thread(void *pHandle) {
  printf("Enter TDL thread\n");
  cvitdl_handle_t pstTDLHandle = (cvitdl_handle_t)pHandle;

  VIDEO_FRAME_INFO_S stFrame;
  cvtdl_face_t stFaceMeta = {0};

  CVI_S32 s32Ret;
  while (bExit == false) {
    s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);

    if (s32Ret != CVI_SUCCESS) {
      printf("CVI_VPSS_GetChnFrame failed with %#x\n", s32Ret);
      goto get_frame_failed;
    }

    memset(&stFaceMeta, 0, sizeof(cvtdl_face_t));
    s32Ret = CVI_TDL_FaceDetection(pstTDLHandle, &stFrame, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE,
                                   &stFaceMeta);
    if (s32Ret != CVI_TDL_SUCCESS) {
      printf("inference failed!, ret=%x\n", s32Ret);
      goto inf_error;
    }

    if (stFaceMeta.size != g_size) {
        printf("face count: %d\n", stFaceMeta.size);
        g_size = stFaceMeta.size;
    }

    {
      MutexAutoLock(ResultMutex, lock);
      memset(&g_stFaceMeta, 0, sizeof(cvtdl_face_t));
      if (NULL != stFaceMeta.info) {
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

  printf("Exit TDL thread\n");
  pthread_exit(NULL);
}

static void SampleHandleSig(CVI_S32 signo) {
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  printf("handle signal, signo: %d\n", signo);
  if (SIGINT == signo || SIGTERM == signo) {
    bExit = true;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf(
        "\nUsage: %s SCRFDFACE_MODEL_PATH.\n\n"
        "\tSCRFDFACE_MODEL_PATH, path to scrfdface model.\n",
        argv[0]);
    return CVI_TDL_FAILURE;
  }

  signal(SIGINT, SampleHandleSig);
  signal(SIGTERM, SampleHandleSig);

  APP_CONTEXT_S stAppCtx = {0};
  CVI_S32 s32Ret = APP_Init(&stAppCtx, argv[1]);
  if (s32Ret != CVI_SUCCESS) {
    printf("Application initialization failed! ret=%x\n", s32Ret);
    return -1;
  }

  pthread_t stVencThread, stTDLThread;
  SAMPLE_TDL_VENC_THREAD_ARG_S args = {
      .pstAppCtx = &stAppCtx,
  };

  pthread_create(&stVencThread, NULL, run_venc, &args);
  pthread_create(&stTDLThread, NULL, run_tdl_thread, stAppCtx.stTDLHandle);
  pthread_join(stVencThread, NULL);
  pthread_join(stTDLThread, NULL);

  APP_Deinit(&stAppCtx);
  return 0;
}
