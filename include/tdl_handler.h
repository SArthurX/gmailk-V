#ifndef TDL_HANDLER_H
#define TDL_HANDLER_H

extern "C" {
#include <cvi_comm.h>
}

#include "cvi_tdl.h"

typedef struct {
    cvitdl_handle_t tdlHandle;
    cvitdl_service_handle_t serviceHandle;
    const char *modelPath;
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

CVI_S32 TDLHandler_DumpVpssFrame(const char *filepath, VIDEO_FRAME_INFO_S *frame);

#endif // TDL_HANDLER_H
