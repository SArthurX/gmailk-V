#ifndef BIOHASH_PROCESSOR_H
#define BIOHASH_PROCESSOR_H

#include <vector>
#include <string>
#include <cstdint>
#include <array>

// ==================== BioHash + BCH 參數 ====================
#define BIOHASH_FEATURE_DIM  512   // ArcFace 輸入特徵維度（不變）
#define BIOHASH_PROJ_DIM    2048   // 隨機投影輸出維度（4× 擴展，安全性修復）
#define BIOHASH_K           511    // 可靠位元數 = BCH 碼字長度 n（全域 XOR 覆蓋）
#define BIOHASH_K_BYTES     ((BIOHASH_K + 7) / 8)  // = 64 bytes
#define BCH_M               9     // GF(2^9), 碼字長度 n = 511
#define BCH_T               42    // 糾錯能力：可糾正 42 個錯誤 (量化分析驗證)
#define BCH_N               ((1 << BCH_M) - 1)  // = 511
#define BIOHASH_KEY_BYTES   16    // 隨機金鑰 K 長度 = 128 bits
#define COMMITMENT_SIZE     32    // SHA-256 output size

/**
 * @brief BioHash 保護模板 — Fuzzy Commitment 方案 (v3)
 * 
 * v3 格式: [version:1B=0x03][ecc_len:1B][indices:1022B][sketch:64B]
 *          [commitment:32B][enc_payload_len:2B][encrypted_payload:變長]
 * 
 * v3 安全性升級 (相比 v2):
 *   - 投影維度 512→2048，選取 511 個最穩定 bits（取代 128 bits）
 *   - sketch = B(511 bits) ⊕ C(511 bits) 全域 XOR（消除 ECC 明文洩漏漏洞）
 *   - BCH_T 從 25 升級至 42（量化分析驗證，INT8 正臉 Max=14，三倍容錯）
 */
struct BioHashTemplate {
    uint8_t version = 3;                         // 模板版本 (3 = 2048-dim 全域 XOR)
    std::vector<int> indices;                    // BIOHASH_K 個可靠位元索引
    std::vector<uint8_t> sketch;                 // δ = B ⊕ C (取代 codeword)
    std::array<uint8_t, COMMITMENT_SIZE> commitment = {};  // SHA-256(K)
    int ecc_bytes_len;                           // ECC 位元組長度
    std::vector<uint8_t> encrypted_payload;      // AES-128-CTR 加密酬載 (可選)
    
    /**
     * @brief 序列化為 hex 字串 (v2 格式)
     * 格式: [0x02][ecc_len:1B][indices:256B][sketch][commitment:32B]
     *       [enc_payload_len:2B][encrypted_payload]
     */
    std::string to_hex() const;
    
    /**
     * @brief 從 hex 字串反序列化 (自動偵測 v2 格式)
     */
    static BioHashTemplate from_hex(const std::string& hex);
    
    /**
     * @brief 檢查模板是否有效
     */
    bool is_valid() const { return !sketch.empty() && indices.size() == BIOHASH_K; }
};

/**
 * @brief Fuzzy Commitment 註冊結果
 * 
 * enroll() 返回模板 + 128-bit 隨機金鑰，呼叫者可用金鑰加密酬載。
 */
struct EnrollResult {
    BioHashTemplate tmpl;
    std::vector<uint8_t> key;   // 128-bit 隨機金鑰（BIOHASH_KEY_BYTES bytes）
};

/**
 * @brief Fuzzy Commitment 驗證結果
 * 
 * verify 成功時，recovered_key 即為原始的隨機金鑰 K，可用於解密酬載。
 */
struct VerifyResult {
    bool matched;
    int num_errors;
    std::vector<uint8_t> recovered_key;  // 恢復的 128-bit 金鑰
};

