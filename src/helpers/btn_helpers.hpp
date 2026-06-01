#ifndef BTN_HELPERS_HPP
#define BTN_HELPERS_HPP

#include "tdl_handler.h"
#include "button_handler.h"
#include "shared_data.h"
#include "face_database.h"
#include "biohash_processor.h"
#include "remote_database.h"

// 短按處理：重新識別當前鎖定的人臉
static inline void HandleShortPress(TDLHandler_t *pstHandler) {
    std::cout << "🔘 Button pressed (SHORT)" << std::endl;
    
    LOCK_SELECTED_TRACK_MUTEX();
    int selectedID = g_iSelectedTrackID;
    UNLOCK_SELECTED_TRACK_MUTEX();
    
    if (selectedID != -1) {
        // 清除舊的特徵和匹配結果
        LOCK_FEATURE_MUTEX();
        g_mapTrackFeatures.erase(selectedID);
        UNLOCK_FEATURE_MUTEX();
        
        LOCK_MATCH_RESULT_MUTEX();
        g_mapTrackMatchResults.erase(selectedID);
        UNLOCK_MATCH_RESULT_MUTEX();
        
        // 重置鎖定時間，觸發重新提取特徵
        LOCK_LOCKTIME_MUTEX();
        g_mapTrackLockTime[selectedID] = time(NULL);
        UNLOCK_LOCKTIME_MUTEX();
        
        std::cout << "=== Re-identification Started ===" << std::endl;
        std::cout << "Track ID: " << selectedID << std::endl;
        std::cout << "🔄 Cleared previous data, re-extracting feature..." << std::endl;
        std::cout << "=================================" << std::endl;
    } else
        std::cout << "❌ No face is currently locked.\n  Wait for a face to be at center for 3 seconds" << std::endl;
}

// 長按處理：註冊當前選中的人臉到資料庫
static inline void HandleLongPress(TDLHandler_t *pstHandler) {
    std::cout << "🔘 Button pressed (LONG)" << std::endl;
    
    LOCK_SELECTED_TRACK_MUTEX();
    int selectedID = g_iSelectedTrackID;
    UNLOCK_SELECTED_TRACK_MUTEX();
    
    if (selectedID != -1) {
        // 檢查是否已經提取特徵
        LOCK_FEATURE_MUTEX();
        bool hasFeature = (g_mapTrackFeatures.find(selectedID) != g_mapTrackFeatures.end());
        std::vector<float> feature;
        if (hasFeature) {
            feature = g_mapTrackFeatures[selectedID];
        }
        UNLOCK_FEATURE_MUTEX();
        
        if (hasFeature && !feature.empty()) {
            // 有特徵，生成 BioHash 模板並註冊到資料庫
            if (pstHandler->faceDatabase && pstHandler->faceDatabase->initialized
                && pstHandler->biohashProcessor) {
                // 生成測試姓名（後續可改為用戶輸入）
                static int person_count = 0;
                char name[64];
                const char* test_names[] = {"eddie", "nany", "lol", "ccc", "cocoya", "sunba"};
                snprintf(name, sizeof(name), "%s", test_names[person_count % 6]);
                person_count++;
                
                // 生成 BioHash 模板 (Fuzzy Commitment v2)
                uint64_t seed = BioHashProcessor::get_datetime_seed();
                std::cout << "🔐 Generating BioHash v2 template (seed=" << seed << ")" << std::endl;
                EnrollResult enrollResult = pstHandler->biohashProcessor->enroll(feature, seed);
                // 按鈕註冊無酬載 → encrypted_payload 為空
                
                if (enrollResult.tmpl.is_valid()) {
                    int person_id = -1;
                    bool registered_remote = false;
                    
                    // 優先嘗試遠端註冊 (RPi HTTP)
                    if (pstHandler->remoteDatabase && pstHandler->remoteDatabase->initialized) {
                        person_id = RemoteDatabase_CreatePerson(
                            pstHandler->remoteDatabase,
                            name,
                            enrollResult.tmpl.to_hex()
                        );
                        if (person_id > 0) {
                            registered_remote = true;
                            std::cout << "=== Face Registered (RPi Remote) ===" << std::endl;
                        } else {
                            std::cerr << "⚠️  RPi registration failed, falling back to local" << std::endl;
                        }
                    }
                    
                    // Fallback 到本地資料庫
                    if (!registered_remote) {
                        person_id = FaceDatabase_AddPerson(
                            pstHandler->faceDatabase,
                            name,
                            enrollResult.tmpl.to_hex()
                        );
                    }
                    
                    if (person_id > 0) {
                        std::cout << "=== Face Registered (BioHash v2) ===" << std::endl;
                        std::cout << "Person added to " 
                                  << (registered_remote ? "RPi" : "local") << " database!" << std::endl;
                        std::cout << "ID: " << person_id << std::endl;
                        std::cout << "Name: " << name << std::endl;
                        std::cout << "Track ID: " << selectedID << std::endl;
                        std::cout << "Template (hex): " << enrollResult.tmpl.to_hex().substr(0, 40) << "..." << std::endl;
                        std::cout << "================================" << std::endl;
                    } else
                        std::cerr << "❌ Failed to add person to database" << std::endl;
                } else
                    std::cerr << "❌ BioHash template generation failed" << std::endl;
            } else
                std::cerr << "❌ Face database or BioHash processor not initialized" << std::endl;
        } else
            std::cout << "❌ No feature extracted for Track ID： " << selectedID 
                        << "\nPlease wait for feature extraction to complete" << std::endl;
    } else
        std::cout << "❌ No face is currently locked\n Short press to lock a face first" << std::endl;
}

// 主按鈕輸入處理函數
static inline void ButtonHandler_Inputs(TDLHandler_t *pstHandler) {
    if (!pstHandler->buttonHandler) return;

    ButtonPressType_t pressType = ButtonHandler_GetPressType(pstHandler->buttonHandler);

    if (pressType == BUTTON_PRESS_SHORT)
        HandleShortPress(pstHandler);
    else if (pressType == BUTTON_PRESS_LONG)
        HandleLongPress(pstHandler);
}

#endif // BTN_HELPERS_HPP
