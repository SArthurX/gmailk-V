#include <iostream>
#include <cstring>
#include <sys/time.h>
#include <time.h>
#include <cmath>
#include <cfloat>
#include "tdl_handler.h"
#include "shared_data.h"
#include "draw_utils.h"
#include "button_handler.h"
#include "face_feature_extractor.h"

extern "C" {
#include <cvi_sys.h>
#include "middleware_utils.h"
}

CVI_S32 TDLHandler_Init(TDLHandler_t *pstHandler, const char *modelPath,
                        const char *arcfaceParam, const char *arcfaceBin) {
    if (!pstHandler || !modelPath) {
        std::cerr << "Invalid parameters for TDLHandler_Init" << std::endl;
        return CVI_FAILURE;
    }
    
    std::memset(pstHandler, 0, sizeof(TDLHandler_t));
    pstHandler->modelPath = modelPath;
    pstHandler->arcfaceParamPath = arcfaceParam;
    pstHandler->arcfaceBinPath = arcfaceBin;
    pstHandler->buttonHandler = nullptr;
    pstHandler->featureExtractor = nullptr;
    pstHandler->oledHandler = nullptr;
    
    // Create TDL handle and assign VPSS Grp1 Device 0 to TDL SDK
    CVI_S32 s32Ret = CVI_TDL_CreateHandle2(&pstHandler->tdlHandle, 1, 0);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to create TDL handle, ret=0x" << std::hex << s32Ret << std::endl;
        return s32Ret;
    }
    
    // Set VBPool for TDL
    s32Ret = CVI_TDL_SetVBPool(pstHandler->tdlHandle, 0, 2);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to set VBPool, ret=0x" << std::hex << s32Ret << std::endl;
        CVI_TDL_DestroyHandle(pstHandler->tdlHandle);
        return s32Ret;
    }
    
    // Set VPSS timeout
    CVI_TDL_SetVpssTimeout(pstHandler->tdlHandle, 1000);
    
    // Create service handle
    s32Ret = CVI_TDL_Service_CreateHandle(&pstHandler->serviceHandle, pstHandler->tdlHandle);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to create service handle, ret=0x" << std::hex << s32Ret << std::endl;
        CVI_TDL_DestroyHandle(pstHandler->tdlHandle);
        return s32Ret;
    }
    
    // Open face detection model
    s32Ret = CVI_TDL_OpenModel(pstHandler->tdlHandle, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, modelPath);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to open model, ret=0x" << std::hex << s32Ret << std::endl;
        CVI_TDL_Service_DestroyHandle(pstHandler->serviceHandle);
        CVI_TDL_DestroyHandle(pstHandler->tdlHandle);
        return s32Ret;
    }
    
    // Initialize DeepSORT tracker
    s32Ret = CVI_TDL_DeepSORT_Init(pstHandler->tdlHandle, false);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to initialize DeepSORT, ret=0x" << std::hex << s32Ret << std::endl;
        CVI_TDL_Service_DestroyHandle(pstHandler->serviceHandle);
        CVI_TDL_DestroyHandle(pstHandler->tdlHandle);
        return s32Ret;
    }
    
    // Configure DeepSORT parameters for face tracking
    cvtdl_deepsort_config_t ds_conf;
    CVI_TDL_DeepSORT_GetDefaultConfig(&ds_conf);
    
    ds_conf.ktracker_conf.max_unmatched_num = 90;      // ~3s @ 30fps
    ds_conf.ktracker_conf.accreditation_threshold = 10; // ~0.33s @ 30fps
    ds_conf.max_distance_iou = 0.5f;
    
    CVI_TDL_DeepSORT_SetConfig(pstHandler->tdlHandle, &ds_conf, -1, false);
    
    // Initialize ArcFace feature extractor (optional)
    if (arcfaceParam && arcfaceBin) {
        pstHandler->featureExtractor = new FaceFeatureExtractor(
            arcfaceParam,
            arcfaceBin,
            pstHandler->tdlHandle
        );
        
        if (pstHandler->featureExtractor && pstHandler->featureExtractor->isLoaded()) {
            std::cout << "✅ Feature extractor initialized successfully" << std::endl;
        } else {
            std::cerr << "⚠️  Feature extractor initialization failed, tracking will work without features" << std::endl;
            if (pstHandler->featureExtractor) {
                delete pstHandler->featureExtractor;
                pstHandler->featureExtractor = nullptr;
            }
        }
    } else {
        std::cout << "ℹ️  Feature extraction disabled (no ArcFace model specified)" << std::endl;
    }
    
    std::cout << "TDL Handler initialized successfully" << std::endl;
    std::cout << "Model loaded: " << modelPath << std::endl;
    std::cout << "DeepSORT tracker initialized" << std::endl;
    
    return CVI_SUCCESS;
}

