#include <iostream>
#include <cstring>
#include "tdl_handler.h"
#include "shared_data.h"
#include "draw_utils.h"
#include "button_handler.h"
#include "face_feature_extractor.h"
#include "helpers/btn_helpers.hpp"
#include "helpers/geometry_helper.hpp"
#include "helpers/auto_lock_helper.hpp"
#include "helpers/fps_helper.hpp"
#include "helpers/oled_helper.hpp"

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
        
        if (pstHandler->featureExtractor && pstHandler->featureExtractor->isLoaded())
            std::cout << "✅ Feature extractor initialized successfully" << std::endl;
        else {
            std::cerr << "⚠️  Feature extractor initialization failed, tracking will work without features" << std::endl;
            if (pstHandler->featureExtractor) {
                delete pstHandler->featureExtractor;
                pstHandler->featureExtractor = nullptr;
            }
        }
    } else
        std::cout << "ℹ️  Feature extraction disabled (no ArcFace model specified)" << std::endl;
    
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
        if (pstHandler->serviceHandle)
            CVI_TDL_Service_DestroyHandle(pstHandler->serviceHandle);
        if (pstHandler->tdlHandle)
            CVI_TDL_DestroyHandle(pstHandler->tdlHandle);
        std::memset(pstHandler, 0, sizeof(TDLHandler_t));
    }
    std::cout << "TDL Handler cleaned up" << std::endl;
}

CVI_S32 TDLHandler_DetectFace(TDLHandler_t *pstHandler, 
                              VIDEO_FRAME_INFO_S *pstFrame, 
                              cvtdl_face_t *pstFaceMeta) {
    if (!pstHandler || !pstFrame || !pstFaceMeta)
        return CVI_FAILURE;
    
    return CVI_TDL_FaceDetection(pstHandler->tdlHandle, pstFrame, 
                                 CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, pstFaceMeta);
}

