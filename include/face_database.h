#ifndef FACE_DATABASE_H
#define FACE_DATABASE_H

#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

// 人員資訊結構
typedef struct {
  int id;
  std::string name;
  std::vector<float> feature;  // 128維特徵向量
  float similarity;             // 比對時的相似度
} PersonInfo_t;

// 資料庫處理器
typedef struct {
  std::string database_path;
  std::vector<PersonInfo_t> persons;
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
 * @param feature 128維特徵向量
 * @param feature_size 特徵向量大小（應為 128）
 * @return 新增的人員 ID，失敗返回 -1
 */
int FaceDatabase_AddPerson(FaceDatabase_t* database, const char* name, 
                            const float* feature, int feature_size);

/**
 * @brief 比對人臉特徵，找出最相似的人員
 * @param database 資料庫處理器
 * @param feature 要比對的128維特徵向量
 * @param feature_size 特徵向量大小（應為 128）
 * @param match_person 輸出：匹配的人員資訊（如果有）
 * @return 0 找到匹配，-1 沒有匹配
 */
int FaceDatabase_Match(FaceDatabase_t* database, const float* feature, 
                       int feature_size, PersonInfo_t* match_person);

/**
 * @brief 計算兩個特徵向量的餘弦相似度
 * @param feature1 特徵向量1
 * @param feature2 特徵向量2
 * @param size 向量大小
 * @return 相似度 [0, 1]
 */
float FaceDatabase_CosineSimilarity(const float* feature1, const float* feature2, int size);

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
