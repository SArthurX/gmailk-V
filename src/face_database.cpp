#include "face_database.h"
#include "biohash_processor.h"
#include "3rdparty/json/json.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <filesystem>

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
  if (FaceDatabase_Load(database) == 0)
    std::cout << "FaceDatabase: Loaded " << database->persons.size() 
              << " persons from " << db_path << std::endl;
  else
    std::cout << "FaceDatabase: Starting with empty database at " << db_path << std::endl;

  database->initialized = true;
  return 0;
}

// 載入資料庫
int FaceDatabase_Load(FaceDatabase_t* database) {
  if (!database) 
    return -1;

  std::ifstream file(database->database_path);
  if (!file.is_open()) 
    return -1;

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
        
        if (person_json.contains("biohash_template") && person_json["biohash_template"].is_string()) {
          person.biohash_template_hex = person_json["biohash_template"].get<std::string>();
        }
        
        person.bch_errors = 0;
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
  if (!database || !database->initialized)
    return -1;

  try {
    json j;
    j["version"] = "2.0";
    j["type"] = "biohash";
    j["persons"] = json::array();

    for (const auto& person : database->persons) {
      json person_json;
      person_json["id"] = person.id;
      person_json["name"] = person.name;
      person_json["biohash_template"] = person.biohash_template_hex;
      j["persons"].push_back(person_json);
    }

    try {
        std::filesystem::path p(database->database_path);
        if (p.has_parent_path()) 
            std::filesystem::create_directories(p.parent_path());
    } catch (const std::exception& e) {
        std::cerr << "Directory error: " << e.what() << std::endl;
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
                            const std::string& template_hex) {
  if (!database || !database->initialized || !name) {
    std::cerr << "FaceDatabase_AddPerson: Invalid parameters" << std::endl;
    return -1;
  }

  if (template_hex.empty()) {
    std::cerr << "FaceDatabase_AddPerson: Empty template" << std::endl;
    return -1;
  }

  // 生成新的 ID
  int new_id = 1;
  if (!database->persons.empty()) 
    new_id = database->persons.back().id + 1;

  // 創建新人員
  PersonInfo_t person;
  person.id = new_id;
  person.name = name;
  person.biohash_template_hex = template_hex;
  person.bch_errors = 0;

  std::cout << "FaceDatabase: Adding person [" << new_id << "] " << name << std::endl;
  std::cout << "  Template size: " << template_hex.length() / 2 << " bytes" << std::endl;

  database->persons.push_back(person);

  // 立即儲存
  FaceDatabase_Save(database);

  return new_id;
}

// 驗證人臉 (BioHash + BCH)
int FaceDatabase_Verify(FaceDatabase_t* database, const std::vector<float>& feature,
                        BioHashProcessor& processor,
                        PersonInfo_t* match_person, int& error_count) {
  if (!database || !database->initialized || !match_person)
    return -1;

  if (feature.empty()) {
    std::cerr << "FaceDatabase_Verify: Empty feature vector" << std::endl;
    return -1;
  }

  if (database->persons.empty())
    return -1;  // 資料庫為空

  std::cout << "[BioHash] Verifying against " << database->persons.size() << " persons..." << std::endl;

  int best_match_idx = -1;
  int best_errors = BCH_T + 1;  // 初始化為超出糾錯能力

  // 遍歷所有人員的模板
  for (size_t i = 0; i < database->persons.size(); i++) {
    if (database->persons[i].biohash_template_hex.empty()) continue;
    
    BioHashTemplate tmpl = BioHashTemplate::from_hex(database->persons[i].biohash_template_hex);
    if (!tmpl.is_valid()) {
      std::cerr << "  Person [" << database->persons[i].id << "] " 
                << database->persons[i].name << ": invalid template, skipping" << std::endl;
      continue;
    }
    
    int num_errors = 0;
    bool success = processor.verify(feature, tmpl, num_errors);
    
    if (success) {
      std::cout << "  Person [" << database->persons[i].id << "] " 
                << database->persons[i].name << ": ✅ MATCH (errors=" << num_errors << ")" << std::endl;
      
      // 選擇錯誤數最少的匹配
      if (num_errors < best_errors) {
        best_errors = num_errors;
        best_match_idx = i;
      }
    } else {
      std::cout << "  Person [" << database->persons[i].id << "] " 
                << database->persons[i].name << ": ❌ no match" << std::endl;
    }
  }

  // 返回最佳匹配
  if (best_match_idx >= 0) {
    *match_person = database->persons[best_match_idx];
    match_person->bch_errors = best_errors;
    error_count = best_errors;
    return 0;  // 找到匹配
  }

  return -1;  // 沒有找到匹配
}

// 取得所有人員
const std::vector<PersonInfo_t>& FaceDatabase_GetAllPersons(FaceDatabase_t* database) {
  static std::vector<PersonInfo_t> empty_list;
  if (!database)
    return empty_list;
  return database->persons;
}

// 清理資料庫
void FaceDatabase_Cleanup(FaceDatabase_t* database) {
  if (!database)
    return;

  database->persons.clear();
  database->initialized = false;
  
  std::cout << "FaceDatabase: Cleaned up" << std::endl;
}
