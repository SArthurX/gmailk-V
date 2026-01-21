#ifndef FPS_HELPER_HPP
#define FPS_HELPER_HPP

#include "shared_data.h"
#include <sys/time.h>

// FPS 計算器結構
typedef struct {
    struct timeval start_time;
    int frame_count;
    float current_fps;
} FPSCalculator_t;

// 初始化 FPS 計算器
static inline void FPSCalculator_Init(FPSCalculator_t* calculator) {
    if (!calculator)
        return;
    
    gettimeofday(&calculator->start_time, NULL);
    calculator->frame_count = 0;
    calculator->current_fps = 0.0f;
}

// 更新 FPS 計算（每幀調用一次）
// 返回 true 表示 FPS 已更新（每秒更新一次）
static inline bool FPSCalculator_Update(FPSCalculator_t* calculator) {
    if (!calculator)
        return false;
    
    calculator->frame_count++;
    
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    unsigned long elapsed = ((current_time.tv_sec - calculator->start_time.tv_sec) * 1000000 + 
                            current_time.tv_usec - calculator->start_time.tv_usec);
    
    if (elapsed >= 1000000) { // 1 second
        calculator->current_fps = (float)calculator->frame_count * 1000000.0f / (float)elapsed;
        
        // 更新全局 FPS
        LOCK_FPS_MUTEX();
        g_fCurrentFPS = calculator->current_fps;
        UNLOCK_FPS_MUTEX();
        
        calculator->frame_count = 0;
        calculator->start_time = current_time;
        
        return true;
    }
    
    return false;
}

// 獲取當前 FPS
static inline float FPSCalculator_GetFPS(FPSCalculator_t* calculator) {
    return calculator ? calculator->current_fps : 0.0f;
}

#endif // FPS_HELPER_HPP
