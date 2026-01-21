#ifndef GEOMETRY_HELPER_HPP
#define GEOMETRY_HELPER_HPP

#include <cmath>
#include <cfloat>
extern "C" {
#include <cvi_comm.h>
}

// 計算人臉中心到畫面中心的距離
static inline float CalculateDistanceToCenter(
    float face_x1, float face_y1, float face_x2, float face_y2,
    float frame_center_x, float frame_center_y) {
    
    float face_center_x = (face_x1 + face_x2) / 2.0f;
    float face_center_y = (face_y1 + face_y2) / 2.0f;
    
    float dx = face_center_x - frame_center_x;
    float dy = face_center_y - frame_center_y;
    
    return sqrt(dx * dx + dy * dy);
}

// 找到最接近畫面中心的人臉索引
// 返回 -1 表示沒有人臉在閾值範圍內
static inline int FindCenterFaceIndex(
    cvtdl_face_t* pstFaceMeta,
    float frame_center_x,
    float frame_center_y,
    float center_threshold) {
    
    if (!pstFaceMeta || pstFaceMeta->size == 0)
        return -1;
    
    int center_face_idx = -1;
    float min_distance = FLT_MAX;
    
    for (uint32_t i = 0; i < pstFaceMeta->size; i++) {
        float distance = CalculateDistanceToCenter(
            pstFaceMeta->info[i].bbox.x1,
            pstFaceMeta->info[i].bbox.y1,
            pstFaceMeta->info[i].bbox.x2,
            pstFaceMeta->info[i].bbox.y2,
            frame_center_x,
            frame_center_y
        );
        
        if (distance < center_threshold && distance < min_distance) {
            min_distance = distance;
            center_face_idx = i;
        }
    }
    
    return center_face_idx;
}

#endif // GEOMETRY_HELPER_HPP
