#include "remote_database.h"
#include "3rdparty/json/json.hpp"

// cpp-httplib (header-only HTTP client)
#include "3rdparty/httplib/httplib.h"

#include <iostream>
#include <fstream>

using json = nlohmann::json;

// ─── 初始化 ───

int RemoteDatabase_Init(RemoteDatabase_t* db, const char* base_url) {
    if (!db || !base_url) {
        std::cerr << "RemoteDatabase_Init: Invalid parameters" << std::endl;
        return -1;
    }

    db->base_url = base_url;
    db->initialized = true;
    db->connected = false;
    db->cached_templates.clear();
    db->cache_valid = false;
    db->cache_time = 0;
    db->cache_ttl_seconds = 10;  // 10 秒快取過期

    std::cout << "RemoteDatabase: Initialized with server " << base_url << std::endl;

    // 嘗試初始連線
    if (RemoteDatabase_CheckConnection(db)) {
        std::cout << "RemoteDatabase: RPi server reachable" << std::endl;
    } else {
        std::cerr << "RemoteDatabase: ⚠️  RPi server not reachable (will retry later)" << std::endl;
    }

    return 0;
}

// ─── 連線檢查 ───

bool RemoteDatabase_CheckConnection(RemoteDatabase_t* db) {
    if (!db || !db->initialized)
        return false;

    try {
        httplib::Client cli(db->base_url);
        cli.set_connection_timeout(3, 0);   // 3 秒連線 timeout
        cli.set_read_timeout(5, 0);         // 5 秒讀取 timeout

        auto res = cli.Get("/api/status");
        if (res && res->status == 200) {
            db->connected = true;
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "RemoteDatabase: Connection check failed: " << e.what() << std::endl;
    }

    db->connected = false;
    return false;
}

// ─── 取得模板 ───

int RemoteDatabase_FetchTemplates(RemoteDatabase_t* db, std::vector<PersonInfo_t>& persons) {
    if (!db || !db->initialized) {
        std::cerr << "RemoteDatabase_FetchTemplates: Not initialized" << std::endl;
        return -1;
    }

    // 如果快取有效且未過期，直接使用快取
    if (db->cache_valid && !db->cached_templates.empty()) {
        time_t now = time(nullptr);
        if (now - db->cache_time < db->cache_ttl_seconds) {
            persons = db->cached_templates;
            return 0;
        }
        // 快取過期，重新抓取
        db->cache_valid = false;
    }

    try {
        httplib::Client cli(db->base_url);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(10, 0);        // 模板可能較大，給更多時間

        auto res = cli.Get("/api/templates");
        if (!res) {
            std::cerr << "RemoteDatabase: GET /api/templates failed (no response)" << std::endl;
            db->connected = false;
            return -1;
        }

        if (res->status != 200) {
            std::cerr << "RemoteDatabase: GET /api/templates returned HTTP " 
                      << res->status << std::endl;
            return -1;
        }

        db->connected = true;

        // 解析 JSON 回應
        json j = json::parse(res->body);
        persons.clear();

        if (!j.is_array()) {
            std::cerr << "RemoteDatabase: Expected JSON array" << std::endl;
            return -1;
        }

        for (const auto& item : j) {
            PersonInfo_t person;
            person.id = item.value("id", 0);
            person.name = item.value("name", "");
            person.biohash_template_hex = item.value("biohash_template", "");
            person.encrypted_payload_hex = item.value("encrypted_payload", "");
            person.bch_errors = 0;

            if (!person.biohash_template_hex.empty()) {
                persons.push_back(person);
            }
        }

        // 更新快取
        db->cached_templates = persons;
        db->cache_valid = true;
        db->cache_time = time(nullptr);

        std::cout << "RemoteDatabase: Fetched " << persons.size() 
                  << " templates from RPi" << std::endl;
        return 0;

    } catch (const json::parse_error& e) {
        std::cerr << "RemoteDatabase: JSON parse error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "RemoteDatabase: FetchTemplates error: " << e.what() << std::endl;
        db->connected = false;
    }

    return -1;
}

// ─── 裝置端直接註冊 ───

int RemoteDatabase_CreatePerson(RemoteDatabase_t* db, const char* name,
                                 const std::string& template_hex) {
    if (!db || !db->initialized || !name) {
        std::cerr << "RemoteDatabase_CreatePerson: Invalid parameters" << std::endl;
        return -1;
    }

    if (template_hex.empty()) {
        std::cerr << "RemoteDatabase_CreatePerson: Empty template" << std::endl;
        return -1;
    }

    try {
        httplib::Client cli(db->base_url);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(10, 0);

        // 構建 JSON body
        json body;
        body["name"] = name;
        body["biohash_template"] = template_hex;
        body["description"] = "";
        body["encrypted_payload"] = "";

        auto res = cli.Post("/api/persons", body.dump(), "application/json");
        if (!res) {
            std::cerr << "RemoteDatabase: POST /api/persons failed (no response)" << std::endl;
            db->connected = false;
            return -1;
        }

        if (res->status != 201) {
            std::cerr << "RemoteDatabase: POST /api/persons returned HTTP " 
                      << res->status << std::endl;
            if (!res->body.empty()) {
                std::cerr << "  Response: " << res->body << std::endl;
            }
            return -1;
        }

        db->connected = true;

        // 解析回應取得新 ID
        json resp = json::parse(res->body);
        int new_id = resp.value("id", -1);

        // 使快取失效（新增了人員）
        db->cache_valid = false;

        std::cout << "RemoteDatabase: Person registered on RPi [" << new_id 
                  << "] " << name << std::endl;
        return new_id;

    } catch (const json::parse_error& e) {
        std::cerr << "RemoteDatabase: JSON parse error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "RemoteDatabase: CreatePerson error: " << e.what() << std::endl;
        db->connected = false;
    }

    return -1;
}

// ─── 快取管理 ───

void RemoteDatabase_InvalidateCache(RemoteDatabase_t* db) {
    if (db) {
        db->cache_valid = false;
    }
}

// ─── 裝置端處理 Pending 註冊 ───

int RemoteDatabase_FetchPendingPersons(RemoteDatabase_t* db, std::vector<PendingPerson_t>& pending_list) {
    if (!db || !db->initialized) {
        std::cerr << "RemoteDatabase_FetchPendingPersons: Not initialized" << std::endl;
        return -1;
    }

    try {
        httplib::Client cli(db->base_url);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(5, 0);

        auto res = cli.Get("/api/pending");
        if (!res) {
            std::cerr << "RemoteDatabase: GET /api/pending failed (no response)" << std::endl;
            db->connected = false;
            return -1;
        }

        if (res->status != 200) {
            std::cerr << "RemoteDatabase: GET /api/pending returned HTTP " << res->status << std::endl;
            return -1;
        }

        db->connected = true;
        json j = json::parse(res->body);
        pending_list.clear();

        if (!j.is_array()) {
            std::cerr << "RemoteDatabase: Expected JSON array for /api/pending" << std::endl;
            return -1;
        }

        for (const auto& item : j) {
            PendingPerson_t pending;
            pending.id = item.value("id", 0);
            pending.name = item.value("name", "");
            pending.photo_path = item.value("photo_path", "");
            pending.valid_date = item.value("valid_date", "");
            pending.description = item.value("description", "");
            
            if (!pending.photo_path.empty()) {
                pending_list.push_back(pending);
            }
        }
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "RemoteDatabase: FetchPendingPersons error: " << e.what() << std::endl;
        db->connected = false;
    }
    return -1;
}

int RemoteDatabase_DownloadPhoto(RemoteDatabase_t* db, const std::string& filename, const std::string& save_path) {
    if (!db || !db->initialized) return -1;

    // 防呆：strip "uploads/" 前綴，避免 /uploads/uploads/xxx
    std::string clean_filename = filename;
    if (clean_filename.substr(0, 8) == "uploads/") {
        clean_filename = clean_filename.substr(8);
    }

    try {
        httplib::Client cli(db->base_url);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(15, 0); // 圖片下載可能需要較長時間

        std::string path = "/uploads/" + clean_filename;
        auto res = cli.Get(path.c_str());

        if (!res) {
            std::cerr << "RemoteDatabase: GET " << path << " failed (no response)" << std::endl;
            return -1;
        }

        if (res->status != 200) {
            std::cerr << "RemoteDatabase: Failed to download photo, HTTP " << res->status << std::endl;
            return -1;
        }

        std::ofstream out(save_path, std::ios::binary);
        if (!out) {
            std::cerr << "RemoteDatabase: Failed to open local file for writing: " << save_path << std::endl;
            return -1;
        }

        out.write(res->body.data(), res->body.size());
        out.close();

        std::cout << "RemoteDatabase: Downloaded photo to " << save_path 
                  << " (Size: " << res->body.size() << " bytes)" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "RemoteDatabase: DownloadPhoto error: " << e.what() << std::endl;
    }
    return -1;
}

int RemoteDatabase_CompletePerson(RemoteDatabase_t* db, int id, const std::string& template_hex,
                                  const std::string& encrypted_payload_hex) {
    if (!db || !db->initialized || template_hex.empty()) return -1;

    try {
        httplib::Client cli(db->base_url);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(5, 0);

        json body;
        body["biohash_template"] = template_hex;
        body["encrypted_payload"] = encrypted_payload_hex;

        std::string path = "/api/persons/" + std::to_string(id) + "/complete";
        auto res = cli.Post(path.c_str(), body.dump(), "application/json");

        if (!res) {
            std::cerr << "RemoteDatabase: POST " << path << " failed (no response)" << std::endl;
            return -1;
        }

        if (res->status != 200 && res->status != 201) {
            std::cerr << "RemoteDatabase: POST " << path << " returned HTTP " << res->status << std::endl;
            if (!res->body.empty()) {
                std::cerr << "  Response: " << res->body << std::endl;
            }
            return -1;
        }

        db->cache_valid = false; // 使快取失效，以便能夠拉取新完成的模板

        std::cout << "RemoteDatabase: Completed remote registration for ID: " << id << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "RemoteDatabase: CompletePerson error: " << e.what() << std::endl;
    }
    return -1;
}

// ─── 清理 ───

void RemoteDatabase_Cleanup(RemoteDatabase_t* db) {
    if (!db)
        return;

    db->cached_templates.clear();
    db->cache_valid = false;
    db->initialized = false;
    db->connected = false;

    std::cout << "RemoteDatabase: Cleaned up" << std::endl;
}
