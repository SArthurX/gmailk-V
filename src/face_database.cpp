#include "face_database.h"
#include "3rdparty/json/json.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

// 初始化資料庫
int FaceDatabase_Init(FaceDatabase_t* database, const char* db_path, float threshold) {
  if (!database || !db_path) {
    std::cerr << "FaceDatabase_Init: Invalid parameters" << std::endl;
    return -1;
  }

  database->database_path = db_path;
  database->similarity_threshold = threshold;
  database->persons.clear();
  database->initialized = false;

  // 嘗試載入現有資料庫
  if (FaceDatabase_Load(database) == 0) {
    std::cout << "FaceDatabase: Loaded " << database->persons.size() 
              << " persons from " << db_path << std::endl;
  } else {
    std::cout << "FaceDatabase: Starting with empty database at " << db_path << std::endl;
  }

  database->initialized = true;
  return 0;
}

// 載入資料庫
int FaceDatabase_Load(FaceDatabase_t* database) {
  if (!database) {
    return -1;
  }

  std::ifstream file(database->database_path);
  if (!file.is_open()) {
    // 檔案不存在，返回失敗（會創建新資料庫）
    return -1;
  }

  try {
    json j;
    file >> j;
    file.close();

    database->persons.clear();

    // 解析 JSON
    if (j.contains("persons") && j["persons"].is_array()) {
      for (const auto& person_json : j["persons"]) {
        PersonInfo_t person;
        person.id = person_json["id"];
        person.name = person_json["name"];
        
        if (person_json.contains("feature") && person_json["feature"].is_array()) {
          person.feature = person_json["feature"].get<std::vector<float>>();
        }
        
        person.similarity = 0.0f;
        database->persons.push_back(person);
      }
    }

    std::cout << "FaceDatabase: Successfully loaded " << database->persons.size() 
              << " persons" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "FaceDatabase_Load: Error parsing JSON: " << e.what() << std::endl;
    file.close();
    return -1;
  }
}

// 儲存資料庫
int FaceDatabase_Save(FaceDatabase_t* database) {
  if (!database || !database->initialized) {
    return -1;
  }

  try {
    json j;
    j["version"] = "1.0";
    j["threshold"] = database->similarity_threshold;
    j["persons"] = json::array();

    for (const auto& person : database->persons) {
      json person_json;
      person_json["id"] = person.id;
      person_json["name"] = person.name;
      person_json["feature"] = person.feature;
      j["persons"].push_back(person_json);
    }

    std::ofstream file(database->database_path);
    if (!file.is_open()) {
      std::cerr << "FaceDatabase_Save: Cannot open file for writing: " 
                << database->database_path << std::endl;
      return -1;
    }

    file << j.dump(2);  // 格式化輸出，縮排2格
    file.close();

    std::cout << "FaceDatabase: Saved " << database->persons.size() 
              << " persons to " << database->database_path << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "FaceDatabase_Save: Error: " << e.what() << std::endl;
    return -1;
  }
}

// 新增人員
int FaceDatabase_AddPerson(FaceDatabase_t* database, const char* name, 
                            const float* feature, int feature_size) {
  if (!database || !database->initialized || !name || !feature) {
    std::cerr << "FaceDatabase_AddPerson: Invalid parameters" << std::endl;
    return -1;
  }

  if (feature_size != 128) {
    std::cerr << "FaceDatabase_AddPerson: Invalid feature size: " << feature_size 
              << " (expected 128)" << std::endl;
    return -1;
  }

  // 生成新的 ID
  int new_id = 1;
  if (!database->persons.empty()) {
    new_id = database->persons.back().id + 1;
  }

  // 創建新人員
  PersonInfo_t person;
  person.id = new_id;
  person.name = name;
  person.feature.assign(feature, feature + feature_size);
  person.similarity = 0.0f;

  database->persons.push_back(person);

  std::cout << "FaceDatabase: Added person [" << new_id << "] " << name << std::endl;

  // 立即儲存
  FaceDatabase_Save(database);

  return new_id;
}

// 計算餘弦相似度
float FaceDatabase_CosineSimilarity(const float* feature1, const float* feature2, int size) {
  if (!feature1 || !feature2 || size <= 0) {
    return 0.0f;
  }

  float dot_product = 0.0f;
  float norm1 = 0.0f;
  float norm2 = 0.0f;

  for (int i = 0; i < size; i++) {
    dot_product += feature1[i] * feature2[i];
    norm1 += feature1[i] * feature1[i];
    norm2 += feature2[i] * feature2[i];
  }

  norm1 = std::sqrt(norm1);
  norm2 = std::sqrt(norm2);

  if (norm1 < 1e-6 || norm2 < 1e-6) {
    return 0.0f;
  }

  return dot_product / (norm1 * norm2);
}

// 比對人臉
int FaceDatabase_Match(FaceDatabase_t* database, const float* feature, 
                       int feature_size, PersonInfo_t* match_person) {
  if (!database || !database->initialized || !feature || !match_person) {
    return -1;
  }

  if (feature_size != 128) {
    std::cerr << "FaceDatabase_Match: Invalid feature size: " << feature_size << std::endl;
    return -1;
  }

  if (database->persons.empty()) {
    return -1;  // 資料庫為空
  }

  float max_similarity = -1.0f;
  int best_match_idx = -1;

  // 找出最相似的人員
  for (size_t i = 0; i < database->persons.size(); i++) {
    float similarity = FaceDatabase_CosineSimilarity(
      feature, 
      database->persons[i].feature.data(), 
      feature_size
    );

    if (similarity > max_similarity) {
      max_similarity = similarity;
      best_match_idx = i;
    }
  }

  // 檢查是否超過閾值
  if (max_similarity >= database->similarity_threshold && best_match_idx >= 0) {
    *match_person = database->persons[best_match_idx];
    match_person->similarity = max_similarity;
    return 0;  // 找到匹配
  }

  return -1;  // 沒有找到匹配
}

// 取得所有人員
const std::vector<PersonInfo_t>& FaceDatabase_GetAllPersons(FaceDatabase_t* database) {
  static std::vector<PersonInfo_t> empty_list;
  if (!database) {
    return empty_list;
  }
  return database->persons;
}

// 清理資料庫
void FaceDatabase_Cleanup(FaceDatabase_t* database) {
  if (!database) {
    return;
  }

  database->persons.clear();
  database->initialized = false;
  
  std::cout << "FaceDatabase: Cleaned up" << std::endl;
}