/**
 * @brief BioHash 處理器 — Fuzzy Commitment 方案
 * 
 * 結合 BioHash 隨機投影 + BCH 糾錯碼 + Fuzzy Commitment，實現可撤銷的人臉特徵保護
 * 並支持面部衍生金鑰功能（加密酬載）。
 * 
 * 註冊: feature → 隨機投影 → 二值化 → 可靠位元 B → 隨機金鑰 K →
 *       BCH(K)=C → sketch δ=B⊕C → commitment=SHA256(K) → 模板
 * 驗證: feature' → 投影 → 二值化 → B' → C'=B'⊕δ → BCH解碼→K* → SHA256(K*)==h?
 */
class BioHashProcessor {
public:
    BioHashProcessor();
    ~BioHashProcessor();
    
    /**
     * @brief 生成當前日期種子 (YYYYMMDD0000 格式)
     * 例如: 202604300000 (代表整天有效)
     */
    static uint64_t get_datetime_seed();
    
    /**
     * @brief 解析 valid_date 字串為種子值
     * 
     * 支援萬用零位：
     *   "202604301725" → 202604301725 (精確到分鐘)
     *   "202604301700" → 202604301700 (整時有效)
     *   "202604300000" → 202604300000 (整天有效)
     *   "202604000000" → 202604000000 (整月有效)
     *   "202600000000" → 202600000000 (整年有效)
     * 
     * @param valid_date 12 位數字字串 (YYYYMMDDHHmm)
     * @return 種子值，若格式無效則返回 0
     */
    static uint64_t parse_valid_date(const std::string& valid_date);
    
    /**
     * @brief 生成候選種子列表（恆定 5 個）
     * 
     * 基於當前時間生成 5 個層級的候選種子：
     * - 年級: YYYY00000000
     * - 月級: YYYYMM000000
     * - 日級: YYYYMMDD0000
     * - 時級: YYYYMMDDHHmm 的 mm=00
     * - 分級: YYYYMMDDHHmm (精確到當前分鐘)
     * 
     * @return 候選種子列表（5 個，從粗到細）
     */
    static std::vector<uint64_t> generate_candidate_seeds();
    
    /**
     * @brief 註冊：Fuzzy Commitment 方案
     * 
     * 1. 特徵 → 隨機投影 → 二值化 → 可靠位元選擇 → 人臉位元 B
     * 2. 生成 128-bit 隨機金鑰 K → BCH 編碼 → 碼字 C
     * 3. sketch δ = B ⊕ C (XOR 遮蔽)
     * 4. commitment = SHA-256(K)
     * 5. 返回 {模板(indices+sketch+commitment), 金鑰K}
     * 
     * @param feature 512 維 L2 正規化的 ArcFace 特徵向量
     * @param seed 隨機投影種子（通常為日期種子）
     * @return EnrollResult {模板, 金鑰}
     */
    EnrollResult enroll(const std::vector<float>& feature, uint64_t seed);

    /**
     * @brief 批量驗證：Fuzzy Commitment + 金鑰恢復
     * 
     * 對每個候選種子做一次投影+二值化，再掃描所有模板：
     * C' = B' ⊕ δ → BCH 解碼 → K* → SHA-256(K*) == commitment?
     * 
     * @param feature 測試的特徵向量
     * @param templates 要驗證的模板陣列
     * @param best_errors [out] 最佳匹配的錯誤位元數
     * @param matching_seed [out] 最佳匹配對應的種子
     * @param recovered_key [out] 恢復的金鑰（可用於解密酬載）
     * @return 最佳匹配在陣列中的索引，若無匹配則回傳 -1
     */
    int verify_multiple(const std::vector<float>& feature, 
                        const std::vector<BioHashTemplate>& templates,
                        int& best_errors, uint64_t& matching_seed,
                        std::vector<uint8_t>& recovered_key);

private:
    /**
     * @brief 使用指定種子嘗試 Fuzzy Commitment 驗證
     * @return VerifyResult 包含匹配結果和恢復的金鑰
     */
    VerifyResult verify_with_seed(const std::vector<float>& feature,
                                  const BioHashTemplate& tmpl,
                                  uint64_t seed);
};

#endif // BIOHASH_PROCESSOR_H
