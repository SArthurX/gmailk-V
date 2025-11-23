#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <atomic>
#include <pthread.h>
#include <map>
#include <vector>

#include "cvi_tdl.h"


extern std::atomic<bool> g_bExit;

extern cvtdl_face_t g_stFaceMeta;
extern cvtdl_tracker_t g_stTracker;

extern pthread_mutex_t g_ResultMutex;

// 選中的軌跡ID (-1 表示未選中)
extern int g_iSelectedTrackID;
extern pthread_mutex_t g_SelectedTrackMutex;

// 軌跡鎖定時間（記錄選中後的時間戳）
extern std::map<int, time_t> g_mapTrackLockTime;
extern pthread_mutex_t g_LockTimeMutex;

// 軌跡特徵數據（track_id -> 128-dim feature）
extern std::map<int, std::vector<float>> g_mapTrackFeatures;
extern pthread_mutex_t g_FeatureMutex;

extern float g_fCurrentFPS;
extern pthread_mutex_t g_FPSMutex;

#define LOCK_RESULT_MUTEX() pthread_mutex_lock(&g_ResultMutex)
#define UNLOCK_RESULT_MUTEX() pthread_mutex_unlock(&g_ResultMutex)

#define LOCK_FPS_MUTEX() pthread_mutex_lock(&g_FPSMutex)
#define UNLOCK_FPS_MUTEX() pthread_mutex_unlock(&g_FPSMutex)

#define LOCK_SELECTED_TRACK_MUTEX() pthread_mutex_lock(&g_SelectedTrackMutex)
#define UNLOCK_SELECTED_TRACK_MUTEX() pthread_mutex_unlock(&g_SelectedTrackMutex)

#define LOCK_LOCKTIME_MUTEX() pthread_mutex_lock(&g_LockTimeMutex)
#define UNLOCK_LOCKTIME_MUTEX() pthread_mutex_unlock(&g_LockTimeMutex)

#define LOCK_FEATURE_MUTEX() pthread_mutex_lock(&g_FeatureMutex)
#define UNLOCK_FEATURE_MUTEX() pthread_mutex_unlock(&g_FeatureMutex)

// 鎖定後需等待的秒數才開始提取特徵
#define FEATURE_EXTRACT_LOCK_SECONDS 3

void SharedData_Init();
void SharedData_Cleanup();

#endif // SHARED_DATA_H