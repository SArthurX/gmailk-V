#define LOG_TAG "SampleFD"
#define LOG_LEVEL LOG_LEVEL_INFO

#include <iostream>
#include <signal.h>
#include <pthread.h>

#include "shared_data.h"
#include "system_init.h"
#include "tdl_handler.h"
#include "venc_handler.h"

static void SampleHandleSig(CVI_S32 signo) {
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  std::cout << "Handle signal, signo: " << signo << std::endl;
  if (SIGINT == signo || SIGTERM == signo) {
    g_bExit = true;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "\nUsage: " << argv[0] << " SCRFDFACE_MODEL_PATH.\n\n"
              << "\tSCRFDFACE_MODEL_PATH, path to scrfdface model.\n" << std::endl;
    return -1;
  }

  signal(SIGINT, SampleHandleSig);
  signal(SIGTERM, SampleHandleSig);

  SharedData_Init();

  SystemConfig_t stSystemConfig;
  SAMPLE_TDL_MW_CONTEXT stMWContext;

  CVI_S32 s32Ret = SystemInit_All(&stSystemConfig, &stMWContext);
  if (s32Ret != CVI_SUCCESS) {
    std::cerr << "System initialization failed!" << std::endl;
    SharedData_Cleanup();
    return -1;
  }

  TDLHandler_t stTDLHandler;
  s32Ret = TDLHandler_Init(&stTDLHandler, argv[1]);
  if (s32Ret != CVI_SUCCESS) {
    std::cerr << "TDL initialization failed!" << std::endl;
    SystemInit_Cleanup(&stMWContext);
    SharedData_Cleanup();
    return -1;
  }

  VENCHandler_t stVencArgs;
  stVencArgs.pstMWContext = &stMWContext;
  stVencArgs.pstTDLHandler = &stTDLHandler;

  pthread_t stVencThread, stTDLThread;
  pthread_create(&stVencThread, nullptr, VENCHandler_ThreadRoutine, &stVencArgs);
  pthread_create(&stTDLThread, nullptr, TDLHandler_ThreadRoutine, &stTDLHandler);

  std::cout << "=== Face Detection Application Started ===" << std::endl;
  std::cout << "Press Ctrl+C to stop..." << std::endl;

  pthread_join(stVencThread, nullptr);
  pthread_join(stTDLThread, nullptr);

  std::cout << "=== Cleaning up resources ===" << std::endl;

  TDLHandler_Cleanup(&stTDLHandler);
  SystemInit_Cleanup(&stMWContext);
  SharedData_Cleanup();

  std::cout << "=== Application exited gracefully ===" << std::endl;
  return 0;
}