void TDLHandler_Cleanup(TDLHandler_t *pstHandler) {
    if (pstHandler) {
        if (pstHandler->featureExtractor) {
            delete pstHandler->featureExtractor;
            pstHandler->featureExtractor = nullptr;
        }
        if (pstHandler->serviceHandle) {
            CVI_TDL_Service_DestroyHandle(pstHandler->serviceHandle);
        }
        if (pstHandler->tdlHandle) {
            CVI_TDL_DestroyHandle(pstHandler->tdlHandle);
        }
        std::memset(pstHandler, 0, sizeof(TDLHandler_t));
    }
    std::cout << "TDL Handler cleaned up" << std::endl;
}

CVI_S32 TDLHandler_DetectFace(TDLHandler_t *pstHandler, 
                              VIDEO_FRAME_INFO_S *pstFrame, 
                              cvtdl_face_t *pstFaceMeta) {
    if (!pstHandler || !pstFrame || !pstFaceMeta) {
        return CVI_FAILURE;
    }
    
    return CVI_TDL_FaceDetection(pstHandler->tdlHandle, pstFrame, 
                                 CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, pstFaceMeta);
}

CVI_S32 TDLHandler_DrawFaceRect(TDLHandler_t *pstHandler,
                                cvtdl_face_t *pstFaceMeta,
                                VIDEO_FRAME_INFO_S *pstFrame,
                                cvtdl_tracker_t *pstTracker) {
    if (!pstHandler || !pstFaceMeta || !pstFrame) {
        return CVI_FAILURE;
    }

    // no face then return
    if (pstFaceMeta->size == 0) {
        return CVI_SUCCESS;
    }

    float frame_center_x = pstFrame->stVFrame.u32Width / 2.0f;
    float frame_center_y = pstFrame->stVFrame.u32Height / 2.0f;
    
    float center_threshold = 80.0f; 
    
    // find the face closest to the center crosshair (within threshold)
    int center_face_idx = -1;
    float min_distance = FLT_MAX;
    
    // Get selected track ID
    LOCK_SELECTED_TRACK_MUTEX();
    int selectedTrackID = g_iSelectedTrackID;
    UNLOCK_SELECTED_TRACK_MUTEX();
    
    for (uint32_t i = 0; i < pstFaceMeta->size; i++) {
        float bbox_x1 = pstFaceMeta->info[i].bbox.x1;
        float bbox_y1 = pstFaceMeta->info[i].bbox.y1;
        float bbox_x2 = pstFaceMeta->info[i].bbox.x2;
        float bbox_y2 = pstFaceMeta->info[i].bbox.y2;
        
        float face_center_x = (bbox_x1 + bbox_x2) / 2.0f;
        float face_center_y = (bbox_y1 + bbox_y2) / 2.0f;
        
        float dx = face_center_x - frame_center_x;
        float dy = face_center_y - frame_center_y;
        float distance = sqrt(dx * dx + dy * dy);
        
        // if in the threshold 
        if (distance < center_threshold && distance < min_distance) {
            min_distance = distance;
            center_face_idx = i;
        }
    }
    
    
    CVI_S32 s32Ret = CVI_SUCCESS;
    for (uint32_t i = 0; i < pstFaceMeta->size; i++) {
        cvtdl_service_brush_t brush;
        brush.size = 4;
        
        bool is_selected = false;
        
        // 優先級判斷：選中狀態 > 中心位置 > 追蹤狀態
        if (pstTracker && i < pstTracker->size) {
            // 1. 最高優先級：檢查是否為選中的軌跡（紅色）
            if (selectedTrackID != -1 && (int)pstTracker->info[i].id == selectedTrackID) {
                brush = BRUSH_RED;  // 選中的軌跡：紅色框
                is_selected = true;
            }
            // 2. 次優先級：在中心準星且未被選中（黃色，可按按鈕選中）
            else if ((int)i == center_face_idx) {
                brush = BRUSH_YELLOW;  // 中心位置的人臉：黃色框（提示可選中）
            }
            // 3. 最低優先級：根據追蹤狀態著色
            else if (pstTracker->info[i].state == CVI_TRACKER_STABLE) {
                brush = BRUSH_GREEN;  // 穩定追蹤：綠色框
            } else if (pstTracker->info[i].state == CVI_TRACKER_NEW) {
                brush = BRUSH_YELLOW;  // 新追蹤：黃色框
            } else {
                brush = BRUSH_BLUE;  // 不穩定追蹤：藍色框
            }
        } else {
            brush = BRUSH_BLUE;  // 無追蹤資訊：藍色框
        }
  
        cvtdl_face_t single_face = {0};
        single_face.size = 1;
        single_face.width = pstFaceMeta->width;
        single_face.height = pstFaceMeta->height;
        single_face.info = (cvtdl_face_info_t *)malloc(sizeof(cvtdl_face_info_t));
        if (single_face.info) {
            memcpy(single_face.info, &pstFaceMeta->info[i], sizeof(cvtdl_face_info_t));
            
            s32Ret = CVI_TDL_Service_FaceDrawRect(pstHandler->serviceHandle, &single_face, 
                                                  pstFrame, false, brush);
            
            // Draw tracking ID and feature data if available
            if (pstTracker && i < pstTracker->size) {
                int text_y_offset = 0;
                
                // Draw ID
                char id_text[64];
                snprintf(id_text, sizeof(id_text), "ID:%lu", pstTracker->info[i].id);
                
                int text_x = (int)pstFaceMeta->info[i].bbox.x1;
                int text_y = (int)pstFaceMeta->info[i].bbox.y1 - 25;
                
                CVI_TDL_Service_ObjectWriteText(id_text, text_x, text_y, pstFrame,
                                               brush.color.r, brush.color.g, brush.color.b);
                text_y_offset += 15;
                
                // If selected and has feature, display feature data
                if (is_selected) {
                    LOCK_FEATURE_MUTEX();
                    if (g_mapTrackFeatures.find(pstTracker->info[i].id) != g_mapTrackFeatures.end()) {
                        const std::vector<float>& feature = g_mapTrackFeatures[pstTracker->info[i].id];
                        
                        // Display first 4 feature values
                        char feat_text[80];
                        snprintf(feat_text, sizeof(feat_text), "F:[%.2f,%.2f,%.2f,%.2f]", 
                                feature[0], feature[1], feature[2], feature[3]);
                        
                        CVI_TDL_Service_ObjectWriteText(feat_text, text_x, text_y - text_y_offset, pstFrame,
                                                       255.0f, 255.0f, 255.0f);  // White text
                    } else {
                        // Show "Waiting..." if locked but not extracted yet
                        LOCK_LOCKTIME_MUTEX();
                        if (g_mapTrackLockTime.find(pstTracker->info[i].id) != g_mapTrackLockTime.end()) {
                            time_t lockTime = g_mapTrackLockTime[pstTracker->info[i].id];
                            int duration = (int)(time(NULL) - lockTime);
                            if (duration < FEATURE_EXTRACT_LOCK_SECONDS) {
                                char wait_text[32];
                                snprintf(wait_text, sizeof(wait_text), "Wait %ds", 
                                        FEATURE_EXTRACT_LOCK_SECONDS - duration);
                                CVI_TDL_Service_ObjectWriteText(wait_text, text_x, text_y - text_y_offset, pstFrame,
                                                               255.0f, 255.0f, 0.0f);  // Yellow text
                            }
                        }
                        UNLOCK_LOCKTIME_MUTEX();
                    }
                    UNLOCK_FEATURE_MUTEX();
                }
            }
            
            free(single_face.info);
            if (s32Ret != CVI_SUCCESS) {
                return s32Ret;
            }
        }
    }
    
    return s32Ret;
}

