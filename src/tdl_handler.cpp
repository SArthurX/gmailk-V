#include "tdl_handler.h"
#include "shared_data.h"
#include <iostream>
#include <cstring>
#include <sys/time.h>

extern "C" {
#include <cvi_sys.h>
#include "middleware_utils.h"
}

CVI_S32 TDLHandler_Init(TDLHandler_t *pstHandler, const char *modelPath) {
    if (!pstHandler || !modelPath) {
        std::cerr << "Invalid parameters for TDLHandler_Init" << std::endl;
        return CVI_FAILURE;
    }
    
    std::memset(pstHandler, 0, sizeof(TDLHandler_t));
    pstHandler->modelPath = modelPath;
    
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
    
    return CVI_TDL_Service_FaceDrawRect(pstHandler->serviceHandle, pstFaceMeta, 
                                        pstFrame, false, CVI_TDL_Service_GetDefaultBrush());
}

CVI_S32 TDLHandler_DumpVpssFrame(const char *filepath, VIDEO_FRAME_INFO_S *frame) {
    VIDEO_FRAME_S *pstVFSrc = &frame->stVFrame;
    int channels = 1;
    int num_pixels = 1;
    
    switch (pstVFSrc->enPixelFormat) {
        case PIXEL_FORMAT_YUV_400:
            channels = 1;
            break;
        case PIXEL_FORMAT_RGB_888_PLANAR:
            channels = 3;
            break;
        case PIXEL_FORMAT_RGB_888:
            channels = 1;
            num_pixels = 3;
            break;
        case PIXEL_FORMAT_NV21:
            channels = 2;
            break;
        default:
            std::cerr << "Unsupported conversion type: " << pstVFSrc->enPixelFormat << std::endl;
            return CVI_FAILURE;
    }
    
    FILE *fp = fopen(filepath, "wb");
    if (fp == nullptr) {
        std::cerr << "Failed to open: " << filepath << std::endl;
        return CVI_FAILURE;
    }
    
    uint32_t width = pstVFSrc->u32Width;
    uint32_t height = pstVFSrc->u32Height;
    fwrite(&width, sizeof(uint32_t), 1, fp);
    fwrite(&height, sizeof(uint32_t), 1, fp);
    uint32_t pix_format = (uint32_t)pstVFSrc->enPixelFormat;
    fwrite(&pix_format, sizeof(pix_format), 1, fp);
    
    for (int c = 0; c < channels; c++) {
        uint8_t *ptr_plane = pstVFSrc->pu8VirAddr[c];
        if (width * num_pixels == pstVFSrc->u32Stride[c]) {
            fwrite(pstVFSrc->pu8VirAddr[c], pstVFSrc->u32Stride[c] * height, 1, fp);
        } else {
            for (uint32_t r = 0; r < height; r++) {
                fwrite(ptr_plane, width * num_pixels, 1, fp);
                ptr_plane += pstVFSrc->u32Stride[c];
            }
        }
    }
    fclose(fp);
    return CVI_SUCCESS;
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
            static bool bFirstFrame = true;
            if (bFirstFrame) {
                std::cout << "=== Frame Information ===" << std::endl;
                std::cout << "Width: " << stFrame.stVFrame.u32Width << std::endl;
                std::cout << "Height: " << stFrame.stVFrame.u32Height << std::endl;
                std::cout << "Pixel Format: " << stFrame.stVFrame.enPixelFormat << std::endl;
                std::cout << "Stride[0]: " << stFrame.stVFrame.u32Stride[0] << std::endl;
                std::cout << "=========================" << std::endl;
                
                bool bNeedUnmap = false;
                if (stFrame.stVFrame.pu8VirAddr[0] == NULL) {
                    stFrame.stVFrame.pu8VirAddr[0] = (CVI_U8 *)CVI_SYS_Mmap(
                        stFrame.stVFrame.u64PhyAddr[0], stFrame.stVFrame.u32Length[0]);
                    stFrame.stVFrame.pu8VirAddr[1] = (CVI_U8 *)CVI_SYS_Mmap(
                        stFrame.stVFrame.u64PhyAddr[1], stFrame.stVFrame.u32Length[1]);
                    bNeedUnmap = true;
                }
                
                CVI_SYS_IonFlushCache(stFrame.stVFrame.u64PhyAddr[0], 
                                      stFrame.stVFrame.pu8VirAddr[0], 
                                      stFrame.stVFrame.u32Length[0]);
                CVI_SYS_IonFlushCache(stFrame.stVFrame.u64PhyAddr[1], 
                                      stFrame.stVFrame.pu8VirAddr[1], 
                                      stFrame.stVFrame.u32Length[1]);
                
                TDLHandler_DumpVpssFrame("first_frame.bin", &stFrame);
                std::cout << "First frame dumped to first_frame.bin" << std::endl;
                
                if (bNeedUnmap) {
                    CVI_SYS_Munmap((void *)stFrame.stVFrame.pu8VirAddr[0], stFrame.stVFrame.u32Length[0]);
                    CVI_SYS_Munmap((void *)stFrame.stVFrame.pu8VirAddr[1], stFrame.stVFrame.u32Length[1]);
                    stFrame.stVFrame.pu8VirAddr[0] = NULL;
                    stFrame.stVFrame.pu8VirAddr[1] = NULL;
                }
                
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
