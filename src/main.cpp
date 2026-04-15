#define LOG_TAG "SampleFD"
#define LOG_LEVEL LOG_LEVEL_INFO

#include <iostream>
#include <signal.h>
#include <pthread.h>
#include <cstring>

#include "shared_data.h"
#include "system_init.h"
#include "tdl_handler.h"
#include "venc_handler.h"
#include "button_handler.h"
#include "oled_ctrl.h"
#include "face_database.h"
#include "remote_database.h"

static void SampleHandleSig(CVI_S32 signo) {
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  std::cout << "Handle signal, signo: " << signo << std::endl;
  if (SIGINT == signo || SIGTERM == signo) {
    g_bExit = true;
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cout << "\nUsage: " << argv[0] << " SCRFDFACE_MODEL_PATH [ARCFACE_CVIMODEL]\n\n"
              << "\tSCRFDFACE_MODEL_PATH, path to scrfdface model.\n"
              << "\tARCFACE_CVIMODEL (optional), path to ArcFace .cvimodel file (TPU).\n"
              << "\t--oled, optional flag to enable OLED display (I2C-2).\n" 
              << "\nExample:\n"
              << "\t" << argv[0] << " models/scrfd.cvimodel\n"
              << "\t" << argv[0] << " models/scrfd.cvimodel models/arcface.cvimodel\n" 
              << std::endl;
    return -1;
  }

  const char* arcfaceCvimodel = (argc >= 3) ? argv[2] : nullptr;

  // Check for --oled flag and --rpi flag
  bool enable_oled = false;
  const char* rpi_url = nullptr;
  for (int i = 2; i < argc; i++) {
    if (std::string(argv[i]) == "--oled") {
      enable_oled = true;
      // If --oled is in position 2, arcface path might be wrong
      if (i == 2) arcfaceCvimodel = nullptr;
    } else if (std::string(argv[i]) == "--rpi") {
      // --rpi [url] : 預設 http://192.168.42.1:3000
      if (i + 1 < argc && argv[i+1][0] != '-') {
        rpi_url = argv[++i];
      } else {
        rpi_url = "http://192.168.42.1:3000";
      }
      // If --rpi is in position 2, arcface path might be wrong
      if (i == 2 || (i == 3 && rpi_url == argv[i])) arcfaceCvimodel = nullptr;
    }
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
  s32Ret = TDLHandler_Init(&stTDLHandler, argv[1], arcfaceCvimodel);
  if (s32Ret != CVI_SUCCESS) {
    std::cerr << "TDL initialization failed!" << std::endl;
    SystemInit_Cleanup(&stMWContext);
    SharedData_Cleanup();
    return -1;
  }

  // Initialize button handler (button pin 21, LED pin 25)
  ButtonHandler_t stButtonHandler;
  s32Ret = ButtonHandler_Init(&stButtonHandler, 21, 25);
  if (s32Ret != 0) {
    std::cerr << "Button handler initialization failed!" << std::endl;
    TDLHandler_Cleanup(&stTDLHandler);
    SystemInit_Cleanup(&stMWContext);
    SharedData_Cleanup();
    return -1;
  }
  
  // Initialize face database
  FaceDatabase_t stFaceDatabase;
  const char* db_path = "data/face_database.json";
  s32Ret = FaceDatabase_Init(&stFaceDatabase, db_path, 0.4f);
  if (s32Ret != 0) {
    std::cerr << "Face database initialization failed!" << std::endl;
    ButtonHandler_Cleanup(&stButtonHandler);
    TDLHandler_Cleanup(&stTDLHandler);
    SystemInit_Cleanup(&stMWContext);
    SharedData_Cleanup();
    return -1;
  }
  
  // Initialize OLED handler (I2C device 3) - optional
  OLEDHandler_t stOLEDHandler;
  std::memset(&stOLEDHandler, 0, sizeof(OLEDHandler_t));
  
  if (enable_oled) {
    std::cout << "Attempting to initialize OLED display..." << std::endl;
    s32Ret = OLEDHandler_Init(&stOLEDHandler, 3);
    if (s32Ret != 0) {
      std::cerr << "Warning: OLED handler initialization failed!" << std::endl;
      std::cerr << "Warning: OLED display will be disabled" << std::endl;
    } else
      std::cout << "OLED display initialized successfully" << std::endl;
  } else 
    std::cout << "OLED display disabled (use --oled flag to enable)" << std::endl;
  
  // Initialize remote database (RPi HTTP) - optional
  RemoteDatabase_t stRemoteDB;
  stRemoteDB.initialized = false;
  stRemoteDB.connected = false;
  stRemoteDB.cache_valid = false;
  bool remote_db_enabled = false;
  
  if (rpi_url) {
    s32Ret = RemoteDatabase_Init(&stRemoteDB, rpi_url);
    if (s32Ret == 0) {
      remote_db_enabled = true;
      std::cout << "Remote database enabled: " << rpi_url << std::endl;
    } else {
      std::cerr << "Warning: Remote database initialization failed!" << std::endl;
      std::cerr << "Warning: Will use local database only" << std::endl;
    }
  } else
    std::cout << "Remote database disabled (use --rpi [url] to enable)" << std::endl;
  
  // link button handler and OLED handler to TDL handler
  TDLHandler_SetButtonHandler(&stTDLHandler, &stButtonHandler);
  TDLHandler_SetFaceDatabase(&stTDLHandler, &stFaceDatabase);
  if (stOLEDHandler.initialized) {
    TDLHandler_SetOLEDHandler(&stTDLHandler, &stOLEDHandler);
  }
  if (remote_db_enabled) {
    TDLHandler_SetRemoteDatabase(&stTDLHandler, &stRemoteDB);
  }

  VENCHandler_t stVencArgs;
  stVencArgs.pstMWContext = &stMWContext;
  stVencArgs.pstTDLHandler = &stTDLHandler;

  pthread_t stVencThread, stTDLThread, stButtonThread;
  pthread_create(&stVencThread, nullptr, VENCHandler_ThreadRoutine, &stVencArgs);
  pthread_create(&stTDLThread, nullptr, TDLHandler_ThreadRoutine, &stTDLHandler);
  pthread_create(&stButtonThread, nullptr, ButtonHandler_ThreadRoutine, &stButtonHandler);

  std::cout << "=== Face Detection Application Started ===" << std::endl;
  std::cout << "Short press button (GPIO 21): Lock face for recognition" << std::endl;
  std::cout << "Long press button (>3s): Register locked face to database" << std::endl;
  std::cout << "LED (GPIO 25) indicates button press" << std::endl;
  std::cout << "Face database: " << db_path << std::endl;
  if (remote_db_enabled)
    std::cout << "Remote database (RPi): " << rpi_url << std::endl;
  if (stOLEDHandler.initialized)
    std::cout << "OLED display (I2C-2) shows face detection results" << std::endl;
  else
    std::cout << "OLED display disabled (not connected or initialization failed)" << std::endl;
  std::cout << "Press Ctrl+C to stop..." << std::endl;

  pthread_join(stVencThread, nullptr);
  pthread_join(stTDLThread, nullptr);
  pthread_join(stButtonThread, nullptr);

  std::cout << "=== Cleaning up resources ===" << std::endl;

  if (stOLEDHandler.initialized)
    OLEDHandler_Cleanup(&stOLEDHandler);
  if (remote_db_enabled)
    RemoteDatabase_Cleanup(&stRemoteDB);
  FaceDatabase_Cleanup(&stFaceDatabase);
  ButtonHandler_Cleanup(&stButtonHandler);
  TDLHandler_Cleanup(&stTDLHandler);
  SystemInit_Cleanup(&stMWContext);
  SharedData_Cleanup();

  std::cout << "=== Application exited gracefully ===" << std::endl;
  return 0;
}