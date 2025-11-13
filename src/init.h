#ifndef _INIT_H_
#define _INIT_H_

#include "middleware_utils.h"
#include "cvi_tdl.h"

typedef struct {
  SAMPLE_TDL_MW_CONTEXT stMWContext;
  cvitdl_handle_t stTDLHandle;
  cvitdl_service_handle_t stServiceHandle;
} APP_CONTEXT_S;


CVI_S32 APP_Init(APP_CONTEXT_S *pstAppCtx, const char *pszModelPath);
void APP_Deinit(APP_CONTEXT_S *pstAppCtx);

#endif 
