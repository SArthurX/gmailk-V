#ifndef TDL_HANDLER_H
#define TDL_HANDLER_H

#include "button_handler.h"
#include "cvi_tdl.h"

extern "C" {
#include <cvi_comm.h>
}

typedef struct {
    cvitdl_handle_t tdlHandle;
    cvitdl_service_handle_t serviceHandle;
    const char *modelPath;
    ButtonHandler_t *buttonHandler;
} TDLHandler_t;

CVI_S32 TDLHandler_Init(TDLHandler_t *pstHandler, const char *modelPath);


void TDLHandler_Cleanup(TDLHandler_t *pstHandler);

CVI_S32 TDLHandler_DetectFace(TDLHandler_t *pstHandler, 
                              VIDEO_FRAME_INFO_S *pstFrame, 
                              cvtdl_face_t *pstFaceMeta);

CVI_S32 TDLHandler_DrawFaceRect(TDLHandler_t *pstHandler,
                                cvtdl_face_t *pstFaceMeta,
                                VIDEO_FRAME_INFO_S *pstFrame);

void *TDLHandler_ThreadRoutine(void *pHandle);

void TDLHandler_SetButtonHandler(TDLHandler_t *pstHandler, ButtonHandler_t *buttonHandler);

CVI_S32 TDLHandler_CapturePhoto(VIDEO_FRAME_INFO_S *pstFrame, const char *filepath);

static inline void CVI_Mmap(VIDEO_FRAME_INFO_S *pstFrame, bool unmap = false){
    size_t image_size = pstFrame->stVFrame.u32Length[0] + pstFrame->stVFrame.u32Length[1] +
                    pstFrame->stVFrame.u32Length[2];
    if (!unmap) {
        pstFrame->stVFrame.pu8VirAddr[0] =
            (uint8_t *)CVI_SYS_Mmap(pstFrame->stVFrame.u64PhyAddr[0], image_size);
        pstFrame->stVFrame.pu8VirAddr[1] =
            pstFrame->stVFrame.pu8VirAddr[0] + pstFrame->stVFrame.u32Length[0];
        pstFrame->stVFrame.pu8VirAddr[2] =
            pstFrame->stVFrame.pu8VirAddr[1] + pstFrame->stVFrame.u32Length[1];
    } else {
        CVI_SYS_Munmap(pstFrame->stVFrame.pu8VirAddr[0], image_size);
        pstFrame->stVFrame.pu8VirAddr[0] = NULL;
        pstFrame->stVFrame.pu8VirAddr[1] = NULL;
        pstFrame->stVFrame.pu8VirAddr[2] = NULL;
    }
}

#endif // TDL_HANDLER_H
