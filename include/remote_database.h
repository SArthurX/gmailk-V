#ifndef REMOTE_DATABASE_H
#define REMOTE_DATABASE_H

#include <string>
#include <vector>
#include <ctime>
#include "face_database.h"

// 遠端資料庫處理器（RPi HTTP Client）
typedef struct {
    std::string base_url;       // e.g. "http://192.168.42.1:8787"
    bool initialized;
    bool connected;             // 最近一次連線是否成功
    
    // 本地快取
    std::vector<PersonInfo_t> cached_templates;
    bool cache_valid;
    time_t cache_time;              // 快取建立時間
    int cache_ttl_seconds;          // 快取有效秒數（預設 30）
} RemoteDatabase_t;

/**
 * @brief 初始化遠端資料庫
 * @param db 遠端資料庫處理器
 * @param base_url RPi Server 的 base URL (e.g. "http://192.168.42.1:3000")
 * @return 0 成功，-1 失敗
 */
int RemoteDatabase_Init(RemoteDatabase_t* db, const char* base_url);

/**
 * @brief 從 RPi 取得所有已完成的模板 (GET /api/templates)
 * @param db 遠端資料庫處理器
 * @param persons 輸出：人員列表（含碼字）
 * @return 0 成功，-1 失敗
 */
int RemoteDatabase_FetchTemplates(RemoteDatabase_t* db, std::vector<PersonInfo_t>& persons);

/**
 * @brief 裝置端直接註冊 (POST /api/persons)
 * @param db 遠端資料庫處理器
 * @param name 人員姓名
 * @param template_hex BioHash 模板 (hex 編碼)
 * @return 新增的人員 ID (>0 成功)，-1 失敗
 */
int RemoteDatabase_CreatePerson(RemoteDatabase_t* db, const char* name,
                                 const std::string& template_hex);

/**
 * @brief 檢查 RPi 連線狀態 (GET /api/status)
 * @param db 遠端資料庫處理器
 * @return true 連線正常，false 連線失敗
 */
bool RemoteDatabase_CheckConnection(RemoteDatabase_t* db);

/**
 * @brief 使快取失效，下次驗證時重新抓取
 * @param db 遠端資料庫處理器
 */
void RemoteDatabase_InvalidateCache(RemoteDatabase_t* db);

/**
 * @brief 清理遠端資料庫
 * @param db 遠端資料庫處理器
 */
void RemoteDatabase_Cleanup(RemoteDatabase_t* db);

#endif // REMOTE_DATABASE_H
