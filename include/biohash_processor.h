#ifndef BIOHASH_PROCESSOR_H
#define BIOHASH_PROCESSOR_H

#include <vector>
#include <string>
#include <cstdint>

// ==================== BioHash + BCH 參數 ====================
#define BIOHASH_DIM       512     // 投影維度 = ArcFace 特徵維度
#define BIOHASH_K         128     // 可靠位元數 (提高至 128 以提升安全性與穩定性)
#define BIOHASH_K_BYTES   ((BIOHASH_K + 7) / 8)  // = 16 bytes
#define BCH_M             9       // GF(2^9), 碼字長度 n = 511
#define BCH_T             25      // 糾錯能力：可糾正 25 個錯誤 (容錯率 ~19.5%)

/**
 * @brief BioHash 保護模板（不含 seed）
 * 
 * 儲存格式: [ECC長度:1byte][索引:64bytes][碼字:data+ecc bytes]
 * seed 不存入模板，驗證時使用時間遞減策略嘗試多個候選種子
 */
struct BioHashTemplate {
    std::vector<int> indices;       // BIOHASH_K 個可靠位元索引
    std::vector<uint8_t> codeword;  // BCH 碼字 (data + ECC)
    int ecc_bytes_len;              // ECC 位元組長度
    
    /**
     * @brief 序列化為 hex 字串
     * 格式: [1 byte ecc_len][64 bytes indices][codeword bytes]
     */
    std::string to_hex() const;
    
    /**
     * @brief 從 hex 字串反序列化
     */
    static BioHashTemplate from_hex(const std::string& hex);
    
    /**
     * @brief 檢查模板是否有效
     */
    bool is_valid() const { return !codeword.empty() && indices.size() == BIOHASH_K; }
};

/**
 * @brief BioHash 處理器
 * 
 * 結合 BioHash 隨機投影 + BCH 糾錯碼，實現可撤銷的人臉特徵保護。
 * 
 * 註冊流程: 特徵 → 隨機投影 → 二值化 → 可靠位元選擇 → BCH 編碼 → 模板
 * 驗證流程: 特徵 → 嘗試候選種子 → 投影+二值化 → 用模板索引提取 → BCH 解碼
 */
class BioHashProcessor {
public:
    BioHashProcessor();
    ~BioHashProcessor();
    
    /**
     * @brief 生成當前時間種子 (YYYYMMDDHHmmss 格式的整數)
     * 例如: 20260311163800
     */
    static uint64_t get_datetime_seed();
    
    /**
     * @brief 生成候選種子列表
     * 
     * 從當前時間往回遞減，按層級生成候選種子：
     * - 月級: YYYYMM (1個)
     * - 日級: YYYYMMDD (最多31個)
     * - 時級: YYYYMMDDHH (最多24個)
     * 
     * 驗證時從粗粒度開始嘗試，以提高效能。
     * 
     * @return 候選種子列表（月→日→時順序）
     */
    static std::vector<uint64_t> generate_candidate_seeds();
    
    /**
     * @brief 註冊：從特徵向量生成保護模板
     * @param feature 512 維 L2 正規化的 ArcFace 特徵向量
     * @param seed 隨機投影種子（通常為 get_datetime_seed() 的返回值）
     * @return BioHash 模板（不含 seed）
     */
    BioHashTemplate enroll(const std::vector<float>& feature, uint64_t seed);
    
    /**
     * @brief 驗證：嘗試所有候選種子進行 BCH 解碼
     * @param feature 512 維待驗證特徵向量
     * @param tmpl 已儲存的 BioHash 模板
     * @param num_errors [輸出] BCH 糾正的錯誤數
     * @return true 驗證通過（BCH 解碼成功），false 失敗
     */
    bool verify(const std::vector<float>& feature, const BioHashTemplate& tmpl,
                int& num_errors);

    /**
     * @brief 批量驗證測試特徵與多個模板，大幅優化效能
     * @param feature 測試的特徵向量
     * @param templates 要驗證的模板陣列
     * @param best_errors [out] 最佳匹配的錯誤位元數
     * @param matching_seed [out] 最佳匹配對應的種子
     * @return 最佳匹配在陣列中的索引，若無匹配則回傳 -1
     */
    int verify_multiple(const std::vector<float>& feature, 
                        const std::vector<BioHashTemplate>& templates,
                        int& best_errors, uint64_t& matching_seed);

private:
    /**
     * @brief 使用指定種子嘗試單次驗證
     */
    bool verify_with_seed(const std::vector<float>& feature,
                          const BioHashTemplate& tmpl,
                          uint64_t seed, int& num_errors);
};

#endif // BIOHASH_PROCESSOR_H
