#ifndef TDL_HANDLER_H
#define TDL_HANDLER_H

#include "button_handler.h"
#include "cvi_tdl.h"
#include "oled_ctrl.h"
#include "face_database.h"
#include "biohash_processor.h"
#include "remote_database.h"
extern "C" {
#include <cvi_comm.h>
}

// Forward declaration
class FaceFeatureExtractor;

typedef struct {
    cvitdl_handle_t tdlHandle;
    cvitdl_service_handle_t serviceHandle;
    pthread_mutex_t tdlMutex;
    const char *modelPath;
    const char *arcfaceCvimodelPath;  // ArcFace .cvimodel 路徑（TPU）
    ButtonHandler_t *buttonHandler;
    FaceFeatureExtractor *featureExtractor;  // 特徵提取器
    OLEDHandler_t *oledHandler;
    FaceDatabase_t *faceDatabase;  // 人臉資料庫
    BioHashProcessor *biohashProcessor;  // BioHash 處理器
    RemoteDatabase_t *remoteDatabase;  // RPi 遠端資料庫 (HTTP)
} TDLHandler_t;

CVI_S32 TDLHandler_Init(TDLHandler_t *pstHandler, const char *modelPath,
                        const char *arcfaceCvimodel = nullptr);


void TDLHandler_Cleanup(TDLHandler_t *pstHandler);

CVI_S32 TDLHandler_DetectFace(TDLHandler_t *pstHandler, 
                              VIDEO_FRAME_INFO_S *pstFrame, 
                              cvtdl_face_t *pstFaceMeta);

CVI_S32 TDLHandler_DrawFaceRect(TDLHandler_t *pstHandler,
                                cvtdl_face_t *pstFaceMeta,
                                VIDEO_FRAME_INFO_S *pstFrame,
                                cvtdl_tracker_t *pstTracker = nullptr);
                                
CVI_S32 TDLHandler_CapturePhoto(VIDEO_FRAME_INFO_S *pstFrame, const char *filepath);

void *TDLHandler_ThreadRoutine(void *pHandle);
void *TDLHandler_RemoteDBThreadRoutine(void *pHandle);

CVI_S32 TDLHandler_ProcessImageAndEnroll(TDLHandler_t *pstHandler, const char *imgPath, std::string &outTemplateHex);


static inline void TDLHandler_SetButtonHandler(TDLHandler_t *pstHandler, ButtonHandler_t *buttonHandler) {
    if (pstHandler)
        pstHandler->buttonHandler = buttonHandler;
}

static inline void TDLHandler_SetOLEDHandler(TDLHandler_t *pstHandler, OLEDHandler_t *oledHandler) {
    if (pstHandler)
        pstHandler->oledHandler = oledHandler;
}

static inline void TDLHandler_SetFaceDatabase(TDLHandler_t *pstHandler, FaceDatabase_t *faceDatabase) {
    if (pstHandler)
        pstHandler->faceDatabase = faceDatabase;
}

static inline void TDLHandler_SetBioHashProcessor(TDLHandler_t *pstHandler, BioHashProcessor *processor) {
    if (pstHandler)
        pstHandler->biohashProcessor = processor;
}

static inline void TDLHandler_SetRemoteDatabase(TDLHandler_t *pstHandler, RemoteDatabase_t *remoteDB) {
    if (pstHandler)
        pstHandler->remoteDatabase = remoteDB;
}

// static inline float CalculateDistanceToCenter(const cvtdl_bbox_t& bbox, uint32_t frameW, uint32_t frameH) {
//     float frame_center_x = frameW / 2.0f;
//     float frame_center_y = frameH / 2.0f;
//     float face_center_x = (bbox.x1 + bbox.x2) / 2.0f;
//     float face_center_y = (bbox.y1 + bbox.y2) / 2.0f;
//     return sqrt(pow(face_center_x - frame_center_x, 2) + pow(face_center_y - frame_center_y, 2));
// }

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