CVI_S32 TDLHandler_DrawFaceRect(TDLHandler_t *pstHandler,
                                cvtdl_face_t *pstFaceMeta,
                                VIDEO_FRAME_INFO_S *pstFrame,
                                cvtdl_tracker_t *pstTracker) {
    if (!pstHandler || !pstFaceMeta || !pstFrame)
        return CVI_FAILURE;

    // no face then return
    if (pstFaceMeta->size == 0)
        return CVI_SUCCESS;

    float frame_center_x = pstFrame->stVFrame.u32Width / 2.0f;
    float frame_center_y = pstFrame->stVFrame.u32Height / 2.0f;
    float center_threshold = 80.0f; 
    
    // find the face closest to the center crosshair (within threshold)
    int center_face_idx = FindCenterFaceIndex(
        pstFaceMeta,
        frame_center_x,
        frame_center_y,
        center_threshold
    );
    
    // Get selected track ID
    LOCK_SELECTED_TRACK_MUTEX();
    int selectedTrackID = g_iSelectedTrackID;
    UNLOCK_SELECTED_TRACK_MUTEX();
    
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
            else if ((int)i == center_face_idx) 
                brush = BRUSH_YELLOW;  // 中心位置的人臉：黃色框（提示可選中）
            // 3. 最低優先級：根據追蹤狀態著色
            else if (pstTracker->info[i].state == CVI_TRACKER_STABLE)
                brush = BRUSH_GREEN;  // 穩定追蹤：綠色框
            else if (pstTracker->info[i].state == CVI_TRACKER_NEW)
                brush = BRUSH_YELLOW;  // 新追蹤：黃色框
            else
                brush = BRUSH_BLUE;  // 不穩定追蹤：藍色框
        } else 
            brush = BRUSH_BLUE;  // 無追蹤資訊：藍色框
  
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
                
                // 檢查是否有匹配結果
                LOCK_MATCH_RESULT_MUTEX();
                bool hasMatch = (g_mapTrackMatchResults.find(pstTracker->info[i].id) != g_mapTrackMatchResults.end());
                MatchResult matchResult;
                if (hasMatch) {
                    matchResult = g_mapTrackMatchResults[pstTracker->info[i].id];
                }
                UNLOCK_MATCH_RESULT_MUTEX();
                
                if (hasMatch) {
                    // 顯示匹配的姓名和相似度
                    char name_text[128];
                    snprintf(name_text, sizeof(name_text), "%s (%.2f)", 
                            matchResult.name.c_str(), matchResult.similarity);
                    
                    CVI_TDL_Service_ObjectWriteText(name_text, text_x, text_y - text_y_offset, pstFrame,
                                                   0.0f, 255.0f, 0.0f);  // Green text for match
                    text_y_offset += 15;
                } else if (is_selected) {
                    // 如果已選中但無匹配，顯示 "Unknown" 或特徵數據
                    LOCK_FEATURE_MUTEX();
                    bool hasFeature = (g_mapTrackFeatures.find(pstTracker->info[i].id) != g_mapTrackFeatures.end());
                    UNLOCK_FEATURE_MUTEX();
                    
                    if (hasFeature) {
                        // 已提取特徵但無匹配
                        CVI_TDL_Service_ObjectWriteText("Unknown", text_x, text_y - text_y_offset, pstFrame,
                                                       255.0f, 255.0f, 0.0f);  // Yellow text
                        text_y_offset += 15;
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
    
    struct timeval t0, t1;
    unsigned long execution_time;
    FPSCalculator_t fps_calculator;
    FPSCalculator_Init(&fps_calculator);
    static uint32_t s_u32LastFaceSize = 0;
    
    while (!g_bExit) {
        s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);
        
        // Get selected track ID
        LOCK_SELECTED_TRACK_MUTEX();
        int selectedTrackID = g_iSelectedTrackID;
        UNLOCK_SELECTED_TRACK_MUTEX();
        
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
            
            // === 自動鎖定機制：檢測在中心超過3秒的人臉 ===
            if (stTracker.size > 0) {
                ProcessAutoLock(&stFaceMeta, &stTracker, &stFrame);
            }
            
            // === 按鈕處理（在追蹤完成後） ===
            if (pstHandler->buttonHandler) {
                ButtonHandler_Inputs(pstHandler);
                ButtonHandler_ClearPressType(pstHandler->buttonHandler);
            }

            // Extract features for tracked faces (if feature extractor is available)
            // 對選中的人臉立即提取特徵（已在自動鎖定時等待3秒）
            if (pstHandler->featureExtractor && stTracker.size > 0) {
                LOCK_SELECTED_TRACK_MUTEX();
                int selectedID = g_iSelectedTrackID;
                UNLOCK_SELECTED_TRACK_MUTEX();
                
                if (selectedID != -1) {
                    // 找到選中的軌跡並提取特徵
                    for (uint32_t i = 0; i < stFaceMeta.size; i++) {
                        if (stTracker.info[i].id == selectedID) {
                            // 檢查是否已經提取過特徵
                            LOCK_FEATURE_MUTEX();
                            bool hasFeature = (g_mapTrackFeatures.find(selectedID) != g_mapTrackFeatures.end());
                            UNLOCK_FEATURE_MUTEX();
                            
                            if (!hasFeature) {
                                std::cout << "🔍 Extracting feature for Track ID " << selectedID << "..." << std::endl;
                                
                                std::vector<float> feature;
                                CVI_S32 feat_ret = pstHandler->featureExtractor->extractFeature(
                                    &stFrame,
                                    &stFaceMeta.info[i],
                                    feature
                                );
                                
                                if (feat_ret == CVI_SUCCESS && feature.size() == 128) {
                                    // 調試：輸出前5個特徵值
                                    std::cout << "✅ Feature extracted for Track ID " << selectedID << std::endl;
                                    std::cout << "  Feature (first 5): ";
                                    for (int k = 0; k < 5; k++) {
                                        std::cout << feature[k] << " ";
                                    }
                                    std::cout << std::endl;
                                    
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
                                    
                                    // 立即與資料庫比對
                                    if (pstHandler->faceDatabase && pstHandler->faceDatabase->initialized) {
                                        PersonInfo_t match_person;
                                        int match_ret = FaceDatabase_Match(
                                            pstHandler->faceDatabase,
                                            feature.data(),
                                            feature.size(),
                                            &match_person
                                        );
                                        
                                        if (match_ret == 0) {
                                            // 找到匹配
                                            std::cout << "🎯 Match Found!" << std::endl;
                                            std::cout << "   Name: " << match_person.name << std::endl;
                                            std::cout << "   Similarity: " << match_person.similarity << std::endl;
                                            std::cout << "   Person ID: " << match_person.id << std::endl;
                                            
                                            // 存儲匹配結果
                                            LOCK_MATCH_RESULT_MUTEX();
                                            MatchResult result;
                                            result.name = match_person.name;
                                            result.similarity = match_person.similarity;
                                            result.person_id = match_person.id;
                                            g_mapTrackMatchResults[selectedID] = result;
                                            UNLOCK_MATCH_RESULT_MUTEX();
                                        } else {
                                            std::cout << "❌ No match found (similarity below threshold)" << std::endl;
                                            
                                            // 清除之前的匹配結果
                                            LOCK_MATCH_RESULT_MUTEX();
                                            g_mapTrackMatchResults.erase(selectedID);
                                            UNLOCK_MATCH_RESULT_MUTEX();
                                        }
                                    }
                                } else
                                    std::cerr << "❌ Feature extraction failed for Track ID " << selectedID << std::endl;
                            }
                            break;
                        }
                    }
                }
            }
        }
        
        execution_time = ((t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec);
        FPSCalculator_Update(&fps_calculator);
        float current_fps = FPSCalculator_GetFPS(&fps_calculator);

        
        // if (stFaceMeta.size > 0) {
        //     std::cout << "=== Face Detection & Tracking Results ===" << std::endl;
        //     std::cout << "Face count: " << stFaceMeta.size << std::endl;
        //     std::cout << "Tracker count: " << stTracker.size << std::endl;
        //     std::cout << "Inference time: " << (float)execution_time / 1000 << " ms" << std::endl;
        //     std::cout << "FPS: " << current_fps << std::endl;
        //     std::cout << "Frame size: " << stFrame.stVFrame.u32Width << "x" 
        //               << stFrame.stVFrame.u32Height << std::endl;
            
        //     for (uint32_t i = 0; i < stTracker.size; i++) {
        //         const char* state_str = "UNKNOWN";
        //         switch(stTracker.info[i].state) {
        //             case CVI_TRACKER_NEW: state_str = "NEW"; break;
        //             case CVI_TRACKER_UNSTABLE: state_str = "UNSTABLE"; break;
        //             case CVI_TRACKER_STABLE: state_str = "STABLE"; break;
        //         }
                
        //         std::cout << "Track[" << i << "] ID=" << stTracker.info[i].id
        //                   << " state=" << state_str
        //                   << " bbox: x1=" << stTracker.info[i].bbox.x1 << ", "
        //                   << "y1=" << stTracker.info[i].bbox.y1 << ", "
        //                   << "x2=" << stTracker.info[i].bbox.x2 << ", "
        //                   << "y2=" << stTracker.info[i].bbox.y2 << std::endl;
        //     }
        //     std::cout << "========================================" << std::endl;
        // } else if (stFaceMeta.size != s_u32LastFaceSize) {
        //     std::cout << "No face detected" << std::endl;
        // }
        
        s_u32LastFaceSize = stFaceMeta.size;
        
        // 更新 OLED 顯示
        UpdateOLEDDisplay(pstHandler->oledHandler, &stFaceMeta, &stFrame, current_fps);

        
        // 更新全局人臉和追蹤數據
        {
            LOCK_RESULT_MUTEX();
            std::memset(&g_stFaceMeta, 0, sizeof(cvtdl_face_t));
            std::memset(&g_stTracker, 0, sizeof(cvtdl_tracker_t));
            if (stFaceMeta.info != nullptr)
                CVI_TDL_CopyFaceMeta(&stFaceMeta, &g_stFaceMeta);
            if (stTracker.info != nullptr)
                CVI_TDL_CopyTrackerMeta(&stTracker, &g_stTracker);
            UNLOCK_RESULT_MUTEX();
        }
        
        CVI_TDL_Free(&stFaceMeta);
        CVI_TDL_Free(&stTracker);
        CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
    }
    
    std::cout << "Exit TDL thread" << std::endl;
    pthread_exit(nullptr);
}


