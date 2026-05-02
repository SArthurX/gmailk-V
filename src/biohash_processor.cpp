#include "biohash_processor.h"
#include <iostream>
#include <random>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>

extern "C" {
#include "bch_codec.h"
}

// ==================== 輔助函數 ====================

static std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    for (uint8_t b : bytes) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    }
    return ss.str();
}

static std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        std::string byteStr = hex.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byteStr.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

// ==================== 核心 BioHash 函數 ====================

/**
 * @brief 生成偽隨機投影矩陣
 * @param seed 隨機種子
 * @return BIOHASH_DIM x BIOHASH_DIM 矩陣
 */
static std::vector<std::vector<float>> generate_random_matrix(uint64_t seed) {
    std::mt19937 rng((uint32_t)(seed & 0xFFFFFFFF) ^ (uint32_t)(seed >> 32));
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<std::vector<float>> matrix(BIOHASH_DIM, std::vector<float>(BIOHASH_DIM));
    for (int i = 0; i < BIOHASH_DIM; i++) {
        for (int j = 0; j < BIOHASH_DIM; j++) {
            matrix[i][j] = dist(rng);
        }
    }
    return matrix;
}

/**
 * @brief BioHash 投影: projected = matrix × feature
 */
static std::vector<float> biohash_projection(const std::vector<float>& feature,
                                              const std::vector<std::vector<float>>& matrix) {
    std::vector<float> projected(BIOHASH_DIM, 0.0f);
    for (int i = 0; i < BIOHASH_DIM; i++) {
        for (int j = 0; j < BIOHASH_DIM; j++) {
            projected[i] += matrix[i][j] * feature[j];
        }
    }
    return projected;
}

/**
 * @brief 二值化：使用中位數作為閾值
 */
static std::vector<uint8_t> binarize(const std::vector<float>& biohash) {
    std::vector<float> sorted_vals = biohash;
    std::sort(sorted_vals.begin(), sorted_vals.end());
    float median = sorted_vals[BIOHASH_DIM / 2];
    
    std::vector<uint8_t> bits(BIOHASH_DIM);
    for (int i = 0; i < BIOHASH_DIM; i++) {
        bits[i] = (biohash[i] > median) ? 1 : 0;
    }
    return bits;
}

/**
 * @brief 可靠位元選擇：選出投影值絕對值最大的 K 個位元
 */
static void select_reliable_bits(
    const std::vector<float>& biohash,
    const std::vector<uint8_t>& all_bits,
    std::vector<uint8_t>& selected_bits,
    std::vector<int>& selected_indices
) {
    // 計算每個投影值的絕對值並排序
    std::vector<std::pair<int, float>> ranked(BIOHASH_DIM);
    for (int i = 0; i < BIOHASH_DIM; i++) {
        ranked[i] = {i, std::fabs(biohash[i])};
    }
    
    std::sort(ranked.begin(), ranked.end(), 
         [](const std::pair<int, float>& a, const std::pair<int, float>& b) {
             return a.second > b.second;
         });
    
    // 取出前 K 個
    selected_bits.resize(BIOHASH_K);
    selected_indices.resize(BIOHASH_K);
    
    for (int i = 0; i < BIOHASH_K; i++) {
        int idx = ranked[i].first;
        selected_indices[i] = idx;
        selected_bits[i] = all_bits[idx];
    }
    
    // 按索引排序以保持一致的順序
    std::vector<std::pair<int, uint8_t>> index_bit_pairs(BIOHASH_K);
    for (int i = 0; i < BIOHASH_K; i++) {
        index_bit_pairs[i] = {selected_indices[i], selected_bits[i]};
    }
    std::sort(index_bit_pairs.begin(), index_bit_pairs.end());
    
    for (int i = 0; i < BIOHASH_K; i++) {
        selected_indices[i] = index_bit_pairs[i].first;
        selected_bits[i] = index_bit_pairs[i].second;
    }
}

/**
 * @brief BCH 編碼
 */
