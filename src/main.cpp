#define LOG_TAG "SampleFD"
#define LOG_LEVEL LOG_LEVEL_INFO

#include <iostream>
#include <signal.h>

#include "app_guards.h"
#include "app_options.h"
#include "button_handler.h"
#include "shared_data.h"
#include "tdl_handler.h"
#include "thread_group.h"
#include "venc_handler.h"

namespace {

constexpr const char *kFaceDatabasePath = "data/face_database.json";

}  // namespace

static void SampleHandleSig(CVI_S32 signo) {
  if (SIGINT == signo || SIGTERM == signo)
    g_bExit = true;
}

int main(int argc, char *argv[]) {
  AppOptions options;
  if (!ParseArgs(argc, argv, &options)) {
    PrintUsage(argv[0]);
    return -1;
  }

  signal(SIGINT, SampleHandleSig);
  signal(SIGTERM, SampleHandleSig);

  SharedDataGuard sharedData;

  SystemGuard system;
  CVI_S32 s32Ret = system.Init();
  if (s32Ret != CVI_SUCCESS) {
    std::cerr << "System initialization failed!" << std::endl;
    return -1;
  }

  TDLHandlerGuard tdl;
  const char *arcfaceModelPath = options.arcfaceCvimodelPath.empty()
                                     ? nullptr
                                     : options.arcfaceCvimodelPath.c_str();
  s32Ret = tdl.Init(options.scrfdModelPath.c_str(), arcfaceModelPath);
  if (s32Ret != CVI_SUCCESS) {
    std::cerr << "TDL initialization failed!" << std::endl;
    return -1;
  }

  ButtonHandlerGuard button;
  s32Ret = button.Init(21, 25);
  if (s32Ret != 0) {
    std::cerr << "Button handler initialization failed!" << std::endl;
    return -1;
  }

  FaceDatabaseGuard faceDatabase;
  s32Ret = faceDatabase.Init(kFaceDatabasePath, 0.4f);
  if (s32Ret != 0) {
    std::cerr << "Face database initialization failed!" << std::endl;
    return -1;
  }

  OLEDHandlerGuard oled;
  if (options.enableOLED) {
    std::cout << "Attempting to initialize OLED display..." << std::endl;
    s32Ret = oled.Init(3);
    if (s32Ret != 0) {
      std::cerr << "Warning: OLED handler initialization failed!" << std::endl;
      std::cerr << "Warning: OLED display will be disabled" << std::endl;
    } else {
      std::cout << "OLED display initialized successfully" << std::endl;
    }
  } else {
    std::cout << "OLED display disabled (use --oled flag to enable)" << std::endl;
  }

  RemoteDatabaseGuard remoteDatabase;
  bool remoteDbEnabled = false;
  if (options.enableRemoteDB) {
    s32Ret = remoteDatabase.Init(options.rpiUrl.c_str());
    if (s32Ret == 0) {
      remoteDbEnabled = true;
      std::cout << "Remote database enabled: " << options.rpiUrl << std::endl;
    } else {
      std::cerr << "Warning: Remote database initialization failed!" << std::endl;
      std::cerr << "Warning: Will use local database only" << std::endl;
    }
  } else {
    std::cout << "Remote database disabled (use --rpi [url] to enable)" << std::endl;
  }

  tdl.SetButtonHandler(button.Get());
  tdl.SetFaceDatabase(faceDatabase.Get());
  if (oled.IsInitialized())
    tdl.SetOLEDHandler(oled.Get());
  if (remoteDbEnabled)
    tdl.SetRemoteDatabase(remoteDatabase.Get());

  VENCHandler_t stVencArgs{};
  stVencArgs.pstMWContext = system.Context();
  stVencArgs.pstTDLHandler = tdl.Get();

  ThreadGroup threads;
  if (threads.Start(VENCHandler_ThreadRoutine, &stVencArgs, "VENC") != 0)
    return -1;
  if (threads.Start(TDLHandler_ThreadRoutine, tdl.Get(), "TDL") != 0)
    return -1;
  if (threads.Start(ButtonHandler_ThreadRoutine, button.Get(), "button") != 0)
    return -1;

  if (remoteDbEnabled) {
    if (threads.Start(TDLHandler_RemoteDBThreadRoutine, tdl.Get(), "remote database") != 0)
      return -1;
  }

  std::cout << "=== Face Detection Application Started ===" << std::endl;
  std::cout << "Short press button (GPIO 21): Lock face for recognition" << std::endl;
  std::cout << "Long press button (>3s): Register locked face to database" << std::endl;
  std::cout << "LED (GPIO 25) indicates button press" << std::endl;
  std::cout << "Face database: " << kFaceDatabasePath << std::endl;
  if (remoteDbEnabled)
    std::cout << "Remote database (RPi): " << options.rpiUrl << std::endl;
  if (oled.IsInitialized())
    std::cout << "OLED display (I2C-2) shows face detection results" << std::endl;
  else
    std::cout << "OLED display disabled (not connected or initialization failed)" << std::endl;
  std::cout << "Press Ctrl+C to stop..." << std::endl;

  threads.WaitAll();

  std::cout << "=== Cleaning up resources ===" << std::endl;
  std::cout << "=== Application exited gracefully ===" << std::endl;
  return 0;
}
