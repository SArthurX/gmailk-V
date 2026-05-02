#include "biohash_processor.h"
#include "crypto_utils.hpp"
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

static std::string bytes_to_hex_local(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    for (uint8_t b : bytes) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
    }
    return ss.str();
}

static std::vector<uint8_t> hex_to_bytes_local(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        std::string byteStr = hex.substr(i, 2);
        uint8_t byte = (uint8_t)strtol(byteStr.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    return bytes;
}

// ==================== 核心 BioHash 函數 ====================

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

static void select_reliable_bits(
    const std::vector<float>& biohash,
    const std::vector<uint8_t>& all_bits,
    std::vector<uint8_t>& selected_bits,
    std::vector<int>& selected_indices
) {
    std::vector<std::pair<int, float>> ranked(BIOHASH_DIM);
    for (int i = 0; i < BIOHASH_DIM; i++) {
        ranked[i] = {i, std::fabs(biohash[i])};
    }
    
    std::sort(ranked.begin(), ranked.end(), 
         [](const std::pair<int, float>& a, const std::pair<int, float>& b) {
             return a.second > b.second;
         });
    
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

// 將 bit 陣列打包為 byte 陣列
static void bits_to_bytes(const uint8_t* bits, int num_bits, uint8_t* bytes) {
    int num_bytes = (num_bits + 7) / 8;
    memset(bytes, 0, num_bytes);
    for (int i = 0; i < num_bytes; i++) {
        for (int j = 0; j < 8 && (i*8 + j) < num_bits; j++) {
            bytes[i] |= (bits[i*8 + j] << (7-j));
        }
    }
}

// ==================== BioHashTemplate 方法 ====================

std::string BioHashTemplate::to_hex() const {
    std::vector<uint8_t> data;
    
    // [version: 1B = 0x02]
    data.push_back(version);
    // [ecc_len: 1B]
    data.push_back((uint8_t)ecc_bytes_len);
    // [indices: 256B] (每個 2 bytes)
    for (int idx : indices) {
        data.push_back((uint8_t)(idx >> 8));
        data.push_back((uint8_t)(idx & 0xFF));
    }
    // [sketch: data+ecc bytes]
    for (uint8_t b : sketch) {
        data.push_back(b);
    }
    // [commitment: 32B]
    for (uint8_t b : commitment) {
        data.push_back(b);
    }
    // [encrypted_payload_len: 2B big-endian]
    uint16_t ep_len = (uint16_t)encrypted_payload.size();
    data.push_back((uint8_t)(ep_len >> 8));
    data.push_back((uint8_t)(ep_len & 0xFF));
    // [encrypted_payload: 變長]
    for (uint8_t b : encrypted_payload) {
        data.push_back(b);
    }
    
    return bytes_to_hex_local(data);
}

BioHashTemplate BioHashTemplate::from_hex(const std::string& hex) {
    BioHashTemplate t;
    std::vector<uint8_t> data = hex_to_bytes_local(hex);
    if (data.empty()) return t;
    
    size_t pos = 0;
    
    // [version: 1B]
    t.version = data[pos++];
    if (t.version != 2) {
        std::cerr << "⚠️  BioHashTemplate::from_hex: unsupported version " 
                  << (int)t.version << std::endl;
        return t;
    }
    
    // [ecc_len: 1B]
    if (pos >= data.size()) return t;
    t.ecc_bytes_len = data[pos++];
    
    // [indices: 256B] (128 × 2 bytes)
    t.indices.resize(BIOHASH_K);
    for (int i = 0; i < BIOHASH_K; i++) {
        if (pos + 1 >= data.size()) break;
        t.indices[i] = ((int)data[pos] << 8) | (int)data[pos+1];
        pos += 2;
    }
    
    // [sketch: BIOHASH_K_BYTES + ecc_bytes_len]
    int sketch_len = BIOHASH_K_BYTES + t.ecc_bytes_len;
    t.sketch.resize(sketch_len);
    for (int i = 0; i < sketch_len && pos < data.size(); i++) {
        t.sketch[i] = data[pos++];
    }
    
    // [commitment: 32B]
    t.commitment = {};
    for (int i = 0; i < COMMITMENT_SIZE && pos < data.size(); i++) {
        t.commitment[i] = data[pos++];
    }
    
    // [encrypted_payload_len: 2B]
    if (pos + 1 < data.size()) {
        uint16_t ep_len = ((uint16_t)data[pos] << 8) | (uint16_t)data[pos+1];
        pos += 2;
        
        // [encrypted_payload: ep_len bytes]
        if (ep_len > 0 && pos + ep_len <= data.size()) {
            t.encrypted_payload.resize(ep_len);
            for (uint16_t i = 0; i < ep_len; i++) {
                t.encrypted_payload[i] = data[pos++];
            }
        }
    }
    
    return t;
}

// ==================== BioHashProcessor 方法 ====================

BioHashProcessor::BioHashProcessor() {
    std::cout << "✅ BioHashProcessor initialized (Fuzzy Commitment v2)" << std::endl;
    std::cout << "   - Feature dim: " << BIOHASH_DIM << std::endl;
    std::cout << "   - Reliable bits (K): " << BIOHASH_K << std::endl;
    std::cout << "   - BCH(" << ((1 << BCH_M) - 1) << ", " << BIOHASH_K << ", t=" << BCH_T << ")" << std::endl;
}

BioHashProcessor::~BioHashProcessor() {}

uint64_t BioHashProcessor::get_datetime_seed() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    uint64_t seed = (uint64_t)(t->tm_year + 1900) * 100000000ULL
                  + (uint64_t)(t->tm_mon + 1)     * 1000000ULL
                  + (uint64_t)(t->tm_mday)         * 10000ULL;
    return seed;
}

uint64_t BioHashProcessor::parse_valid_date(const std::string& valid_date) {
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
    
    if (year < 2020) return 0;
    if (month > 12) return 0;
    if (day > 31) return 0;
    if (hour > 23) return 0;
    if (min > 59) return 0;
    
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
    
    seeds.push_back(year * 100000000ULL);
    seeds.push_back(year * 100000000ULL + month * 1000000ULL);
    seeds.push_back(year * 100000000ULL + month * 1000000ULL + day * 10000ULL);
    seeds.push_back(year * 100000000ULL + month * 1000000ULL + day * 10000ULL + hour * 100ULL);
    seeds.push_back(year * 100000000ULL + month * 1000000ULL + day * 10000ULL + hour * 100ULL + min);
    
    return seeds;
}

// ==================== Fuzzy Commitment Enroll ====================

EnrollResult BioHashProcessor::enroll(const std::vector<float>& feature, uint64_t seed) {
    EnrollResult result;
    
    if ((int)feature.size() != BIOHASH_DIM) {
        std::cerr << "❌ BioHash enroll: feature dim mismatch (expected " 
                  << BIOHASH_DIM << ", got " << feature.size() << ")" << std::endl;
        return result;
    }
    
    std::cout << "🔐 BioHash Enrollment — Fuzzy Commitment (seed=" << seed << ")" << std::endl;
    
    // 步驟 1-4: 投影 → 二值化 → 可靠位元 (與 v1 完全一致)
    auto matrix = generate_random_matrix(seed);
    std::vector<float> biohash = biohash_projection(feature, matrix);
    std::vector<uint8_t> all_bits = binarize(biohash);
    
    std::vector<uint8_t> selected_bits;  // B (128 bits)
    select_reliable_bits(biohash, all_bits, selected_bits, result.tmpl.indices);
    
    // 步驟 5: 生成 128-bit 隨機金鑰 K
    result.key.resize(BIOHASH_K_BYTES);
    std::random_device rd;
    for (auto& byte : result.key) {
        byte = rd() & 0xFF;
    }
    
    // 步驟 6: BCH 編碼金鑰 → C = K + ECC
    struct bch_control* bch = init_bch(BCH_M, BCH_T, 0);
    if (!bch) {
        std::cerr << "❌ BioHash: Failed to initialize BCH" << std::endl;
        return result;
    }
    
    int ecc_len = bch->ecc_bytes;
    std::vector<uint8_t> ecc_bytes(ecc_len);
    encode_bch(bch, result.key.data(), BIOHASH_K_BYTES, ecc_bytes.data());
    free_bch(bch);
    
    // C = K(16B) + ECC
    std::vector<uint8_t> codeword(BIOHASH_K_BYTES + ecc_len);
    memcpy(codeword.data(), result.key.data(), BIOHASH_K_BYTES);
    memcpy(codeword.data() + BIOHASH_K_BYTES, ecc_bytes.data(), ecc_len);
    
    // 步驟 7: sketch δ = B_bytes ⊕ C_data || C_ecc
    // 將人臉 bits 打包為 bytes
    uint8_t B_bytes[BIOHASH_K_BYTES];
    bits_to_bytes(selected_bits.data(), BIOHASH_K, B_bytes);
    
    result.tmpl.sketch.resize(codeword.size());
    // data 部分 XOR (人臉位元遮蔽金鑰)
    for (int i = 0; i < BIOHASH_K_BYTES; i++) {
        result.tmpl.sketch[i] = B_bytes[i] ^ codeword[i];
    }
    // ECC 部分直接複製
    for (int i = BIOHASH_K_BYTES; i < (int)codeword.size(); i++) {
        result.tmpl.sketch[i] = codeword[i];
    }
    
    // 步驟 8: commitment = SHA-256(K)
    result.tmpl.commitment = crypto::sha256(result.key);
    
    result.tmpl.ecc_bytes_len = ecc_len;
    result.tmpl.version = 2;
    
    if (result.tmpl.is_valid()) {
        std::cout << "✅ BioHash v2 template created (Fuzzy Commitment)" << std::endl;
        std::cout << "   - Template size: " 
                  << (1 + 1 + BIOHASH_K * 2 + result.tmpl.sketch.size() + 32 + 2) 
                  << " bytes (excl. payload)" << std::endl;
        std::cout << "   - ECC bytes: " << ecc_len << std::endl;
        std::cout << "   - Key (first 4B): " 
                  << bytes_to_hex_local(std::vector<uint8_t>(result.key.begin(), result.key.begin() + 4))
                  << "..." << std::endl;
    } else {
        std::cerr << "❌ BioHash enrollment failed" << std::endl;
    }
    
    return result;
}

// ==================== Fuzzy Commitment Verify ====================

VerifyResult BioHashProcessor::verify_with_seed(const std::vector<float>& feature,
                                                 const BioHashTemplate& tmpl,
                                                 uint64_t seed) {
    VerifyResult result = {false, 0, {}};
    
    // 步驟 1-3: 投影 → 二值化
    auto matrix = generate_random_matrix(seed);
    std::vector<float> biohash = biohash_projection(feature, matrix);
    std::vector<uint8_t> all_bits = binarize(biohash);
    
    // 步驟 4: 使用模板 indices 提取人臉位元 B'
    uint8_t B_prime_bytes[BIOHASH_K_BYTES] = {0};
    for (int i = 0; i < BIOHASH_K; i++) {
        int idx = tmpl.indices[i];
        if (idx >= 0 && idx < BIOHASH_DIM) {
            int byte_pos = i / 8;
            int bit_pos = 7 - (i % 8);
            B_prime_bytes[byte_pos] |= (all_bits[idx] << bit_pos);
        }
    }
    
    // 步驟 5: C'_data = B'_bytes ⊕ sketch_data
    uint8_t C_prime_data[BIOHASH_K_BYTES];
    for (int i = 0; i < BIOHASH_K_BYTES; i++) {
        C_prime_data[i] = B_prime_bytes[i] ^ tmpl.sketch[i];
    }
    
    // 步驟 6: BCH 解碼 C' → K*
    struct bch_control* bch = init_bch(BCH_M, BCH_T, 0);
    if (!bch) return result;
    
    // 取出 sketch 的 ECC 部分（未被 XOR 的原始 ECC）
    const uint8_t* stored_ecc = tmpl.sketch.data() + BIOHASH_K_BYTES;
    
    // 計算 C'_data 的 ECC
    std::vector<uint8_t> calc_ecc(tmpl.ecc_bytes_len);
    encode_bch(bch, C_prime_data, BIOHASH_K_BYTES, calc_ecc.data());
    
    // BCH 解碼
    std::vector<unsigned int> errloc(BCH_T);
    int nerr = decode_bch(bch, C_prime_data, BIOHASH_K_BYTES,
                          stored_ecc, calc_ecc.data(),
                          nullptr, errloc.data());
    
    if (nerr >= 0) {
        // BCH 解碼成功 → 糾正 C'_data 中的錯誤 → 得到 K*
        correct_bch(bch, C_prime_data, BIOHASH_K_BYTES, errloc.data(), nerr);
        
        // 步驟 7: 驗證 SHA-256(K*) == commitment
        std::vector<uint8_t> K_star(C_prime_data, C_prime_data + BIOHASH_K_BYTES);
        auto hash = crypto::sha256(K_star);
        
        if (hash == tmpl.commitment) {
            result.matched = true;
            result.num_errors = nerr;
            result.recovered_key = K_star;
        }
        // hash 不匹配 = BCH 糾錯後得到了錯誤的金鑰（模板不屬於此人）
    }
    
    free_bch(bch);
    return result;
}

int BioHashProcessor::verify_multiple(const std::vector<float>& feature, 
                                      const std::vector<BioHashTemplate>& templates,
                                      int& best_errors, uint64_t& matching_seed,
                                      std::vector<uint8_t>& recovered_key) {
    if ((int)feature.size() != BIOHASH_DIM || templates.empty()) {
        return -1;
    }
    
    auto candidate_seeds = generate_candidate_seeds();
    std::cout << "🔍 BioHash batch verify (Fuzzy Commitment): " << templates.size() 
              << " templates × " << candidate_seeds.size() << " seeds..." << std::endl;
    
    int best_match_idx = -1;
    best_errors = BCH_T + 1;
    
    for (uint64_t seed : candidate_seeds) {
        // 核心優化：對每個 seed 只做一次投影和二值化
        auto matrix = generate_random_matrix(seed);
        std::vector<float> biohash = biohash_projection(feature, matrix);
        std::vector<uint8_t> all_bits = binarize(biohash);
        
        // 遍歷所有模板
        for (size_t i = 0; i < templates.size(); i++) {
            const auto& tmpl = templates[i];
            if (!tmpl.is_valid()) continue;
            
            // 提取 B' 位元
            uint8_t B_prime_bytes[BIOHASH_K_BYTES] = {0};
            for (int k = 0; k < BIOHASH_K; k++) {
                int idx = tmpl.indices[k];
                if (idx >= 0 && idx < BIOHASH_DIM) {
                    int byte_pos = k / 8;
                    int bit_pos = 7 - (k % 8);
                    B_prime_bytes[byte_pos] |= (all_bits[idx] << bit_pos);
                }
            }
            
            // C'_data = B' ⊕ sketch_data
            uint8_t C_prime_data[BIOHASH_K_BYTES];
            for (int b = 0; b < BIOHASH_K_BYTES; b++) {
                C_prime_data[b] = B_prime_bytes[b] ^ tmpl.sketch[b];
            }
            
            // BCH 解碼
            struct bch_control* bch = init_bch(BCH_M, BCH_T, 0);
            if (!bch) continue;
            
            const uint8_t* stored_ecc = tmpl.sketch.data() + BIOHASH_K_BYTES;
            std::vector<uint8_t> calc_ecc(tmpl.ecc_bytes_len);
            encode_bch(bch, C_prime_data, BIOHASH_K_BYTES, calc_ecc.data());
            
            std::vector<unsigned int> errloc(BCH_T);
            int nerr = decode_bch(bch, C_prime_data, BIOHASH_K_BYTES,
                                  stored_ecc, calc_ecc.data(),
                                  nullptr, errloc.data());
            
            if (nerr >= 0) {
                correct_bch(bch, C_prime_data, BIOHASH_K_BYTES, errloc.data(), nerr);
                
                std::vector<uint8_t> K_star(C_prime_data, C_prime_data + BIOHASH_K_BYTES);
                auto hash = crypto::sha256(K_star);
                
                if (hash == tmpl.commitment && nerr < best_errors) {
                    best_errors = nerr;
                    best_match_idx = i;
                    matching_seed = seed;
                    recovered_key = K_star;
                }
            }
            
            free_bch(bch);
        }
        
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
