#ifndef FEATURE_VERIFY_HELPER_HPP
#define FEATURE_VERIFY_HELPER_HPP

#include "tdl_handler.h"
#include "shared_data.h"
#include "face_feature_extractor.h"
#include "face_database.h"
#include "biohash_processor.h"
#include "remote_database.h"
#include <iostream>
#include <vector>
extern "C" {
#include <cvi_comm.h>
}

// BioHash 驗證：遠端資料庫優先，Fallback 到本地
static inline void VerifyAgainstDatabase(
    TDLHandler_t *pstHandler,
    const std::vector<float> &feature,
    int selectedID)
{
    PersonInfo_t match_person;
    int error_count = 0;
    int match_ret = -1;

    // 優先使用遠端資料庫 (RPi)
    if (pstHandler->remoteDatabase && pstHandler->remoteDatabase->initialized) {
        std::vector<PersonInfo_t> remote_persons;
        int fetch_ret = RemoteDatabase_FetchTemplates(
            pstHandler->remoteDatabase, remote_persons);

        if (fetch_ret == 0 && !remote_persons.empty()) {
            // 用遠端模板建立臨時資料庫
            FaceDatabase_t tempDB;
            tempDB.persons = remote_persons;
            tempDB.initialized = true;
            tempDB.similarity_threshold = 0.4f;

            match_ret = FaceDatabase_Verify(
                &tempDB, feature,
                *pstHandler->biohashProcessor,
                &match_person, error_count);
        } else {
            std::cerr << "⚠️  Remote fetch failed, trying local DB" << std::endl;
        }
    }

    // Fallback 到本地資料庫
    if (match_ret != 0 && pstHandler->faceDatabase
        && pstHandler->faceDatabase->initialized) {
        match_ret = FaceDatabase_Verify(
            pstHandler->faceDatabase,
            feature,
            *pstHandler->biohashProcessor,
            &match_person,
            error_count
        );
    }

    if (match_ret == 0) {
        // 找到匹配
        std::cout << "🎯 Match Found!" << std::endl;
        std::cout << "   Name: " << match_person.name << std::endl;
        std::cout << "   BCH Errors: " << error_count << std::endl;
        std::cout << "   Person ID: " << match_person.id << std::endl;

        // 存儲匹配結果
        LOCK_MATCH_RESULT_MUTEX();
        MatchResult result;
        result.name = match_person.name;
        result.bch_errors = error_count;
        result.person_id = match_person.id;
        g_mapTrackMatchResults[selectedID] = result;
        UNLOCK_MATCH_RESULT_MUTEX();
    } else {
        std::cout << "❌ No match found (BCH decode failed for all)" << std::endl;

        // 清除之前的匹配結果
        LOCK_MATCH_RESULT_MUTEX();
        g_mapTrackMatchResults.erase(selectedID);
        UNLOCK_MATCH_RESULT_MUTEX();
    }
}

// 對當前選中的 Track ID 提取 ArcFace 特徵，並進行 BioHash 驗證
// 從原 TDLHandler_ThreadRoutine L389-514 提取而來
static inline void ProcessFeatureAndVerify(
    TDLHandler_t *pstHandler,
    cvtdl_face_t *pstFaceMeta,
    cvtdl_tracker_t *pstTracker,
    VIDEO_FRAME_INFO_S *pstFrame)
{
    if (!pstHandler->featureExtractor || !pstTracker || pstTracker->size == 0)
        return;

    LOCK_SELECTED_TRACK_MUTEX();
    int selectedID = g_iSelectedTrackID;
    UNLOCK_SELECTED_TRACK_MUTEX();

    if (selectedID == -1)
        return;

    // 找到選中的軌跡並提取特徵
    for (uint32_t i = 0; i < pstFaceMeta->size; i++) {
        if ((int)pstTracker->info[i].id != selectedID)
            continue;

        // 檢查是否已經提取過特徵
        LOCK_FEATURE_MUTEX();
        bool hasFeature = (g_mapTrackFeatures.find(selectedID) != g_mapTrackFeatures.end());
        UNLOCK_FEATURE_MUTEX();

        if (hasFeature)
            break;

        std::cout << "🔍 Extracting feature for Track ID " << selectedID << "..." << std::endl;

        std::vector<float> feature;
        pthread_mutex_lock(&pstHandler->tdlMutex);
        CVI_S32 feat_ret = pstHandler->featureExtractor->extractFeature(
            pstFrame, &pstFaceMeta->info[i], feature);
        pthread_mutex_unlock(&pstHandler->tdlMutex);

        int expected_dim = pstHandler->featureExtractor->getFeatureDim();
        if (feat_ret != CVI_SUCCESS || (int)feature.size() != expected_dim) {
            std::cerr << "❌ Feature extraction failed for Track ID " << selectedID << std::endl;
            break;
        }

        // 調試：輸出前5個特徵值
        std::cout << "✅ Feature extracted for Track ID " << selectedID << std::endl;
        std::cout << "  Feature (first 5): ";
        for (int k = 0; k < 5; k++) {
            std::cout << feature[k] << " ";
        }
        std::cout << std::endl;

        // 存儲特徵到全局 map
        LOCK_FEATURE_MUTEX();
        g_mapTrackFeatures[selectedID] = feature;
        UNLOCK_FEATURE_MUTEX();

        // 填充到 face meta（供 DeepSORT 使用）
        if (!pstFaceMeta->info[i].feature.ptr) {
            pstFaceMeta->info[i].feature.ptr = (int8_t*)malloc(expected_dim);
        }

        for (int j = 0; j < expected_dim; j++) {
            float val = feature[j] * 127.0f;
            val = val < -128.0f ? -128.0f : (val > 127.0f ? 127.0f : val);
            pstFaceMeta->info[i].feature.ptr[j] = (int8_t)val;
        }
        pstFaceMeta->info[i].feature.size = expected_dim;
        pstFaceMeta->info[i].feature.type = TYPE_INT8;

        // 立即與資料庫比對 (BioHash + BCH)
        if (pstHandler->biohashProcessor) {
            VerifyAgainstDatabase(pstHandler, feature, selectedID);
        }

        break;
    }
}

#endif // FEATURE_VERIFY_HELPER_HPP