void TDLHandler_SetButtonHandler(TDLHandler_t *pstHandler, ButtonHandler_t *buttonHandler) {
    if (pstHandler) {
        pstHandler->buttonHandler = buttonHandler;
    }
}

void TDLHandler_SetOLEDHandler(TDLHandler_t *pstHandler, OLEDHandler_t *oledHandler) {
    if (pstHandler) {
        pstHandler->oledHandler = oledHandler;
    }
}

CVI_S32 TDLHandler_CapturePhoto(VIDEO_FRAME_INFO_S *pstFrame, const char *filepath) {
    if (!pstFrame || !filepath) {
        std::cerr << "Invalid parameters for capture" << std::endl;
        return CVI_FAILURE;
    }

    CVI_Mmap(pstFrame);
        
    CVI_S32 ret = CVI_TDL_DumpVpssFrame(filepath, pstFrame);
    
    CVI_Mmap(pstFrame, true);
    return ret;
}

void *TDLHandler_ThreadRoutine(void *pHandle) {
    std::cout << "Enter TDL thread" << std::endl;
    
    TDLHandler_t *pstHandler = static_cast<TDLHandler_t *>(pHandle);
    VIDEO_FRAME_INFO_S stFrame;
    cvtdl_face_t stFaceMeta = {0};
    cvtdl_tracker_t stTracker = {0};
    CVI_S32 s32Ret;
    static uint32_t s_u32LastFaceSize = 0;
    
    struct timeval t0, t1 ,fps_t0, fps_t1;;
    unsigned long execution_time;
    gettimeofday(&fps_t0, NULL);
    int frame_count = 0;
    float current_fps = 0.0f;
    
    while (!g_bExit) {
        s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);
        
        if (s32Ret == CVI_SUCCESS) {
            static bool bFirstFrame = true;
            if (bFirstFrame) {
                std::cout << "=== Frame Information ===" << std::endl;
                std::cout << "Width: " << stFrame.stVFrame.u32Width << std::endl;
                std::cout << "Height: " << stFrame.stVFrame.u32Height << std::endl;
                std::cout << "Pixel Format: " << stFrame.stVFrame.enPixelFormat << std::endl;
                std::cout << "Stride[0]: " << stFrame.stVFrame.u32Stride[0] << std::endl;
                std::cout << "=========================" << std::endl;
                bFirstFrame = false;
            }
        }
        
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << "CVI_VPSS_GetChnFrame failed with 0x" << std::hex << s32Ret << std::endl;
            break;
        }
        
        std::memset(&stFaceMeta, 0, sizeof(cvtdl_face_t));
        gettimeofday(&t0, NULL);
        
        s32Ret = TDLHandler_DetectFace(pstHandler, &stFrame, &stFaceMeta);
        
        gettimeofday(&t1, NULL);
        
        if (s32Ret != CVI_TDL_SUCCESS) {
            std::cerr << "Inference failed, ret=0x" << std::hex << s32Ret << std::endl;
            CVI_TDL_Free(&stFaceMeta);
            CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
            if (s32Ret != CVI_SUCCESS) {
                g_bExit = true;
            }
            continue;
        }
        
        // Perform DeepSORT tracking
        std::memset(&stTracker, 0, sizeof(cvtdl_tracker_t));
        if (stFaceMeta.size > 0) {
            s32Ret = CVI_TDL_DeepSORT_Face(pstHandler->tdlHandle, &stFaceMeta, &stTracker);
            if (s32Ret != CVI_TDL_SUCCESS) {
                std::cerr << "DeepSORT tracking failed, ret=0x" << std::hex << s32Ret << std::endl;
            }
            
            // === 按鈕處理（在追蹤完成後） ===
            if (pstHandler->buttonHandler) {
                ButtonPressType_t pressType = ButtonHandler_GetPressType(pstHandler->buttonHandler);
                
                if (pressType == BUTTON_PRESS_SHORT) {
                    std::cout << "🔘 Button pressed (SHORT)" << std::endl;
                    
                    // 短按：選中當前在中心準星的人臉
                    if (stTracker.size > 0) {
                        // 找到中心的人臉
                        float frame_center_x = stFrame.stVFrame.u32Width / 2.0f;
                        float frame_center_y = stFrame.stVFrame.u32Height / 2.0f;
                        float center_threshold = 80.0f;
                        
                        int center_face_idx = -1;
                        float min_distance = FLT_MAX;
                        
                        for (uint32_t i = 0; i < stFaceMeta.size; i++) {
                            float face_center_x = (stFaceMeta.info[i].bbox.x1 + stFaceMeta.info[i].bbox.x2) / 2.0f;
                            float face_center_y = (stFaceMeta.info[i].bbox.y1 + stFaceMeta.info[i].bbox.y2) / 2.0f;
                            
                            float dx = face_center_x - frame_center_x;
                            float dy = face_center_y - frame_center_y;
                            float distance = sqrt(dx * dx + dy * dy);
                            
                            if (distance < center_threshold && distance < min_distance) {
                                min_distance = distance;
                                center_face_idx = i;
                            }
                        }
                        
                        if (center_face_idx != -1 && center_face_idx < (int)stTracker.size) {
                            LOCK_SELECTED_TRACK_MUTEX();
                            g_iSelectedTrackID = stTracker.info[center_face_idx].id;
                            UNLOCK_SELECTED_TRACK_MUTEX();
                            
                            // 記錄選中時間
                            LOCK_LOCKTIME_MUTEX();
                            g_mapTrackLockTime[g_iSelectedTrackID] = time(NULL);
                            UNLOCK_LOCKTIME_MUTEX();
                            
                            std::cout << "=== Track Selected ===" << std::endl;
                            std::cout << "Track ID: " << g_iSelectedTrackID << std::endl;
                            std::cout << "🎯 Face locked! Feature extraction will start in " 
                                      << FEATURE_EXTRACT_LOCK_SECONDS << " seconds..." << std::endl;
                            std::cout << "=====================" << std::endl;
                        } else {
                            std::cout << "❌ No face at center crosshair (threshold: " << center_threshold << "px)" << std::endl;
                            std::cout << "   Move a face to center and press again" << std::endl;
                        }
                    } else {
                        std::cout << "❌ No faces detected (stTracker.size = " << stTracker.size << ")" << std::endl;
                    }
                    
                    ButtonHandler_ClearPressType(pstHandler->buttonHandler);
                } 
                else if (pressType == BUTTON_PRESS_LONG) {
                    std::cout << "🔘 Button pressed (LONG)" << std::endl;
                    
                    // 長按：取消選中
                    LOCK_SELECTED_TRACK_MUTEX();
                    if (g_iSelectedTrackID != -1) {
                        std::cout << "=== Track Deselected ===" << std::endl;
                        std::cout << "Track ID " << g_iSelectedTrackID << " unlocked" << std::endl;
                        std::cout << "Red frame removed" << std::endl;
                        std::cout << "========================" << std::endl;
                        g_iSelectedTrackID = -1;
                        
                        // 清理鎖定時間和特徵（可選）
                        LOCK_LOCKTIME_MUTEX();
                        g_mapTrackLockTime.clear();
                        UNLOCK_LOCKTIME_MUTEX();
                    } else {
                        std::cout << "No track is currently selected" << std::endl;
                    }
                    UNLOCK_SELECTED_TRACK_MUTEX();
                    
                    ButtonHandler_ClearPressType(pstHandler->buttonHandler);
                }
            }
            
            // Extract features for tracked faces (if feature extractor is available)
            // 只對選中且鎖定超過 3 秒的人臉提取特徵
            if (pstHandler->featureExtractor && stTracker.size > 0) {
                LOCK_SELECTED_TRACK_MUTEX();
                int selectedID = g_iSelectedTrackID;
                UNLOCK_SELECTED_TRACK_MUTEX();
                
                if (selectedID != -1) {
                    // 檢查是否鎖定超過 3 秒
                    LOCK_LOCKTIME_MUTEX();
                    time_t lockTime = 0;
                    if (g_mapTrackLockTime.find(selectedID) != g_mapTrackLockTime.end()) {
                        lockTime = g_mapTrackLockTime[selectedID];
                    }
                    UNLOCK_LOCKTIME_MUTEX();
                    
                    time_t now = time(NULL);
                    int lockDuration = (int)(now - lockTime);
                    
                    // 顯示倒計時（僅在需要時）
                    static int last_remaining = -1;
                    if (lockDuration < FEATURE_EXTRACT_LOCK_SECONDS) {
                        int remaining = FEATURE_EXTRACT_LOCK_SECONDS - lockDuration;
                        if (remaining != last_remaining) {
                            std::cout << "⏱️  Track ID " << selectedID << " locked, feature extraction in " 
                                      << remaining << "s..." << std::endl;
                            last_remaining = remaining;
                        }
                    } else {
                        last_remaining = -1;
                        
                        // 找到選中的軌跡並提取特徵
                        for (uint32_t i = 0; i < stFaceMeta.size; i++) {
                            if (stTracker.info[i].id == selectedID) {
                                // 檢查是否已經提取過特徵
                                LOCK_FEATURE_MUTEX();
                                bool hasFeature = (g_mapTrackFeatures.find(selectedID) != g_mapTrackFeatures.end());
                                UNLOCK_FEATURE_MUTEX();
                                
                                if (!hasFeature) {
                                    std::cout << "🔍 Extracting feature for locked Track ID " << selectedID 
                                              << " (locked for " << lockDuration << "s)..." << std::endl;
                                    
                                    std::vector<float> feature;
                                    CVI_S32 feat_ret = pstHandler->featureExtractor->extractFeature(
                                        &stFrame,
                                        &stFaceMeta.info[i],
                                        feature
                                    );
                                    
                                    if (feat_ret == CVI_SUCCESS && feature.size() == 128) {
                                        // 存儲特徵到全局 map
                                        LOCK_FEATURE_MUTEX();
                                        g_mapTrackFeatures[selectedID] = feature;
                                        UNLOCK_FEATURE_MUTEX();
                                        
                                        // 填充到 face meta（供 DeepSORT 使用）
                                        if (!stFaceMeta.info[i].feature.ptr) {
                                            stFaceMeta.info[i].feature.ptr = (int8_t*)malloc(128);
                                        }
                                        
                                        for (int j = 0; j < 128; j++) {
                                            float val = feature[j] * 127.0f;
                                            val = val < -128.0f ? -128.0f : (val > 127.0f ? 127.0f : val);
                                            stFaceMeta.info[i].feature.ptr[j] = (int8_t)val;
                                        }
                                        stFaceMeta.info[i].feature.size = 128;
                                        stFaceMeta.info[i].feature.type = TYPE_INT8;
                                        
                                        std::cout << "✅ Feature extracted and stored for Track ID " << selectedID << std::endl;
                                    } else {
                                        std::cerr << "❌ Feature extraction failed for Track ID " << selectedID << std::endl;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        execution_time = ((t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec);
        

        frame_count++;
        gettimeofday(&fps_t1, NULL);
        unsigned long fps_elapsed = ((fps_t1.tv_sec - fps_t0.tv_sec) * 1000000 + fps_t1.tv_usec - fps_t0.tv_usec);
        if (fps_elapsed >= 1000000) { // 1 second
            current_fps = (float)frame_count * 1000000.0f / (float)fps_elapsed;
            {
                LOCK_FPS_MUTEX();
                g_fCurrentFPS = current_fps;
                UNLOCK_FPS_MUTEX();
            }
            frame_count = 0;
            fps_t0 = fps_t1;
        }
        
        if (stFaceMeta.size > 0) {
            std::cout << "=== Face Detection & Tracking Results ===" << std::endl;
            std::cout << "Face count: " << stFaceMeta.size << std::endl;
            std::cout << "Tracker count: " << stTracker.size << std::endl;
            std::cout << "Inference time: " << (float)execution_time / 1000 << " ms" << std::endl;
            std::cout << "FPS: " << current_fps << std::endl;
            std::cout << "Frame size: " << stFrame.stVFrame.u32Width << "x" 
                      << stFrame.stVFrame.u32Height << std::endl;
            
            for (uint32_t i = 0; i < stTracker.size; i++) {
                const char* state_str = "UNKNOWN";
                switch(stTracker.info[i].state) {
                    case CVI_TRACKER_NEW: state_str = "NEW"; break;
                    case CVI_TRACKER_UNSTABLE: state_str = "UNSTABLE"; break;
                    case CVI_TRACKER_STABLE: state_str = "STABLE"; break;
                }
                
                std::cout << "Track[" << i << "] ID=" << stTracker.info[i].id
                          << " state=" << state_str
                          << " bbox: x1=" << stTracker.info[i].bbox.x1 << ", "
                          << "y1=" << stTracker.info[i].bbox.y1 << ", "
                          << "x2=" << stTracker.info[i].bbox.x2 << ", "
                          << "y2=" << stTracker.info[i].bbox.y2 << std::endl;
            }
            std::cout << "========================================" << std::endl;
        } else if (stFaceMeta.size != s_u32LastFaceSize) {
            std::cout << "No face detected" << std::endl;
        }
        
        s_u32LastFaceSize = stFaceMeta.size;
        
        // 更新 OLED 顯示
        if (pstHandler->oledHandler && pstHandler->oledHandler->initialized) {
            // 準備 OLED 人臉框數據
            OLEDFaceBox_t oled_faces[32]; // 最多支持 32 個人臉
            uint32_t oled_face_count = std::min((uint32_t)stFaceMeta.size, (uint32_t)32);
            
            // 計算畫面中心點
            float frame_center_x = stFrame.stVFrame.u32Width / 2.0f;
            float frame_center_y = stFrame.stVFrame.u32Height / 2.0f;
            float center_threshold = 150.0f; // 中心對準閾值
            
            // 找出最接近中心的人臉
            int center_face_idx = -1;
            float min_distance = FLT_MAX;
            
            for (uint32_t i = 0; i < oled_face_count; i++) {
                oled_faces[i].x1 = stFaceMeta.info[i].bbox.x1;
                oled_faces[i].y1 = stFaceMeta.info[i].bbox.y1;
                oled_faces[i].x2 = stFaceMeta.info[i].bbox.x2;
                oled_faces[i].y2 = stFaceMeta.info[i].bbox.y2;
                oled_faces[i].score = stFaceMeta.info[i].bbox.score;
                oled_faces[i].is_center = 0;
                
                // 計算人臉中心到畫面中心的距離
                float face_center_x = (oled_faces[i].x1 + oled_faces[i].x2) / 2.0f;
                float face_center_y = (oled_faces[i].y1 + oled_faces[i].y2) / 2.0f;
                float dx = face_center_x - frame_center_x;
                float dy = face_center_y - frame_center_y;
                float distance = sqrt(dx * dx + dy * dy);
                
                if (distance < center_threshold && distance < min_distance) {
                    min_distance = distance;
                    center_face_idx = i;
                }
            }
            
            // 標記中心人臉
            if (center_face_idx >= 0) {
                oled_faces[center_face_idx].is_center = 1;
            }
            
            // 更新 OLED 顯示
            OLEDHandler_UpdateDisplay(pstHandler->oledHandler, oled_faces, 
                                     oled_face_count, current_fps);
        }
        
        // 更新全局人臉和追蹤數據
        {
            LOCK_RESULT_MUTEX();
            std::memset(&g_stFaceMeta, 0, sizeof(cvtdl_face_t));
            std::memset(&g_stTracker, 0, sizeof(cvtdl_tracker_t));
            if (stFaceMeta.info != nullptr) {
                CVI_TDL_CopyFaceMeta(&stFaceMeta, &g_stFaceMeta);
            }
            if (stTracker.info != nullptr) {
                CVI_TDL_CopyTrackerMeta(&stTracker, &g_stTracker);
            }
            UNLOCK_RESULT_MUTEX();
        }
        
        CVI_TDL_Free(&stFaceMeta);
        CVI_TDL_Free(&stTracker);
        CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
    }
    
    std::cout << "Exit TDL thread" << std::endl;
    pthread_exit(nullptr);
}
