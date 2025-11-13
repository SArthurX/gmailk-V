#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <atomic>
#include <pthread.h>

#include "cvi_tdl.h"


extern std::atomic<bool> g_bExit;

extern cvtdl_face_t g_stFaceMeta;

extern pthread_mutex_t g_ResultMutex;

#define LOCK_RESULT_MUTEX() pthread_mutex_lock(&g_ResultMutex)
#define UNLOCK_RESULT_MUTEX() pthread_mutex_unlock(&g_ResultMutex)

void SharedData_Init();
void SharedData_Cleanup();

#endif // SHARED_DATA_H
