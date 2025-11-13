#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

extern "C" {
#include <cvi_comm.h>
#include <sample_comm.h>
#include "middleware_utils.h"
}

typedef struct {
    SIZE_S stSensorSize;
    SIZE_S stVencSize;
    SAMPLE_TDL_MW_CONFIG_S stMWConfig;
} SystemConfig_t;

CVI_S32 SystemInit_GetSensorConfig(SystemConfig_t *pstConfig);
CVI_S32 SystemInit_SetupVBPool(SystemConfig_t *pstConfig);
CVI_S32 SystemInit_SetupVPSS(SystemConfig_t *pstConfig);
CVI_S32 SystemInit_SetupVENC(SystemConfig_t *pstConfig);
CVI_S32 SystemInit_SetupRTSP(SystemConfig_t *pstConfig);
CVI_S32 SystemInit_InitMiddleware(SystemConfig_t *pstConfig, SAMPLE_TDL_MW_CONTEXT *pstMWContext);

CVI_S32 SystemInit_All(SystemConfig_t *pstConfig, SAMPLE_TDL_MW_CONTEXT *pstMWContext);

void SystemInit_Cleanup(SAMPLE_TDL_MW_CONTEXT *pstMWContext);

#endif // SYSTEM_INIT_H
