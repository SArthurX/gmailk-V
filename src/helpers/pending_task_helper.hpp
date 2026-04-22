#ifndef PENDING_TASK_HELPER_HPP
#define PENDING_TASK_HELPER_HPP

#include "tdl_handler.h"
#include "shared_data.h"
#include <iostream>
#include <cstdio>
#include <string>
extern "C" {
#include <cvi_comm.h>
}

// 處理 Pending 註冊佇列（每幀最多處理一個）
// 在 TDL Thread 內處理，unbind/rebind 與 GetChnFrame 在同一執行緒，無競爭
// 從原 TDLHandler_ThreadRoutine L573-614 提取而來
static inline void ProcessPendingRegistration(TDLHandler_t *pstHandler) {
    PendingTask_t pendingTask;
    bool hasTask = false;

    LOCK_PENDING_TASK_MUTEX();
    if (!g_vecPendingTasks.empty()) {
        pendingTask = g_vecPendingTasks.front();
        g_vecPendingTasks.erase(g_vecPendingTasks.begin());
        hasTask = true;
    }
    UNLOCK_PENDING_TASK_MUTEX();

    if (!hasTask)
        return;

    std::cout << "🔄 [TDL] Processing pending registration: " << pendingTask.name
              << " (ID: " << pendingTask.person_id << ")" << std::endl;

    std::string hex_template;
    CVI_S32 enroll_ret = TDLHandler_ProcessImageAndEnroll(
        pstHandler, pendingTask.local_photo_path.c_str(), hex_template);

    CompletedTask_t result;
    result.person_id = pendingTask.person_id;
    result.success = (enroll_ret == CVI_SUCCESS);
    result.template_hex = hex_template;

    LOCK_COMPLETED_TASK_MUTEX();
    g_vecCompletedTasks.push_back(result);
    UNLOCK_COMPLETED_TASK_MUTEX();

    if (result.success) {
        std::cout << "✅ [TDL] BioHash generated for " << pendingTask.name
                  << " (" << hex_template.size() / 2 << " bytes)" << std::endl;
    } else {
        std::cerr << "❌ [TDL] Failed to process image for " << pendingTask.name << std::endl;
    }

    // 清理下載的臨時照片
    std::remove(pendingTask.local_photo_path.c_str());
}

#endif // PENDING_TASK_HELPER_HPP
