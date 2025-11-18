#include "tdl_handler.h"
#include "shared_data.h"
#include <iostream>
#include <cstring>
#include <sys/time.h>
#include <time.h>
#include <cmath>
#include <cfloat>

extern "C" {
#include <cvi_sys.h>
#include "middleware_utils.h"
#include "button_handler.h"
}

CVI_S32 TDLHandler_Init(TDLHandler_t *pstHandler, const char *modelPath) {
    if (!pstHandler || !modelPath) {
        std::cerr << "Invalid parameters for TDLHandler_Init" << std::endl;
        return CVI_FAILURE;
    }
    
    std::memset(pstHandler, 0, sizeof(TDLHandler_t));
    pstHandler->modelPath = modelPath;
    pstHandler->buttonHandler = nullptr;
    
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
    
    std::cout << "TDL Handler initialized successfully" << std::endl;
    std::cout << "Model loaded: " << modelPath << std::endl;
    
    return CVI_SUCCESS;
}

void TDLHandler_Cleanup(TDLHandler_t *pstHandler) {
    if (pstHandler) {
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
                                VIDEO_FRAME_INFO_S *pstFrame) {
    if (!pstHandler || !pstFaceMeta || !pstFrame) {
        return CVI_FAILURE;
    }

    // 如果沒有檢測到人臉，直接返回
    if (pstFaceMeta->size == 0) {
        return CVI_SUCCESS;
    }

    // 計算畫面中心點
    float frame_center_x = pstFrame->stVFrame.u32Width / 2.0f;
    float frame_center_y = pstFrame->stVFrame.u32Height / 2.0f;
    
    // 找出最接近畫面中心的人臉
    int center_face_idx = -1;
    float min_distance = FLT_MAX;
    
    for (uint32_t i = 0; i < pstFaceMeta->size; i++) {
        // 計算人臉框的中心點
        float face_center_x = (pstFaceMeta->info[i].bbox.x1 + pstFaceMeta->info[i].bbox.x2) / 2.0f;
        float face_center_y = (pstFaceMeta->info[i].bbox.y1 + pstFaceMeta->info[i].bbox.y2) / 2.0f;
        
        // 計算人臉中心到畫面中心的距離
        float dx = face_center_x - frame_center_x;
        float dy = face_center_y - frame_center_y;
        float distance = sqrt(dx * dx + dy * dy);
        
        if (distance < min_distance) {
            min_distance = distance;
            center_face_idx = i;
        }
    }
    
    // 分別繪製不同顏色的人臉框
    CVI_S32 s32Ret = CVI_SUCCESS;
    for (uint32_t i = 0; i < pstFaceMeta->size; i++) {
        cvtdl_service_brush_t brush;
        brush.size = 4;
        
        if ((int)i == center_face_idx) {
            // 中心人臉用紅色
            brush.color.r = 255.0f;
            brush.color.g = 0.0f;
            brush.color.b = 0.0f;
        } else {
            // 其他人臉用藍色
            brush.color.r = 0.0f;
            brush.color.g = 0.0f;
            brush.color.b = 255.0f;
        }
        
        // 創建單個人臉的臨時結構來繪製
        cvtdl_face_t single_face = {0};
        single_face.size = 1;
        single_face.width = pstFaceMeta->width;
        single_face.height = pstFaceMeta->height;
        single_face.info = (cvtdl_face_info_t *)malloc(sizeof(cvtdl_face_info_t));
        if (single_face.info) {
            memcpy(single_face.info, &pstFaceMeta->info[i], sizeof(cvtdl_face_info_t));
            
            s32Ret = CVI_TDL_Service_FaceDrawRect(pstHandler->serviceHandle, &single_face, 
                                                  pstFrame, false, brush);
            
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
    struct timeval t0, t1;
    unsigned long execution_time;
    CVI_S32 s32Ret;
    static uint32_t s_u32LastFaceSize = 0;
    
    while (!g_bExit) {
        s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);
        
        if (s32Ret == CVI_SUCCESS) {
            if (pstHandler->buttonHandler) {
                ButtonPressType_t pressType = ButtonHandler_GetPressType(pstHandler->buttonHandler);
                
                if (pressType == BUTTON_PRESS_SHORT) {
                    time_t now = time(NULL);
                    struct tm *t = localtime(&now);
                    char filename[256];
                    snprintf(filename, sizeof(filename), 
                            "capture_%04d%02d%02d_%02d%02d%02d.bin",
                            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                            t->tm_hour, t->tm_min, t->tm_sec);
                    
                    std::cout << "=== Short Press: Capturing Photo ===" << std::endl;
                    std::cout << "Filename: " << filename << std::endl;
                    
                    s32Ret = TDLHandler_CapturePhoto(&stFrame, filename);
                    if (s32Ret == CVI_SUCCESS) {
                        std::cout << "Photo captured successfully!" << std::endl;
                        std::cout << "Size: " << stFrame.stVFrame.u32Width << "x" 
                                  << stFrame.stVFrame.u32Height << std::endl;
                    } else {
                        std::cerr << "Failed to capture photo" << std::endl;
                    }
                    std::cout << "=====================================" << std::endl;
                    
                    ButtonHandler_ClearPressType(pstHandler->buttonHandler);
                } 
                else if (pressType == BUTTON_PRESS_LONG) {
                    std::cout << "=== Long Press: Special Function ===" << std::endl;
                    std::cout << "Long press detected - executing special function" << std::endl;
                    std::cout << "====================================" << std::endl;
                    
                    ButtonHandler_ClearPressType(pstHandler->buttonHandler);
                }
            }
            
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
            goto get_frame_failed;
        }
        
        std::memset(&stFaceMeta, 0, sizeof(cvtdl_face_t));
        gettimeofday(&t0, NULL);
        
        s32Ret = TDLHandler_DetectFace(pstHandler, &stFrame, &stFaceMeta);
        
        gettimeofday(&t1, NULL);
        
        if (s32Ret != CVI_TDL_SUCCESS) {
            std::cerr << "Inference failed, ret=0x" << std::hex << s32Ret << std::endl;
            goto inf_error;
        }
        
        execution_time = ((t1.tv_sec - t0.tv_sec) * 1000000 + t1.tv_usec - t0.tv_usec);
        
        if (stFaceMeta.size > 0) {
            std::cout << "=== Face Detection Results ===" << std::endl;
            std::cout << "Face count: " << stFaceMeta.size << std::endl;
            std::cout << "Inference time: " << (float)execution_time / 1000 << " ms" << std::endl;
            std::cout << "Frame size: " << stFrame.stVFrame.u32Width << "x" 
                      << stFrame.stVFrame.u32Height << std::endl;
            
            for (uint32_t i = 0; i < stFaceMeta.size; i++) {
                std::cout << "Face[" << i << "] bbox: "
                          << "x1=" << stFaceMeta.info[i].bbox.x1 << ", "
                          << "y1=" << stFaceMeta.info[i].bbox.y1 << ", "
                          << "x2=" << stFaceMeta.info[i].bbox.x2 << ", "
                          << "y2=" << stFaceMeta.info[i].bbox.y2 << ", "
                          << "score=" << stFaceMeta.info[i].bbox.score << std::endl;
            }
            std::cout << "=============================" << std::endl;
        } else if (stFaceMeta.size != s_u32LastFaceSize) {
            std::cout << "No face detected" << std::endl;
        }
        
        s_u32LastFaceSize = stFaceMeta.size;
        
        // 更新全局人臉數據
        {
            LOCK_RESULT_MUTEX();
            std::memset(&g_stFaceMeta, 0, sizeof(cvtdl_face_t));
            if (stFaceMeta.info != nullptr) {
                CVI_TDL_CopyFaceMeta(&stFaceMeta, &g_stFaceMeta);
            }
            UNLOCK_RESULT_MUTEX();
        }
        
    inf_error:
        CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
    get_frame_failed:
        CVI_TDL_Free(&stFaceMeta);
        if (s32Ret != CVI_SUCCESS) {
            g_bExit = true;
        }
    }
    
    std::cout << "Exit TDL thread" << std::endl;
    pthread_exit(nullptr);
}
