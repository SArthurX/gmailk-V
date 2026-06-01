#ifndef AUTO_LOCK_HELPER_HPP
#define AUTO_LOCK_HELPER_HPP

#include "shared_data.h"
#include "geometry_helper.hpp"
#include <iostream>
#include <ctime>
#include <vector>
#include <map>
extern "C" {
#include <cvi_comm.h>
}

// 自動鎖定配置
#define AUTO_LOCK_DURATION_SECONDS 3
#define AUTO_LOCK_CENTER_THRESHOLD 80.0f

// 處理自動鎖定邏輯
// 檢測在中心超過指定時間的人臉並自動鎖定
static inline void ProcessAutoLock(
    cvtdl_face_t* pstFaceMeta,
    cvtdl_tracker_t* pstTracker,
    VIDEO_FRAME_INFO_S* pstFrame) {
    
    if (!pstFaceMeta || !pstTracker || !pstFrame)
        return;
    
    if (pstTracker->size == 0)
        return;
    
    float frame_center_x = pstFrame->stVFrame.u32Width / 2.0f;
    float frame_center_y = pstFrame->stVFrame.u32Height / 2.0f;
    
    // 找到當前在中心的人臉
    int center_face_idx = FindCenterFaceIndex(
        pstFaceMeta,
        frame_center_x,
        frame_center_y,
        AUTO_LOCK_CENTER_THRESHOLD
    );
    
    if (center_face_idx != -1 && center_face_idx < (int)pstTracker->size) {
        int center_track_id = pstTracker->info[center_face_idx].id;
        
        // 記錄或更新該人臉在中心的時間
        LOCK_CENTERTIME_MUTEX();
        time_t now = time(NULL);
        if (g_mapTrackCenterTime.find(center_track_id) == g_mapTrackCenterTime.end()) {
            // 第一次出現在中心
            g_mapTrackCenterTime[center_track_id] = now;
        }
        
        time_t center_time = g_mapTrackCenterTime[center_track_id];
        int duration = (int)(now - center_time);
        UNLOCK_CENTERTIME_MUTEX();
        
        // 檢查是否已經被鎖定
        LOCK_SELECTED_TRACK_MUTEX();
        bool is_already_locked = (g_iSelectedTrackID == center_track_id);
        UNLOCK_SELECTED_TRACK_MUTEX();
        
        // 如果在中心超過指定時間且未被鎖定，自動鎖定
        if (duration >= AUTO_LOCK_DURATION_SECONDS && !is_already_locked) {
            LOCK_SELECTED_TRACK_MUTEX();
            g_iSelectedTrackID = center_track_id;
            UNLOCK_SELECTED_TRACK_MUTEX();
            
            // 記錄鎖定時間
            LOCK_LOCKTIME_MUTEX();
            g_mapTrackLockTime[center_track_id] = now;
            UNLOCK_LOCKTIME_MUTEX();
            
            std::cout << "=== Auto-Lock Triggered ===" << std::endl;
            std::cout << "Track ID: " << center_track_id << std::endl;
            std::cout << "Face at center for " << duration << "s - Auto-locked!" << std::endl;
            std::cout << "Feature extraction starting..." << std::endl;
            std::cout << "===========================" << std::endl;
        }
    }
    
    // 清除不在中心的人臉的中心時間記錄
    LOCK_CENTERTIME_MUTEX();
    std::vector<int> to_remove;
    for (auto& pair : g_mapTrackCenterTime) {
        bool still_in_center = false;
        if (center_face_idx != -1 && center_face_idx < (int)pstTracker->size)
            if (pstTracker->info[center_face_idx].id == pair.first)
                still_in_center = true;

        if (!still_in_center)
            to_remove.push_back(pair.first);
    }
    for (int id : to_remove) {
        g_mapTrackCenterTime.erase(id);
    }
    UNLOCK_CENTERTIME_MUTEX();
}

#endif // AUTO_LOCK_HELPER_HPP