static std::vector<uint8_t> bch_encode_bits(const std::vector<uint8_t>& data_bits, int& ecc_bytes_len) {
    struct bch_control* bch = init_bch(BCH_M, BCH_T, 0);
    if (!bch) {
        std::cerr << "❌ BioHash: Failed to initialize BCH" << std::endl;
        ecc_bytes_len = 0;
        return {};
    }
    
    ecc_bytes_len = bch->ecc_bytes;
    
    // 將 bit 陣列轉為 byte 陣列
    uint8_t data_bytes[BIOHASH_K_BYTES];
    memset(data_bytes, 0, BIOHASH_K_BYTES);
    for (int i = 0; i < BIOHASH_K_BYTES; i++) {
        for (int j = 0; j < 8 && (i*8 + j) < BIOHASH_K; j++) {
            data_bytes[i] |= (data_bits[i*8 + j] << (7-j));
        }
    }
    
    // BCH 編碼
    std::vector<uint8_t> ecc_bytes(ecc_bytes_len);
    encode_bch(bch, data_bytes, BIOHASH_K_BYTES, ecc_bytes.data());
    
    // 組裝碼字 = data + ECC
    std::vector<uint8_t> codeword(BIOHASH_K_BYTES + ecc_bytes_len);
    memcpy(codeword.data(), data_bytes, BIOHASH_K_BYTES);
    memcpy(codeword.data() + BIOHASH_K_BYTES, ecc_bytes.data(), ecc_bytes_len);
    
    free_bch(bch);
    return codeword;
}

/**
 * @brief BCH 解碼驗證
 */
static bool bch_decode_and_verify(
    const std::vector<uint8_t>& received_bits,
    const std::vector<uint8_t>& stored_codeword,
    int ecc_bytes_len,
    int& num_errors
) {
    struct bch_control* bch = init_bch(BCH_M, BCH_T, 0);
    if (!bch) return false;
    
    // 將接收的 bit 轉為 byte
    uint8_t recv_bytes[BIOHASH_K_BYTES];
    memset(recv_bytes, 0, BIOHASH_K_BYTES);
    for (int i = 0; i < BIOHASH_K_BYTES; i++) {
        for (int j = 0; j < 8 && (i*8 + j) < BIOHASH_K; j++) {
            recv_bytes[i] |= (received_bits[i*8 + j] << (7-j));
        }
    }
    
    // 取出儲存的 ECC
    std::vector<uint8_t> stored_ecc(ecc_bytes_len);
    memcpy(stored_ecc.data(), stored_codeword.data() + BIOHASH_K_BYTES, ecc_bytes_len);
    
    // 計算接收數據的 ECC
    std::vector<uint8_t> calc_ecc(ecc_bytes_len);
    encode_bch(bch, recv_bytes, BIOHASH_K_BYTES, calc_ecc.data());
    
    // BCH 解碼
    std::vector<unsigned int> errloc(BCH_T);
    int nerr = decode_bch(bch, recv_bytes, BIOHASH_K_BYTES, stored_ecc.data(), calc_ecc.data(),
                          nullptr, errloc.data());
    
    num_errors = nerr;
    free_bch(bch);
    
    // nerr < 0 表示錯誤太多無法糾正
    return (nerr >= 0);
}

// ==================== BioHashTemplate 方法 ====================

std::string BioHashTemplate::to_hex() const {
    std::vector<uint8_t> data;
    // 1 byte: ecc_len
    data.push_back((uint8_t)ecc_bytes_len);
    // 索引 (每個 2 bytes, 因為 512 維需要 > 255)
    for (int idx : indices) {
        data.push_back((uint8_t)(idx >> 8));   // high byte
        data.push_back((uint8_t)(idx & 0xFF)); // low byte
    }
    // 碼字
    for (uint8_t b : codeword) {
        data.push_back(b);
    }
    return bytes_to_hex(data);
}

BioHashTemplate BioHashTemplate::from_hex(const std::string& hex) {
    BioHashTemplate t;
    std::vector<uint8_t> data = hex_to_bytes(hex);
    if (data.empty()) return t;
    
    size_t pos = 0;
    
    // ecc_len
    t.ecc_bytes_len = data[pos++];
    
    // indices (2 bytes each)
    t.indices.resize(BIOHASH_K);
    for (int i = 0; i < BIOHASH_K; i++) {
        if (pos + 1 >= data.size()) break;
        t.indices[i] = ((int)data[pos] << 8) | (int)data[pos+1];
        pos += 2;
    }
    
    // codeword
    int codeword_len = BIOHASH_K_BYTES + t.ecc_bytes_len;
    t.codeword.resize(codeword_len);
    for (int i = 0; i < codeword_len && pos < data.size(); i++) {
        t.codeword[i] = data[pos++];
    }
    
    return t;
}

// ==================== BioHashProcessor 方法 ====================

BioHashProcessor::BioHashProcessor() {
    std::cout << "✅ BioHashProcessor initialized" << std::endl;
    std::cout << "   - Feature dim: " << BIOHASH_DIM << std::endl;
    std::cout << "   - Reliable bits (K): " << BIOHASH_K << std::endl;
    std::cout << "   - BCH(" << ((1 << BCH_M) - 1) << ", " << BIOHASH_K << ", t=" << BCH_T << ")" << std::endl;
}

