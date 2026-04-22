#include "app_guards.h"

SharedDataGuard::SharedDataGuard() {
  SharedData_Init();
  initialized_ = true;
}

SharedDataGuard::~SharedDataGuard() {
  if (initialized_)
    SharedData_Cleanup();
}

CVI_S32 SystemGuard::Init() {
  CVI_S32 ret = SystemInit_All(&config_, &context_);
  initialized_ = (ret == CVI_SUCCESS);
  return ret;
}

SAMPLE_TDL_MW_CONTEXT *SystemGuard::Context() { return &context_; }

SystemGuard::~SystemGuard() {
  if (initialized_)
    SystemInit_Cleanup(&context_);
}

CVI_S32 TDLHandlerGuard::Init(const char *modelPath, const char *arcfaceCvimodelPath) {
  CVI_S32 ret = TDLHandler_Init(&handler_, modelPath, arcfaceCvimodelPath);
  initialized_ = (ret == CVI_SUCCESS);
  return ret;
}

TDLHandler_t *TDLHandlerGuard::Get() { return &handler_; }

void TDLHandlerGuard::SetButtonHandler(ButtonHandler_t *button) {
  TDLHandler_SetButtonHandler(&handler_, button);
}

void TDLHandlerGuard::SetFaceDatabase(FaceDatabase_t *faceDatabase) {
  TDLHandler_SetFaceDatabase(&handler_, faceDatabase);
}

void TDLHandlerGuard::SetOLEDHandler(OLEDHandler_t *oled) {
  TDLHandler_SetOLEDHandler(&handler_, oled);
}

void TDLHandlerGuard::SetRemoteDatabase(RemoteDatabase_t *remoteDb) {
  TDLHandler_SetRemoteDatabase(&handler_, remoteDb);
}

TDLHandlerGuard::~TDLHandlerGuard() {
  if (initialized_)
    TDLHandler_Cleanup(&handler_);
}

int ButtonHandlerGuard::Init(int buttonPin, int ledPin) {
  int ret = ButtonHandler_Init(&handler_, buttonPin, ledPin);
  initialized_ = (ret == 0);
  return ret;
}

ButtonHandler_t *ButtonHandlerGuard::Get() { return &handler_; }

ButtonHandlerGuard::~ButtonHandlerGuard() {
  if (initialized_)
    ButtonHandler_Cleanup(&handler_);
}

int FaceDatabaseGuard::Init(const char *dbPath, float threshold) {
  int ret = FaceDatabase_Init(&database_, dbPath, threshold);
  initialized_ = (ret == 0);
  return ret;
}

FaceDatabase_t *FaceDatabaseGuard::Get() { return &database_; }

FaceDatabaseGuard::~FaceDatabaseGuard() {
  if (initialized_)
    FaceDatabase_Cleanup(&database_);
}

int OLEDHandlerGuard::Init(uint8_t i2cDevice) {
  int ret = OLEDHandler_Init(&handler_, i2cDevice);
  initialized_ = (ret == 0);
  return ret;
}

bool OLEDHandlerGuard::IsInitialized() const { return initialized_; }

OLEDHandler_t *OLEDHandlerGuard::Get() { return &handler_; }

OLEDHandlerGuard::~OLEDHandlerGuard() {
  if (initialized_)
    OLEDHandler_Cleanup(&handler_);
}

int RemoteDatabaseGuard::Init(const char *baseUrl) {
  int ret = RemoteDatabase_Init(&database_, baseUrl);
  enabled_ = (ret == 0);
  return ret;
}

bool RemoteDatabaseGuard::IsEnabled() const { return enabled_; }

RemoteDatabase_t *RemoteDatabaseGuard::Get() { return &database_; }

RemoteDatabaseGuard::~RemoteDatabaseGuard() {
  if (enabled_)
    RemoteDatabase_Cleanup(&database_);
}
