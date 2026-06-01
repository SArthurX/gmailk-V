#ifndef OLED_HELPER_HPP
#define OLED_HELPER_HPP

#include "oled_ctrl.h"
#include "geometry_helper.hpp"
#include "shared_data.h"
#include <algorithm>
#include <cstring>
#include <time.h>
extern "C" {
#include <cvi_comm.h>
}

// OLED 最大刷新間隔 (毫秒) — 限制約 10 FPS，避免 SPI 過載造成閃爍/撕裂
#define OLED_MIN_REFRESH_MS 100

// 更新 OLED 顯示
// 準備人臉框數據並更新 OLED (含幀率節流 + 動態匹配結果)
static inline void UpdateOLEDDisplay(
    OLEDHandler_t* oledHandler,
    cvtdl_face_t* pstFaceMeta,
    cvtdl_tracker_t* pstTracker,
    VIDEO_FRAME_INFO_S* pstFrame,
    float current_fps) {
    
    if (!oledHandler || !oledHandler->initialized || !pstFaceMeta || !pstFrame)
        return;
    
    // === 幀率節流：避免 OLED SPI 傳輸跟不上 TDL 管線速度 ===
    static struct timespec last_oled_time = {0, 0};
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    long elapsed_ms = (now.tv_sec - last_oled_time.tv_sec) * 1000 
                    + (now.tv_nsec - last_oled_time.tv_nsec) / 1000000;
    
    if (elapsed_ms < OLED_MIN_REFRESH_MS)
        return;  // 跳過此幀，避免 SPI 過載
    
    last_oled_time = now;
    
    // 準備 OLED 人臉框數據
    OLEDFaceBox_t oled_faces[32]; // 最多支持 32 個人臉
    uint32_t oled_face_count = std::min((uint32_t)pstFaceMeta->size, (uint32_t)32);
    
    // 計算畫面中心點
    float frame_center_x = pstFrame->stVFrame.u32Width / 2.0f;
    float frame_center_y = pstFrame->stVFrame.u32Height / 2.0f;
    float center_threshold = 150.0f; // 中心對準閾值
    
    // 找出最接近中心的人臉
    int center_face_idx = FindCenterFaceIndex(
        pstFaceMeta,
        frame_center_x,
        frame_center_y,
        center_threshold
    );
    
    for (uint32_t i = 0; i < oled_face_count; i++) {
        oled_faces[i].x1 = pstFaceMeta->info[i].bbox.x1;
        oled_faces[i].y1 = pstFaceMeta->info[i].bbox.y1;
        oled_faces[i].x2 = pstFaceMeta->info[i].bbox.x2;
        oled_faces[i].y2 = pstFaceMeta->info[i].bbox.y2;
        oled_faces[i].score = pstFaceMeta->info[i].bbox.score;
        oled_faces[i].is_center = 0;
    }
    
    // 標記中心人臉
    if (center_face_idx >= 0)
        oled_faces[center_face_idx].is_center = 1;
    
    // === 動態匹配結果：只顯示當前可見軌跡的匹配 ===
    const char *match_name_ptr = nullptr;
    const char *match_payload_ptr = nullptr;
    std::string oled_match_name;
    std::string oled_match_payload;
    
    if (pstTracker && pstTracker->size > 0) {
        // 取得選中的軌跡 ID
        LOCK_SELECTED_TRACK_MUTEX();
        int selectedTrackID = g_iSelectedTrackID;
        UNLOCK_SELECTED_TRACK_MUTEX();
        
        LOCK_MATCH_RESULT_MUTEX();
        
        // 優先級：選中的軌跡 > 中心人臉 > 第一個有匹配的可見軌跡
        int best_track_id = -1;
        
        // 1. 最高優先：選中的軌跡
        if (selectedTrackID != -1) {
            for (uint32_t i = 0; i < pstTracker->size; i++) {
                if ((int)pstTracker->info[i].id == selectedTrackID) {
                    if (g_mapTrackMatchResults.count(selectedTrackID))
                        best_track_id = selectedTrackID;
                    break;
                }
            }
        }
        
        // 2. 次優先：中心人臉的軌跡
        if (best_track_id == -1 && center_face_idx >= 0 
            && (uint32_t)center_face_idx < pstTracker->size) {
            int center_tid = (int)pstTracker->info[center_face_idx].id;
            if (g_mapTrackMatchResults.count(center_tid))
                best_track_id = center_tid;
        }
        
        // 3. 最低優先：任何可見軌跡中有匹配的
        if (best_track_id == -1) {
            for (uint32_t i = 0; i < pstTracker->size; i++) {
                int tid = (int)pstTracker->info[i].id;
                if (g_mapTrackMatchResults.count(tid)) {
                    best_track_id = tid;
                    break;
                }
            }
        }
        
        // 取出匹配結果
        if (best_track_id != -1) {
            auto it = g_mapTrackMatchResults.find(best_track_id);
            if (it != g_mapTrackMatchResults.end()) {
                oled_match_name = it->second.name;
                oled_match_payload = it->second.decrypted_payload;
            }
        }
        
        UNLOCK_MATCH_RESULT_MUTEX();
    }
    // 如果沒有可見軌跡或沒有匹配，oled_match_name 為空 → 文字自動消失
    
    if (!oled_match_name.empty())
        match_name_ptr = oled_match_name.c_str();
    if (!oled_match_payload.empty())
        match_payload_ptr = oled_match_payload.c_str();
    
    // 更新 OLED 顯示
    OLEDHandler_UpdateDisplay(oledHandler, oled_faces, oled_face_count, current_fps,
                              match_name_ptr, match_payload_ptr);
}

#endif // OLED_HELPER_HPP