BioHashProcessor::~BioHashProcessor() {}

uint64_t BioHashProcessor::get_datetime_seed() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    // 日級精度 (YYYYMMDD0000)，萬用零代表整天有效
    uint64_t seed = (uint64_t)(t->tm_year + 1900) * 100000000ULL
                  + (uint64_t)(t->tm_mon + 1)     * 1000000ULL
                  + (uint64_t)(t->tm_mday)         * 10000ULL;
    // HH 和 mm 為 0，代表整天有效
    return seed;
}

uint64_t BioHashProcessor::parse_valid_date(const std::string& valid_date) {
    // 必須是 12 位數字 (YYYYMMDDHHmm)
    if (valid_date.size() != 12)
        return 0;
    
    for (char c : valid_date) {
        if (c < '0' || c > '9') return 0;
    }
    
    int year  = std::stoi(valid_date.substr(0, 4));
    int month = std::stoi(valid_date.substr(4, 2));
    int day   = std::stoi(valid_date.substr(6, 2));
    int hour  = std::stoi(valid_date.substr(8, 2));
    int min   = std::stoi(valid_date.substr(10, 2));
    
    // 基本驗證（允許 0 代表萬用）
    if (year < 2020) return 0;
    if (month > 12) return 0;
    if (day > 31) return 0;
    if (hour > 23) return 0;
    if (min > 59) return 0;
    
    // 層級一致性：如果月=0，則日/時/分也必須為 0
    if (month == 0 && (day != 0 || hour != 0 || min != 0)) return 0;
    if (day == 0 && (hour != 0 || min != 0)) return 0;
    if (hour == 0 && min != 0) return 0;
    
    return (uint64_t)year  * 100000000ULL
         + (uint64_t)month * 1000000ULL
         + (uint64_t)day   * 10000ULL
         + (uint64_t)hour  * 100ULL
         + (uint64_t)min;
}

std::vector<uint64_t> BioHashProcessor::generate_candidate_seeds() {
    std::vector<uint64_t> seeds;
    
    time_t now = time(nullptr);
    struct tm t_now;
    localtime_r(&now, &t_now);
    
    uint64_t year  = t_now.tm_year + 1900;
    uint64_t month = t_now.tm_mon + 1;
    uint64_t day   = t_now.tm_mday;
    uint64_t hour  = t_now.tm_hour;
    uint64_t min   = t_now.tm_min;
    
    // 恆定 5 個候選種子（從粗到細）
    seeds.push_back(year * 100000000ULL);                                                     // YYYY00000000 (整年)
    seeds.push_back(year * 100000000ULL + month * 1000000ULL);                                // YYYYMM000000 (整月)
    seeds.push_back(year * 100000000ULL + month * 1000000ULL + day * 10000ULL);                // YYYYMMDD0000 (整天)
    seeds.push_back(year * 100000000ULL + month * 1000000ULL + day * 10000ULL + hour * 100ULL); // YYYYMMDDHHmm mm=00 (整時)
    seeds.push_back(year * 100000000ULL + month * 1000000ULL + day * 10000ULL + hour * 100ULL + min); // YYYYMMDDHHmm (精確到分)
    
    return seeds;
}

BioHashTemplate BioHashProcessor::enroll(const std::vector<float>& feature, uint64_t seed) {
    BioHashTemplate tmpl;
    
    if ((int)feature.size() != BIOHASH_DIM) {
        std::cerr << "❌ BioHash enroll: feature dim mismatch (expected " 
                  << BIOHASH_DIM << ", got " << feature.size() << ")" << std::endl;
        return tmpl;
    }
    
    std::cout << "🔐 BioHash Enrollment (seed=" << seed << ")" << std::endl;
    
    // 1. 生成隨機投影矩陣
    auto matrix = generate_random_matrix(seed);
    
    // 2. BioHash 投影
    std::vector<float> biohash = biohash_projection(feature, matrix);
    
    // 3. 二值化
    std::vector<uint8_t> all_bits = binarize(biohash);
    
    // 4. 可靠位元選擇
    std::vector<uint8_t> selected_bits;
    select_reliable_bits(biohash, all_bits, selected_bits, tmpl.indices);
    
    // 5. BCH 編碼
    tmpl.codeword = bch_encode_bits(selected_bits, tmpl.ecc_bytes_len);
    
    if (tmpl.is_valid()) {
        std::cout << "✅ BioHash template created" << std::endl;
        std::cout << "   - Template size: " << (1 + BIOHASH_K * 2 + tmpl.codeword.size()) << " bytes" << std::endl;
        std::cout << "   - ECC bytes: " << tmpl.ecc_bytes_len << std::endl;
    } else {
        std::cerr << "❌ BioHash enrollment failed" << std::endl;
    }
    
    return tmpl;
}

