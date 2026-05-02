#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <atomic>
#include <pthread.h>
#include <map>
#include <vector>
#include <string>

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

// 軌跡在中心的時間記錄（track_id -> 第一次出現在中心的時間）
extern std::map<int, time_t> g_mapTrackCenterTime;
extern pthread_mutex_t g_CenterTimeMutex;

// 軌跡特徵數據（track_id -> feature vector）
extern std::map<int, std::vector<float>> g_mapTrackFeatures;
extern pthread_mutex_t g_FeatureMutex;

// 軌跡匹配結果（track_id -> person info: name, similarity）
struct MatchResult {
    std::string name;
    int bch_errors;      // BCH 糾正的錯誤數（越少越匹配）
    int person_id;
};
extern std::map<int, MatchResult> g_mapTrackMatchResults;
extern pthread_mutex_t g_MatchResultMutex;

extern float g_fCurrentFPS;

// Pending 註冊任務佇列（Background Thread → TDL Thread）
struct PendingTask_t {
    int person_id;
    std::string name;
    std::string local_photo_path;
    std::string valid_date;  // "YYYYMMDDHHmm" 有效期（萬用零：MM/DD/HH/mm=00 代表更粗粒度）
};
extern std::vector<PendingTask_t> g_vecPendingTasks;
extern pthread_mutex_t g_PendingTaskMutex;

// 完成結果佇列（TDL Thread → Background Thread）
struct CompletedTask_t {
    int person_id;
    std::string template_hex;
    bool success;
};
extern std::vector<CompletedTask_t> g_vecCompletedTasks;
extern pthread_mutex_t g_CompletedTaskMutex;
extern pthread_mutex_t g_FPSMutex;

#define LOCK_RESULT_MUTEX() pthread_mutex_lock(&g_ResultMutex)
#define UNLOCK_RESULT_MUTEX() pthread_mutex_unlock(&g_ResultMutex)

#define LOCK_FPS_MUTEX() pthread_mutex_lock(&g_FPSMutex)
#define UNLOCK_FPS_MUTEX() pthread_mutex_unlock(&g_FPSMutex)

#define LOCK_SELECTED_TRACK_MUTEX() pthread_mutex_lock(&g_SelectedTrackMutex)
#define UNLOCK_SELECTED_TRACK_MUTEX() pthread_mutex_unlock(&g_SelectedTrackMutex)

#define LOCK_LOCKTIME_MUTEX() pthread_mutex_lock(&g_LockTimeMutex)
#define UNLOCK_LOCKTIME_MUTEX() pthread_mutex_unlock(&g_LockTimeMutex)

#define LOCK_CENTERTIME_MUTEX() pthread_mutex_lock(&g_CenterTimeMutex)
#define UNLOCK_CENTERTIME_MUTEX() pthread_mutex_unlock(&g_CenterTimeMutex)

#define LOCK_FEATURE_MUTEX() pthread_mutex_lock(&g_FeatureMutex)
#define UNLOCK_FEATURE_MUTEX() pthread_mutex_unlock(&g_FeatureMutex)

#define LOCK_MATCH_RESULT_MUTEX() pthread_mutex_lock(&g_MatchResultMutex)
#define UNLOCK_MATCH_RESULT_MUTEX() pthread_mutex_unlock(&g_MatchResultMutex)

#define LOCK_PENDING_TASK_MUTEX() pthread_mutex_lock(&g_PendingTaskMutex)
#define UNLOCK_PENDING_TASK_MUTEX() pthread_mutex_unlock(&g_PendingTaskMutex)

#define LOCK_COMPLETED_TASK_MUTEX() pthread_mutex_lock(&g_CompletedTaskMutex)
#define UNLOCK_COMPLETED_TASK_MUTEX() pthread_mutex_unlock(&g_CompletedTaskMutex)

// 鎖定後需等待的秒數才開始提取特徵
#define FEATURE_EXTRACT_LOCK_SECONDS 3

void SharedData_Init();
void SharedData_Cleanup();

#endif // SHARED_DATA_H