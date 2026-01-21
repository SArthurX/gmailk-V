#ifndef OLED_HELPER_HPP
#define OLED_HELPER_HPP

#include "oled_ctrl.h"
#include "geometry_helper.hpp"
#include <algorithm>
extern "C" {
#include <cvi_comm.h>
}

// 更新 OLED 顯示
// 準備人臉框數據並更新 OLED
static inline void UpdateOLEDDisplay(
    OLEDHandler_t* oledHandler,
    cvtdl_face_t* pstFaceMeta,
    VIDEO_FRAME_INFO_S* pstFrame,
    float current_fps) {
    
    if (!oledHandler || !oledHandler->initialized || !pstFaceMeta || !pstFrame)
        return;
    
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
    
    // 更新 OLED 顯示
    OLEDHandler_UpdateDisplay(oledHandler, oled_faces, oled_face_count, current_fps);
}

#endif // OLED_HELPER_HPP