bool BioHashProcessor::verify(const std::vector<float>& feature,
                               const BioHashTemplate& tmpl,
                               int& num_errors) {
    if ((int)feature.size() != BIOHASH_DIM) {
        std::cerr << "❌ BioHash verify: feature dim mismatch" << std::endl;
        return false;
    }
    
    if (!tmpl.is_valid()) {
        std::cerr << "❌ BioHash verify: invalid template" << std::endl;
        return false;
    }
    
    // 生成候選種子列表
    auto candidate_seeds = generate_candidate_seeds();
    
    std::cout << "🔍 BioHash verify: trying " << candidate_seeds.size() << " candidate seeds..." << std::endl;
    
    for (uint64_t seed : candidate_seeds) {
        if (verify_with_seed(feature, tmpl, seed, num_errors)) {
            std::cout << "✅ BioHash verify success (seed=" << seed 
                      << ", errors=" << num_errors << ")" << std::endl;
            return true;
        }
    }
    
    std::cout << "❌ BioHash verify failed: no matching seed found" << std::endl;
    return false;
}

int BioHashProcessor::verify_multiple(const std::vector<float>& feature, 
                                      const std::vector<BioHashTemplate>& templates,
                                      int& best_errors, uint64_t& matching_seed) {
    if ((int)feature.size() != BIOHASH_DIM || templates.empty()) {
        return -1;
    }
    
    auto candidate_seeds = generate_candidate_seeds();
    std::cout << "🔍 BioHash batch verify: " << templates.size() << " templates × " 
              << candidate_seeds.size() << " seeds..." << std::endl;
    
    int best_match_idx = -1;
    best_errors = BCH_T + 1; // 預設大於可糾正數量
    
    for (uint64_t seed : candidate_seeds) {
        // 核心優化：對每個 seed，只做一次投影和二值化（計算量最大的部分）
        auto matrix = generate_random_matrix(seed);
        std::vector<float> biohash = biohash_projection(feature, matrix);
        std::vector<uint8_t> all_bits = binarize(biohash);
        
        // 遍歷所有模板，只做提取和解碼（計算量極小）
        for (size_t i = 0; i < templates.size(); i++) {
            const auto& tmpl = templates[i];
            if (!tmpl.is_valid()) continue;
            
            std::vector<uint8_t> selected_bits(BIOHASH_K);
            for (int k = 0; k < BIOHASH_K; k++) {
                if (tmpl.indices[k] >= 0 && tmpl.indices[k] < BIOHASH_DIM) {
                    selected_bits[k] = all_bits[tmpl.indices[k]];
                }
            }
            
            int num_errors = 0;
            if (bch_decode_and_verify(selected_bits, tmpl.codeword, tmpl.ecc_bytes_len, num_errors)) {
                if (num_errors < best_errors) {
                    best_errors = num_errors;
                    best_match_idx = i;
                    matching_seed = seed;
                }
            }
        }
        
        // 可選優化：如果有完全無錯誤的匹配，提早結束
        if (best_match_idx >= 0 && best_errors == 0) {
            break;
        }
    }
    
    if (best_match_idx >= 0) {
        std::cout << "✅ Batch verify success: match index " << best_match_idx 
                  << " (seed=" << matching_seed << ", errors=" << best_errors << ")" << std::endl;
    } else {
        std::cout << "❌ Batch verify failed: no match found" << std::endl;
    }
    
    return best_match_idx;
}

bool BioHashProcessor::verify_with_seed(const std::vector<float>& feature,
                                         const BioHashTemplate& tmpl,
                                         uint64_t seed, int& num_errors) {
    // 1. 重建隨機矩陣
    auto matrix = generate_random_matrix(seed);
    
    // 2. 投影
    std::vector<float> biohash = biohash_projection(feature, matrix);
    
    // 3. 二值化
    std::vector<uint8_t> all_bits = binarize(biohash);
    
    // 4. 使用模板中的索引提取位元
    std::vector<uint8_t> selected_bits(BIOHASH_K);
    for (int i = 0; i < BIOHASH_K; i++) {
        if (tmpl.indices[i] >= 0 && tmpl.indices[i] < BIOHASH_DIM) {
            selected_bits[i] = all_bits[tmpl.indices[i]];
        }
    }
    
    // 5. BCH 解碼驗證
    return bch_decode_and_verify(selected_bits, tmpl.codeword, tmpl.ecc_bytes_len, num_errors);
}
