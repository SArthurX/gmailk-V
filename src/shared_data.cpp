#include "shared_data.h"
#include <cstring>

std::atomic<bool> g_bExit(false);
cvtdl_face_t g_stFaceMeta = {0};
pthread_mutex_t g_ResultMutex;

void SharedData_Init() {
    g_bExit = false;
    std::memset(&g_stFaceMeta, 0, sizeof(cvtdl_face_t));
    pthread_mutex_init(&g_ResultMutex, NULL);
}

void SharedData_Cleanup() {
    CVI_TDL_Free(&g_stFaceMeta);
    pthread_mutex_destroy(&g_ResultMutex);
}
