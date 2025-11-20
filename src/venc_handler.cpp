#include "venc_handler.h"
#include "shared_data.h"
#include "draw_utils.h"
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
        
        // copy face meta data from shared data
        {
            LOCK_RESULT_MUTEX();
            std::memset(&stFaceMeta, 0, sizeof(cvtdl_face_t));
            if (g_stFaceMeta.info != nullptr) {
                CVI_TDL_CopyFaceMeta(&g_stFaceMeta, &stFaceMeta);
            }
            UNLOCK_RESULT_MUTEX();
        }
        
        // draw face rectangles on the frame
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
        
        {
            int center_x = stFrame.stVFrame.u32Width / 2;
            int center_y = stFrame.stVFrame.u32Height / 2;
            int cross_size = 20;
            
            cvtdl_pts_t crosshair;
            crosshair.size = 4;
            crosshair.x = (float*)malloc(sizeof(float) * 4);
            crosshair.y = (float*)malloc(sizeof(float) * 4);
            
            if (crosshair.x && crosshair.y) {
                crosshair.x[0] = center_x - cross_size;
                crosshair.y[0] = center_y;
                crosshair.x[1] = center_x + cross_size;
                crosshair.y[1] = center_y;
                crosshair.x[2] = center_x;
                crosshair.y[2] = center_y - cross_size;
                crosshair.x[3] = center_x;
                crosshair.y[3] = center_y + cross_size;
                
                cvtdl_pts_t h_line;
                h_line.size = 2;
                h_line.x = &crosshair.x[0];
                h_line.y = &crosshair.y[0];
                CVI_TDL_Service_DrawPolygon(pstHandler->pstTDLHandler->serviceHandle, &stFrame, &h_line, BRUSH_GREEN);
                
                cvtdl_pts_t v_line;
                v_line.size = 2;
                v_line.x = &crosshair.x[2];
                v_line.y = &crosshair.y[2];
                CVI_TDL_Service_DrawPolygon(pstHandler->pstTDLHandler->serviceHandle, &stFrame, &v_line, BRUSH_GREEN);
                
                free(crosshair.x);
                free(crosshair.y);
            }
        }

        {
            float fps_value = 0.0f;
            {
                LOCK_FPS_MUTEX();
                fps_value = g_fCurrentFPS;
                UNLOCK_FPS_MUTEX();
            }
            
            char fps_text[10];
            snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", fps_value);
            
            // 繪製文字到畫面左上角
            CVI_TDL_Service_ObjectWriteText(fps_text, 10, 30, &stFrame, 0.0f, 255.0f, 0.0f);
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
