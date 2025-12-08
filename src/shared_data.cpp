#include <cstring>
#include "shared_data.h"


std::atomic<bool> g_bExit(false);
cvtdl_face_t g_stFaceMeta = {0};
cvtdl_tracker_t g_stTracker = {0};
pthread_mutex_t g_ResultMutex;

// FPS tracking
float g_fCurrentFPS = 0.0f;
pthread_mutex_t g_FPSMutex;

// Selected track and lock time
int g_iSelectedTrackID = -1;
pthread_mutex_t g_SelectedTrackMutex;
std::map<int, time_t> g_mapTrackLockTime;
pthread_mutex_t g_LockTimeMutex;
std::map<int, std::vector<float>> g_mapTrackFeatures;
pthread_mutex_t g_FeatureMutex;
std::map<int, MatchResult> g_mapTrackMatchResults;
pthread_mutex_t g_MatchResultMutex;

void SharedData_Init() {
    g_bExit = false;
    std::memset(&g_stFaceMeta, 0, sizeof(cvtdl_face_t));
    std::memset(&g_stTracker, 0, sizeof(cvtdl_tracker_t));
    pthread_mutex_init(&g_ResultMutex, NULL);
    pthread_mutex_init(&g_FPSMutex, NULL);
    pthread_mutex_init(&g_SelectedTrackMutex, NULL);
    pthread_mutex_init(&g_LockTimeMutex, NULL);
    pthread_mutex_init(&g_FeatureMutex, NULL);
    pthread_mutex_init(&g_MatchResultMutex, NULL);
    g_fCurrentFPS = 0.0f;
    g_iSelectedTrackID = -1;
    g_mapTrackLockTime.clear();
    g_mapTrackFeatures.clear();
    g_mapTrackMatchResults.clear();
}

void SharedData_Cleanup() {
    CVI_TDL_Free(&g_stFaceMeta);
    CVI_TDL_Free(&g_stTracker);
    pthread_mutex_destroy(&g_ResultMutex);
    pthread_mutex_destroy(&g_FPSMutex);
    pthread_mutex_destroy(&g_SelectedTrackMutex);
    pthread_mutex_destroy(&g_LockTimeMutex);
    pthread_mutex_destroy(&g_FeatureMutex);
    pthread_mutex_destroy(&g_MatchResultMutex);
    g_mapTrackLockTime.clear();
    g_mapTrackFeatures.clear();
    g_mapTrackMatchResults.clear();
}
