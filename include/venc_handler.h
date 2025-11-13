#ifndef VENC_HANDLER_H
#define VENC_HANDLER_H

extern "C" {
#include <cvi_comm.h>
#include <sample_comm.h>
#include "middleware_utils.h"
}

#include "cvi_tdl.h"
#include "tdl_handler.h"

typedef struct {
    SAMPLE_TDL_MW_CONTEXT *pstMWContext;
    TDLHandler_t *pstTDLHandler;
} VENCHandler_t;

void *VENCHandler_ThreadRoutine(void *pArgs);

CVI_S32 VENCHandler_SendFrameRTSP(VIDEO_FRAME_INFO_S *pstFrame, 
                                  SAMPLE_TDL_MW_CONTEXT *pstMWContext);

#endif // VENC_HANDLER_H
