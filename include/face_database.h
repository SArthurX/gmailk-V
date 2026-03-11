#ifndef FACE_DATABASE_H
#define FACE_DATABASE_H

#include <string>
#include <vector>

class BioHashProcessor;

#ifdef __cplusplus
extern "C" {
#endif

// 人員資訊結構
typedef struct {
  int id;
  std::string name;
  std::string biohash_template_hex;  // BioHash 保護模板 (hex 編碼)
  int bch_errors;                     // 驗證時 BCH 糾正的錯誤數
} PersonInfo_t;

// 資料庫處理器
// 注意：
// - persons 是記憶體中的資料庫（即時更新）
// - database_path 指向持久化檔案（啟動時讀取，註冊時寫入）
// - 運行中手動修改檔案不會自動重載到記憶體
typedef struct {
  std::string database_path;
  std::vector<PersonInfo_t> persons;  // 記憶體中的資料庫
  bool initialized;
  float similarity_threshold;  // 相似度閾值
} FaceDatabase_t;

/**
 * @brief 初始化人臉資料庫
 * @param database 資料庫處理器
 * @param db_path 資料庫檔案路徑
 * @param threshold 相似度閾值（預設 0.4）
 * @return 0 成功，-1 失敗
 */
int FaceDatabase_Init(FaceDatabase_t* database, const char* db_path, float threshold = 0.4f);

/**
 * @brief 載入資料庫
 * @param database 資料庫處理器
 * @return 0 成功，-1 失敗
 */
int FaceDatabase_Load(FaceDatabase_t* database);

/**
 * @brief 儲存資料庫
 * @param database 資料庫處理器
 * @return 0 成功，-1 失敗
 */
int FaceDatabase_Save(FaceDatabase_t* database);

/**
 * @brief 新增人員到資料庫
 * @param database 資料庫處理器
 * @param name 人員姓名
 * @param template_hex BioHash 保護模板 (hex 編碼)
 * @return 新增的人員 ID，失敗返回 -1
 * @note 會立即更新記憶體資料庫並寫入檔案
 */
int FaceDatabase_AddPerson(FaceDatabase_t* database, const char* name, 
                            const std::string& template_hex);

/**
 * @brief 驗證人臉特徵，使用 BioHash + BCH 解碼進行比對
 * @param database 資料庫處理器
 * @param feature 要驗證的特徵向量 (512-dim)
 * @param processor BioHash 處理器
 * @param match_person 輸出：匹配的人員資訊（如果有）
 * @param error_count 輸出：BCH 糾正的錯誤數
 * @return 0 找到匹配，-1 沒有匹配
 */
int FaceDatabase_Verify(FaceDatabase_t* database, const std::vector<float>& feature,
                        BioHashProcessor& processor,
                        PersonInfo_t* match_person, int& error_count);

/**
 * @brief 取得資料庫中的所有人員
 * @param database 資料庫處理器
 * @return 人員列表
 */
const std::vector<PersonInfo_t>& FaceDatabase_GetAllPersons(FaceDatabase_t* database);

/**
 * @brief 清理資料庫
 * @param database 資料庫處理器
 */
void FaceDatabase_Cleanup(FaceDatabase_t* database);

#ifdef __cplusplus
}
#endif

#endif // FACE_DATABASE_H
