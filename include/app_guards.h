#ifndef APP_GUARDS_H
#define APP_GUARDS_H

#include <stdint.h>

#include "button_handler.h"
#include "face_database.h"
#include "oled_ctrl.h"
#include "remote_database.h"
#include "shared_data.h"
#include "system_init.h"
#include "tdl_handler.h"

class SharedDataGuard {
public:
  SharedDataGuard();
  ~SharedDataGuard();

private:
  bool initialized_ = false;
};

class SystemGuard {
public:
  CVI_S32 Init();
  SAMPLE_TDL_MW_CONTEXT *Context();
  ~SystemGuard();

private:
  SystemConfig_t config_{};
  SAMPLE_TDL_MW_CONTEXT context_{};
  bool initialized_ = false;
};

class TDLHandlerGuard {
public:
  CVI_S32 Init(const char *modelPath, const char *arcfaceCvimodelPath);
  TDLHandler_t *Get();
  void SetButtonHandler(ButtonHandler_t *button);
  void SetFaceDatabase(FaceDatabase_t *faceDatabase);
  void SetOLEDHandler(OLEDHandler_t *oled);
  void SetRemoteDatabase(RemoteDatabase_t *remoteDb);
  ~TDLHandlerGuard();

private:
  TDLHandler_t handler_{};
  bool initialized_ = false;
};

class ButtonHandlerGuard {
public:
  int Init(int buttonPin, int ledPin);
  ButtonHandler_t *Get();
  ~ButtonHandlerGuard();

private:
  ButtonHandler_t handler_{};
  bool initialized_ = false;
};

class FaceDatabaseGuard {
public:
  int Init(const char *dbPath, float threshold);
  FaceDatabase_t *Get();
  ~FaceDatabaseGuard();

private:
  FaceDatabase_t database_{};
  bool initialized_ = false;
};

class OLEDHandlerGuard {
public:
  int Init(uint8_t i2cDevice);
  bool IsInitialized() const;
  OLEDHandler_t *Get();
  ~OLEDHandlerGuard();

private:
  OLEDHandler_t handler_{};
  bool initialized_ = false;
};

class RemoteDatabaseGuard {
public:
  int Init(const char *baseUrl);
  bool IsEnabled() const;
  RemoteDatabase_t *Get();
  ~RemoteDatabaseGuard();

private:
  RemoteDatabase_t database_{};
  bool enabled_ = false;
};

#endif  // APP_GUARDS_H
