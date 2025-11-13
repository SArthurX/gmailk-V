#include "venc_handler.h"
#include "shared_data.h"
#include <iostream>
#include <cstring>

extern "C" {
#include "middleware_utils.h"
#include "sample_utils.h"
}

CVI_S32 VENCHandler_SendFrameRTSP(VIDEO_FRAME_INFO_S *pstFrame, 
                                  SAMPLE_TDL_MW_CONTEXT *pstMWContext) {
    return SAMPLE_TDL_Send_Frame_RTSP(pstFrame, pstMWContext);
}

void *VENCHandler_ThreadRoutine(void *pArgs) {
    std::cout << "Enter encoder thread" << std::endl;
    
    VENCHandler_t *pstHandler = static_cast<VENCHandler_t *>(pArgs);
    VIDEO_FRAME_INFO_S stFrame;
    cvtdl_face_t stFaceMeta = {0};
    CVI_S32 s32Ret;
    
    while (!g_bExit) {
        s32Ret = CVI_VPSS_GetChnFrame(0, 0, &stFrame, 2000);
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << "CVI_VPSS_GetChnFrame chn0 failed with 0x" 
                      << std::hex << s32Ret << std::endl;
            break;
        }
        
        // 複製全局人臉檢測結果
        {
            LOCK_RESULT_MUTEX();
            std::memset(&stFaceMeta, 0, sizeof(cvtdl_face_t));
            if (g_stFaceMeta.info != nullptr) {
                CVI_TDL_CopyFaceMeta(&g_stFaceMeta, &stFaceMeta);
            }
            UNLOCK_RESULT_MUTEX();
        }
        
        // 在畫面上繪製人臉框
        s32Ret = TDLHandler_DrawFaceRect(pstHandler->pstTDLHandler, &stFaceMeta, &stFrame);
        if (s32Ret != CVI_TDL_SUCCESS) {
            std::cerr << "Draw frame failed, ret=0x" << std::hex << s32Ret << std::endl;
            CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
            goto error;
        }
        
        // 發送畫面到 RTSP
        s32Ret = VENCHandler_SendFrameRTSP(&stFrame, pstHandler->pstMWContext);
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << "Send output frame failed, ret=0x" << std::hex << s32Ret << std::endl;
            CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
            goto error;
        }
        
    error:
        CVI_TDL_Free(&stFaceMeta);
        CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
        
        if (s32Ret != CVI_SUCCESS) {
            g_bExit = true;
        }
    }
    
    std::cout << "Exit encoder thread" << std::endl;
    pthread_exit(nullptr);
}
