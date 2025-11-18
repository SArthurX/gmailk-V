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
            CVI_TDL_Free(&stFaceMeta);
            CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
            if (s32Ret != CVI_SUCCESS) {
                g_bExit = true;
            }
            continue;
        }
        
        // 在畫面正中央繪製瞄準點
        {
            int center_x = stFrame.stVFrame.u32Width / 2;
            int center_y = stFrame.stVFrame.u32Height / 2;
            int cross_size = 20;
            
            cvtdl_service_brush_t center_brush;
            center_brush.color.r = 0.0f;
            center_brush.color.g = 255.0f;
            center_brush.color.b = 0.0f;
            center_brush.size = 2;
            
            // 繪製水平線
            cvtdl_pts_t h_line;
            h_line.size = 2;
            h_line.x = (float*)malloc(sizeof(float) * 2);
            h_line.y = (float*)malloc(sizeof(float) * 2);
            if (h_line.x && h_line.y) {
                h_line.x[0] = center_x - cross_size;
                h_line.y[0] = center_y;
                h_line.x[1] = center_x + cross_size;
                h_line.y[1] = center_y;
                CVI_TDL_Service_DrawPolygon(pstHandler->pstTDLHandler->serviceHandle, &stFrame, &h_line, center_brush);
                free(h_line.x);
                free(h_line.y);
            }
            
            // 繪製垂直線
            cvtdl_pts_t v_line;
            v_line.size = 2;
            v_line.x = (float*)malloc(sizeof(float) * 2);
            v_line.y = (float*)malloc(sizeof(float) * 2);
            if (v_line.x && v_line.y) {
                v_line.x[0] = center_x;
                v_line.y[0] = center_y - cross_size;
                v_line.x[1] = center_x;
                v_line.y[1] = center_y + cross_size;
                CVI_TDL_Service_DrawPolygon(pstHandler->pstTDLHandler->serviceHandle, &stFrame, &v_line, center_brush);
                free(v_line.x);
                free(v_line.y);
            }
            
            // 繪製中心點
            cvtdl_face_t center_dot = {0};
            center_dot.size = 1;
            center_dot.width = stFrame.stVFrame.u32Width;
            center_dot.height = stFrame.stVFrame.u32Height;
            center_dot.info = (cvtdl_face_info_t*)malloc(sizeof(cvtdl_face_info_t));
            if (center_dot.info) {
                memset(center_dot.info, 0, sizeof(cvtdl_face_info_t));
                center_dot.info[0].bbox.x1 = center_x - 3;
                center_dot.info[0].bbox.y1 = center_y - 3;
                center_dot.info[0].bbox.x2 = center_x + 3;
                center_dot.info[0].bbox.y2 = center_y + 3;
                
                cvtdl_service_brush_t dot_brush;
                dot_brush.color.r = 0.0f;
                dot_brush.color.g = 255.0f;
                dot_brush.color.b = 0.0f;
                dot_brush.size = 3;
                
                CVI_TDL_Service_FaceDrawRect(pstHandler->pstTDLHandler->serviceHandle, &center_dot, &stFrame, false, dot_brush);
                free(center_dot.info);
            }
        }
        
        // 發送畫面到 RTSP
        s32Ret = VENCHandler_SendFrameRTSP(&stFrame, pstHandler->pstMWContext);
        if (s32Ret != CVI_SUCCESS) {
            std::cerr << "Send output frame failed, ret=0x" << std::hex << s32Ret << std::endl;
        }
        
        CVI_TDL_Free(&stFaceMeta);
        CVI_VPSS_ReleaseChnFrame(0, 0, &stFrame);
        
        if (s32Ret != CVI_SUCCESS) {
            g_bExit = true;
        }
    }
    
    std::cout << "Exit encoder thread" << std::endl;
    pthread_exit(nullptr);
}
