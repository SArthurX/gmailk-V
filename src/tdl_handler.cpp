#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cstdio>
#include <set>
#include "tdl_handler.h"
#include "cvi_tdl_media.h"
#include "shared_data.h"
#include "draw_utils.h"
#include "button_handler.h"
#include "face_feature_extractor.h"
#include "tdl/core/cvi_tdl_utils.h"
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
                        const char *arcfaceCvimodel) {
    if (!pstHandler || !modelPath) {
        std::cerr << "Invalid parameters for TDLHandler_Init" << std::endl;
        return CVI_FAILURE;
    }
    
    std::memset(pstHandler, 0, sizeof(TDLHandler_t));
    if (pthread_mutex_init(&pstHandler->tdlMutex, nullptr) != 0) {
        std::cerr << "Failed to initialize TDL mutex" << std::endl;
        return CVI_FAILURE;
    }
    pstHandler->modelPath = modelPath;
    pstHandler->arcfaceCvimodelPath = arcfaceCvimodel;
    pstHandler->buttonHandler = nullptr;
    pstHandler->featureExtractor = nullptr;
    pstHandler->oledHandler = nullptr;
    pstHandler->biohashProcessor = nullptr;
    pstHandler->remoteDatabase = nullptr;
    
    // Create TDL handle and assign VPSS Grp1 Device 0 to TDL SDK
    CVI_S32 s32Ret = CVI_TDL_CreateHandle(&pstHandler->tdlHandle);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to create TDL handle, ret=0x" << std::hex << s32Ret << std::endl;
        goto FAIL;
    }
    
    // Set VBPool for TDL
    s32Ret = CVI_TDL_SetVBPool(pstHandler->tdlHandle, 0, 2);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to set VBPool, ret=0x" << std::hex << s32Ret << std::endl;
        goto FAIL;
    }
    
    // Set VPSS timeout
    CVI_TDL_SetVpssTimeout(pstHandler->tdlHandle, 1000);
    
    // Create service handle
    s32Ret = CVI_TDL_Service_CreateHandle(&pstHandler->serviceHandle, pstHandler->tdlHandle);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to create service handle, ret=0x" << std::hex << s32Ret << std::endl;
        goto FAIL;
    }

    // Open face detection model
    s32Ret = CVI_TDL_OpenModel(pstHandler->tdlHandle, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, modelPath);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to open model, ret=0x" << std::hex << s32Ret << std::endl;
        goto FAIL;
    }
    
    // Initialize DeepSORT tracker
    s32Ret = CVI_TDL_DeepSORT_Init(pstHandler->tdlHandle, false);
    if (s32Ret != CVI_SUCCESS) {
        std::cerr << "Failed to initialize DeepSORT, ret=0x" << std::hex << s32Ret << std::endl;
        goto FAIL;
    }
    
    // Configure DeepSORT parameters for face tracking
    cvtdl_deepsort_config_t ds_conf;
    CVI_TDL_DeepSORT_GetDefaultConfig(&ds_conf);
    
    ds_conf.ktracker_conf.max_unmatched_num = 90;      // ~3s @ 30fps
    ds_conf.ktracker_conf.accreditation_threshold = 10; // ~0.33s @ 30fps
    ds_conf.max_distance_iou = 0.5f;
    
    CVI_TDL_DeepSORT_SetConfig(pstHandler->tdlHandle, &ds_conf, -1, false);
    
    // Initialize ArcFace feature extractor (optional, TPU)
    if (arcfaceCvimodel) {
        pstHandler->featureExtractor = new FaceFeatureExtractor(
            arcfaceCvimodel,
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
    
    // Initialize BioHash processor
    pstHandler->biohashProcessor = new BioHashProcessor();
    
    std::cout << "TDL Handler initialized successfully" << std::endl;
    std::cout << "Model loaded: " << modelPath << std::endl;
    std::cout << "DeepSORT tracker initialized" << std::endl;
    
    return CVI_SUCCESS;

FAIL:
    if (pstHandler->featureExtractor) {
        delete pstHandler->featureExtractor;
        pstHandler->featureExtractor = nullptr;
    }
    if (pstHandler->biohashProcessor) {
        delete pstHandler->biohashProcessor;
        pstHandler->biohashProcessor = nullptr;
    }
    if (pstHandler->serviceHandle)
        CVI_TDL_Service_DestroyHandle(pstHandler->serviceHandle);
    if (pstHandler->tdlHandle)
        CVI_TDL_DestroyHandle(pstHandler->tdlHandle);
    pthread_mutex_destroy(&pstHandler->tdlMutex);
    std::memset(pstHandler, 0, sizeof(TDLHandler_t));
    return s32Ret;
}

void TDLHandler_Cleanup(TDLHandler_t *pstHandler) {
    if (!pstHandler)
        return;

    if (pstHandler->featureExtractor) {
        delete pstHandler->featureExtractor;
        pstHandler->featureExtractor = nullptr;
    }
    if (pstHandler->biohashProcessor) {
        delete pstHandler->biohashProcessor;
        pstHandler->biohashProcessor = nullptr;
    }
    if (pstHandler->serviceHandle)
        CVI_TDL_Service_DestroyHandle(pstHandler->serviceHandle);
    if (pstHandler->tdlHandle)
        CVI_TDL_DestroyHandle(pstHandler->tdlHandle);
    pthread_mutex_destroy(&pstHandler->tdlMutex);
    std::memset(pstHandler, 0, sizeof(TDLHandler_t));
    std::cout << "TDL Handler cleaned up" << std::endl;
}

CVI_S32 TDLHandler_DetectFace(TDLHandler_t *pstHandler, 
                              VIDEO_FRAME_INFO_S *pstFrame, 
                              cvtdl_face_t *pstFaceMeta) {
    if (!pstHandler || !pstFrame || !pstFaceMeta)
        return CVI_FAILURE;
    
    pthread_mutex_lock(&pstHandler->tdlMutex);
    CVI_S32 ret = CVI_TDL_FaceDetection(pstHandler->tdlHandle, pstFrame, 
                                 CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, pstFaceMeta);
    pthread_mutex_unlock(&pstHandler->tdlMutex);
    return ret;
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
                    snprintf(name_text, sizeof(name_text), "%s (err:%d)", 
                            matchResult.name.c_str(), matchResult.bch_errors);
                    
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
                                pthread_mutex_lock(&pstHandler->tdlMutex);
                                CVI_S32 feat_ret = pstHandler->featureExtractor->extractFeature(
                                    &stFrame,
                                    &stFaceMeta.info[i],
                                    feature
                                );
                                pthread_mutex_unlock(&pstHandler->tdlMutex);
                                
                                int expected_dim = pstHandler->featureExtractor->getFeatureDim();
                                if (feat_ret == CVI_SUCCESS && (int)feature.size() == expected_dim) {
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
                                        stFaceMeta.info[i].feature.ptr = (int8_t*)malloc(expected_dim);
                                    }
                                    
                                    for (int j = 0; j < expected_dim; j++) {
                                        float val = feature[j] * 127.0f;
                                        val = val < -128.0f ? -128.0f : (val > 127.0f ? 127.0f : val);
                                        stFaceMeta.info[i].feature.ptr[j] = (int8_t)val;
                                    }
                                    stFaceMeta.info[i].feature.size = expected_dim;
                                    stFaceMeta.info[i].feature.type = TYPE_INT8;
                                    
                                    // 立即與資料庫比對 (BioHash + BCH)
                                    if (pstHandler->biohashProcessor) {
                                        PersonInfo_t match_person;
                                        int error_count = 0;
                                        int match_ret = -1;
                                        
                                        // 優先使用遠端資料庫 (RPi)
                                        if (pstHandler->remoteDatabase && pstHandler->remoteDatabase->initialized) {
                                            std::vector<PersonInfo_t> remote_persons;
                                            int fetch_ret = RemoteDatabase_FetchTemplates(
                                                pstHandler->remoteDatabase, remote_persons);
                                            
                                            if (fetch_ret == 0 && !remote_persons.empty()) {
                                                // 用遠端模板建立臨時資料庫
                                                FaceDatabase_t tempDB;
                                                tempDB.persons = remote_persons;
                                                tempDB.initialized = true;
                                                tempDB.similarity_threshold = 0.4f;
                                                
                                                match_ret = FaceDatabase_Verify(
                                                    &tempDB, feature,
                                                    *pstHandler->biohashProcessor,
                                                    &match_person, error_count);
                                            } else {
                                                std::cerr << "⚠️  Remote fetch failed, trying local DB" << std::endl;
                                            }
                                        }
                                        
                                        // Fallback 到本地資料庫
                                        if (match_ret != 0 && pstHandler->faceDatabase 
                                            && pstHandler->faceDatabase->initialized) {
                                            match_ret = FaceDatabase_Verify(
                                                pstHandler->faceDatabase,
                                                feature,
                                                *pstHandler->biohashProcessor,
                                                &match_person,
                                                error_count
                                            );
                                        }
                                        
                                        if (match_ret == 0) {
                                            // 找到匹配
                                            std::cout << "🎯 Match Found!" << std::endl;
                                            std::cout << "   Name: " << match_person.name << std::endl;
                                            std::cout << "   BCH Errors: " << error_count << std::endl;
                                            std::cout << "   Person ID: " << match_person.id << std::endl;
                                            
                                            // 存儲匹配結果
                                            LOCK_MATCH_RESULT_MUTEX();
                                            MatchResult result;
                                            result.name = match_person.name;
                                            result.bch_errors = error_count;
                                            result.person_id = match_person.id;
                                            g_mapTrackMatchResults[selectedID] = result;
                                            UNLOCK_MATCH_RESULT_MUTEX();
                                        } else {
                                            std::cout << "❌ No match found (BCH decode failed for all)" << std::endl;
                                            
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
        
        // === 處理 Pending 註冊佇列（每幀最多處理一個） ===
        // 在 TDL Thread 內處理，unbind/rebind 與 GetChnFrame 在同一執行緒，無競爭
        {
            PendingTask_t pendingTask;
            bool hasTask = false;
            
            LOCK_PENDING_TASK_MUTEX();
            if (!g_vecPendingTasks.empty()) {
                pendingTask = g_vecPendingTasks.front();
                g_vecPendingTasks.erase(g_vecPendingTasks.begin());
                hasTask = true;
            }
            UNLOCK_PENDING_TASK_MUTEX();
            
            if (hasTask) {
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
        }
    }
    
    
    std::cout << "Exit TDL thread" << std::endl;
    pthread_exit(nullptr);
}

void* TDLHandler_RemoteDBThreadRoutine(void* pHandle) {
    TDLHandler_t* pstHandler = static_cast<TDLHandler_t*>(pHandle);
    RemoteDatabase_t* db = pstHandler->remoteDatabase;
    if (!db || !db->initialized) {
        return nullptr;
    }

    std::cout << "[Background] Started pending registration handler (network I/O only)..." << std::endl;

    // 追蹤已嘗試過的 person ID（避免重複下載失敗的照片）
    std::set<int> attempted_ids;

    while (!g_bExit) {
        if (!db->connected) {
            // 嘗試重新連線
            RemoteDatabase_CheckConnection(db);
            for (int i = 0; i < 5 && !g_bExit; i++) sleep(1);
            continue;
        }

        // === Phase 1: 上傳已完成的結果 (TDL Thread → RPi) ===
        {
            LOCK_COMPLETED_TASK_MUTEX();
            std::vector<CompletedTask_t> results = g_vecCompletedTasks;
            g_vecCompletedTasks.clear();
            UNLOCK_COMPLETED_TASK_MUTEX();

            for (const auto& result : results) {
                if (g_bExit) break;
                if (result.success) {
                    if (RemoteDatabase_CompletePerson(db, result.person_id, result.template_hex) == 0) {
                        std::cout << "[Background] ✅ Uploaded registration for person ID " << result.person_id << std::endl;
                        attempted_ids.erase(result.person_id);  // 成功，允許未來重新處理
                    } else {
                        std::cerr << "[Background] ⚠️ Failed to upload result for person ID " << result.person_id << std::endl;
                    }
                } else {
                    std::cerr << "[Background] ⚠️ TDL processing failed for person ID " << result.person_id << std::endl;
                }
            }
        }

        // === Phase 2: 拉取新的 pending 註冊並下載照片 ===
        {
            std::vector<PendingPerson_t> pending_list;
            if (RemoteDatabase_FetchPendingPersons(db, pending_list) == 0 && !pending_list.empty()) {
                // 過濾掉已在佇列中的任務
                LOCK_PENDING_TASK_MUTEX();
                std::vector<int> queued_ids;
                for (const auto& task : g_vecPendingTasks) {
                    queued_ids.push_back(task.person_id);
                }
                UNLOCK_PENDING_TASK_MUTEX();

                for (const auto& person : pending_list) {
                    if (g_bExit) break;

                    // 跳過已在佇列中的任務
                    bool already_queued = false;
                    for (int qid : queued_ids) {
                        if (qid == person.id) { already_queued = true; break; }
                    }
                    if (already_queued) continue;

                    // 跳過已嘗試但失敗的 ID
                    if (attempted_ids.count(person.id)) continue;

                    std::cout << "[Background] Downloading photo for " << person.name 
                              << " (ID: " << person.id << ")..." << std::endl;

                    std::string local_path = "/tmp/pending_photo_" + std::to_string(person.id) + ".jpg";
                    if (RemoteDatabase_DownloadPhoto(db, person.photo_path, local_path) == 0) {
                        // 推入佇列，等待 TDL Thread 處理
                        PendingTask_t task;
                        task.person_id = person.id;
                        task.name = person.name;
                        task.local_photo_path = local_path;

                        LOCK_PENDING_TASK_MUTEX();
                        g_vecPendingTasks.push_back(task);
                        UNLOCK_PENDING_TASK_MUTEX();

                        std::cout << "[Background] 📋 Queued photo for TDL processing: " << person.name << std::endl;
                        attempted_ids.insert(person.id);  // 標記已嘗試
                    } else {
                        std::cerr << "[Background] ⚠️ Failed to download photo for " << person.name << std::endl;
                    }
                }
            }
        }

        // 每 5 秒輪詢一次
        for (int i = 0; i < 5 && !g_bExit; i++) sleep(1);
    }
    
    std::cout << "[Background] Stopped pending registration handler." << std::endl;
    return nullptr;
}

CVI_S32 TDLHandler_ProcessImageAndEnroll(TDLHandler_t *pstHandler, const char *imgPath, std::string &outTemplateHex) {
    if (!pstHandler || !pstHandler->featureExtractor || !pstHandler->biohashProcessor || !imgPath) {
        std::cerr << "TDLHandler_ProcessImageAndEnroll: Missing component or image path" << std::endl;
        return CVI_FAILURE;
    }

    // === 使用官方 CVI_TDL_ReadImage API 讀取圖片 ===
    // 此 API 通過獨立的 imgprocess_t 管線處理，不依賴 VPSS Grp0 的綁定狀態
    imgprocess_t img_handle = NULL;
    CVI_TDL_Create_ImageProcessor(&img_handle);
    if (!img_handle) {
        std::cerr << "TDLHandler_ProcessImageAndEnroll: Failed to create ImageProcessor" << std::endl;
        return CVI_FAILURE;
    }
    
    VIDEO_FRAME_INFO_S frame;
    std::memset(&frame, 0, sizeof(VIDEO_FRAME_INFO_S));
    
    CVI_S32 ret = CVI_TDL_ReadImage(img_handle, imgPath, &frame, PIXEL_FORMAT_RGB_888_PLANAR);
    if (ret != CVI_SUCCESS || frame.stVFrame.u32Width == 0) {
        std::cerr << "TDLHandler_ProcessImageAndEnroll: ReadImage failed ret=0x" 
                  << std::hex << ret << std::dec 
                  << " size=" << frame.stVFrame.u32Width << "x" << frame.stVFrame.u32Height << std::endl;
        CVI_TDL_Destroy_ImageProcessor(img_handle);
        return CVI_FAILURE;
    }
    
    std::cout << "📸 Image loaded: " << frame.stVFrame.u32Width << "x" << frame.stVFrame.u32Height 
              << " format=" << frame.stVFrame.enPixelFormat << std::endl;
    
    // VPSS Grp0 仍需解綁才能讓 FaceDetection 的 VPSS 前處理接受記憶體幀
    MMF_CHN_S stSrcChn = {CVI_ID_VI, 0, 0};
    MMF_CHN_S stDestChn = {CVI_ID_VPSS, 0, 0};

    pthread_mutex_lock(&pstHandler->tdlMutex);
    CVI_SYS_UnBind(&stSrcChn, &stDestChn);
    usleep(100000);  // 100ms 排空管線
    
    cvtdl_face_t face_meta = {0};
    ret = CVI_TDL_FaceDetection(pstHandler->tdlHandle, &frame, CVI_TDL_SUPPORTED_MODEL_SCRFDFACE, &face_meta);
    
    CVI_SYS_Bind(&stSrcChn, &stDestChn);
    pthread_mutex_unlock(&pstHandler->tdlMutex);

    if (ret == CVI_TDL_SUCCESS && face_meta.size > 0) {
        std::cout << "👤 Face detected: " << face_meta.size << " face(s)" << std::endl;
        
        std::vector<float> feature;
        pthread_mutex_lock(&pstHandler->tdlMutex);
        CVI_S32 ext_ret = pstHandler->featureExtractor->extractFeature(&frame, &face_meta.info[0], feature);
        pthread_mutex_unlock(&pstHandler->tdlMutex);
        
        if (ext_ret == CVI_SUCCESS) {
            uint64_t seed = BioHashProcessor::get_datetime_seed();
            BioHashTemplate tmpl = pstHandler->biohashProcessor->enroll(feature, seed);
            if (tmpl.is_valid()) {
                outTemplateHex = tmpl.to_hex();
            } else {
                std::cerr << "TDLHandler_ProcessImageAndEnroll: Failed to generate BioHash" << std::endl;
                ret = CVI_FAILURE;
            }
        } else {
            std::cerr << "TDLHandler_ProcessImageAndEnroll: Failed to extract feature" << std::endl;
            ret = CVI_FAILURE;
        }
    } else {
        if (ret != CVI_TDL_SUCCESS) {
            std::cerr << "TDLHandler_ProcessImageAndEnroll: FaceDetection FAILED 0x" << std::hex << ret << std::dec << std::endl;
        } else {
            std::cerr << "TDLHandler_ProcessImageAndEnroll: No face detected (size=0)" << std::endl;
        }
        ret = CVI_FAILURE;
    }

    CVI_TDL_Free(&face_meta);
    CVI_TDL_ReleaseImage(img_handle, &frame);
    CVI_TDL_Destroy_ImageProcessor(img_handle);
    return ret;
}

