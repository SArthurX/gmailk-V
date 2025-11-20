#include <cstring>
#include "shared_data.h"


std::atomic<bool> g_bExit(false);
cvtdl_face_t g_stFaceMeta = {0};
pthread_mutex_t g_ResultMutex;

// FPS tracking
float g_fCurrentFPS = 0.0f;
pthread_mutex_t g_FPSMutex;

void SharedData_Init() {
    g_bExit = false;
    std::memset(&g_stFaceMeta, 0, sizeof(cvtdl_face_t));
    pthread_mutex_init(&g_ResultMutex, NULL);
    pthread_mutex_init(&g_FPSMutex, NULL);
    g_fCurrentFPS = 0.0f;
}

void SharedData_Cleanup() {
    CVI_TDL_Free(&g_stFaceMeta);
    pthread_mutex_destroy(&g_ResultMutex);
    pthread_mutex_destroy(&g_FPSMutex);
}
