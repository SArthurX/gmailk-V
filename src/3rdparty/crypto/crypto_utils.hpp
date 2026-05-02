#ifndef CRYPTO_UTILS_HPP
#define CRYPTO_UTILS_HPP

/**
 * @file crypto_utils.hpp
 * @brief C++ 封裝 SHA-256 + AES-128-CTR + HMAC-SHA256
 * 
 * 用於 Fuzzy Commitment 方案的加密工具：
 * - sha256(): 計算 SHA-256 雜湊
 * - hmac_sha256(): 計算 HMAC-SHA256 認證碼
 * - encrypt_payload(): AES-128-CTR 加密 + HMAC 認證
 * - decrypt_payload(): HMAC 驗證 + AES-128-CTR 解密
 */

#include <vector>
#include <array>
#include <string>
#include <cstring>
#include <cstdint>
#include <random>

extern "C" {
#include "sha256.h"
#include "aes.h"
}

namespace crypto {

// ==================== SHA-256 ====================

/**
 * @brief 計算 SHA-256 雜湊
 * @param data 輸入資料
 * @return 32 bytes 雜湊值
 */
inline std::array<uint8_t, 32> sha256(const std::vector<uint8_t>& data) {
    std::array<uint8_t, 32> hash;
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data.data(), data.size());
    sha256_final(&ctx, hash.data());
    return hash;
}

/**
 * @brief 計算 SHA-256 雜湊（指標版本）
 */
inline std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len) {
    std::array<uint8_t, 32> hash;
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, hash.data());
    return hash;
}

// ==================== HMAC-SHA256 ====================

/**
 * @brief 計算 HMAC-SHA256
 * @param key 金鑰
 * @param key_len 金鑰長度
 * @param data 資料
 * @param data_len 資料長度
 * @return 32 bytes HMAC 值
 */
inline std::array<uint8_t, 32> hmac_sha256(
    const uint8_t* key, size_t key_len,
    const uint8_t* data, size_t data_len)
{
    // HMAC(K, m) = H((K' ⊕ opad) || H((K' ⊕ ipad) || m))
    const size_t BLOCK_SIZE = 64;
    uint8_t k_prime[BLOCK_SIZE];
    memset(k_prime, 0, BLOCK_SIZE);

    if (key_len > BLOCK_SIZE) {
        // 金鑰過長，先 hash
        auto hk = sha256(key, key_len);
        memcpy(k_prime, hk.data(), 32);
    } else {
        memcpy(k_prime, key, key_len);
    }

    // inner: (K' ⊕ ipad) || message
    uint8_t i_key_pad[BLOCK_SIZE];
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        i_key_pad[i] = k_prime[i] ^ 0x36;
    }

    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, i_key_pad, BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    uint8_t inner_hash[32];
    sha256_final(&ctx, inner_hash);

    // outer: (K' ⊕ opad) || inner_hash
    uint8_t o_key_pad[BLOCK_SIZE];
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        o_key_pad[i] = k_prime[i] ^ 0x5c;
    }

    sha256_init(&ctx);
    sha256_update(&ctx, o_key_pad, BLOCK_SIZE);
    sha256_update(&ctx, inner_hash, 32);

    std::array<uint8_t, 32> result;
    sha256_final(&ctx, result.data());
    return result;
}

// ==================== AES-128-CTR 加解密 ====================

/**
 * @brief AES-128-CTR 加密/解密（對稱操作）
 * @param key 16 bytes 金鑰
 * @param iv 16 bytes 初始化向量
 * @param data 輸入資料（會被原地修改）
 * @param len 資料長度
 */
inline void aes128_ctr_xcrypt(const uint8_t* key, const uint8_t* iv,
                               uint8_t* data, size_t len)
{
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, iv);
    AES_CTR_xcrypt_buffer(&ctx, data, len);
}

// ==================== 酬載加解密（Encrypt-then-MAC）====================

/**
 * @brief 加密酬載
 * 
 * 格式: [IV:16B][ciphertext:N][HMAC-SHA256:32B]
 * - IV = random 16 bytes
 * - ciphertext = AES-128-CTR(key, IV, plaintext)
 * - HMAC = HMAC-SHA256(key, IV || ciphertext)
 * 
 * @param key 16 bytes 金鑰（來自 Fuzzy Commitment 恢復的 K）
 * @param plaintext 明文字串
 * @return 加密後的 blob (IV + ciphertext + HMAC)
 */
inline std::vector<uint8_t> encrypt_payload(
    const std::vector<uint8_t>& key,
    const std::string& plaintext)
{
    if (key.size() != 16 || plaintext.empty()) {
        return {};
    }

    // 生成隨機 IV
    uint8_t iv[16];
    std::random_device rd;
    for (int i = 0; i < 16; i++) {
        iv[i] = rd() & 0xFF;
    }

    // 複製明文到 buffer（AES_CTR 原地加密）
    std::vector<uint8_t> ciphertext(plaintext.begin(), plaintext.end());
    aes128_ctr_xcrypt(key.data(), iv, ciphertext.data(), ciphertext.size());

    // 組裝: IV + ciphertext
    std::vector<uint8_t> iv_ct;
    iv_ct.reserve(16 + ciphertext.size());
    iv_ct.insert(iv_ct.end(), iv, iv + 16);
    iv_ct.insert(iv_ct.end(), ciphertext.begin(), ciphertext.end());

    // 計算 HMAC-SHA256(key, IV || ciphertext)
    auto hmac = hmac_sha256(key.data(), key.size(), iv_ct.data(), iv_ct.size());

    // 最終輸出: IV + ciphertext + HMAC
    std::vector<uint8_t> result;
    result.reserve(16 + ciphertext.size() + 32);
    result.insert(result.end(), iv_ct.begin(), iv_ct.end());
    result.insert(result.end(), hmac.begin(), hmac.end());

    return result;
}

/**
 * @brief 解密酬載
 * 
 * 驗證 HMAC → AES-128-CTR 解密 → 返回明文
 * 
 * @param key 16 bytes 金鑰（從 Fuzzy Commitment 恢復的 K*）
 * @param blob 加密 blob (IV + ciphertext + HMAC)
 * @param[out] plaintext 解密後的明文
 * @return true 解密成功（HMAC 驗證通過），false 失敗
 */
inline bool decrypt_payload(
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& blob,
    std::string& plaintext)
{
    // 最小長度: IV(16) + 最少 1 byte + HMAC(32) = 49
    if (key.size() != 16 || blob.size() < 49) {
        return false;
    }

    size_t ct_len = blob.size() - 16 - 32;  // ciphertext 長度
    const uint8_t* iv = blob.data();
    const uint8_t* ciphertext = blob.data() + 16;
    const uint8_t* stored_hmac = blob.data() + 16 + ct_len;

    // 1. 驗證 HMAC (Encrypt-then-MAC: 先驗再解)
    auto computed_hmac = hmac_sha256(
        key.data(), key.size(),
        blob.data(), 16 + ct_len);  // HMAC(key, IV || ciphertext)

    // 常數時間比較，防 timing attack
    uint8_t diff = 0;
    for (size_t i = 0; i < 32; i++) {
        diff |= computed_hmac[i] ^ stored_hmac[i];
    }
    if (diff != 0) {
        return false;  // HMAC 不匹配：資料被篡改或金鑰錯誤
    }

    // 2. AES-128-CTR 解密
    std::vector<uint8_t> pt_buf(ciphertext, ciphertext + ct_len);
    aes128_ctr_xcrypt(key.data(), iv, pt_buf.data(), pt_buf.size());

    plaintext.assign(pt_buf.begin(), pt_buf.end());
    return true;
}

// ==================== Hex 輔助 ====================

inline std::string bytes_to_hex(const uint8_t* data, size_t len) {
    static const char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        result += hex_chars[(data[i] >> 4) & 0x0F];
        result += hex_chars[data[i] & 0x0F];
    }
    return result;
}

inline std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        uint8_t b = 0;
        for (int j = 0; j < 2; j++) {
            char c = hex[i + j];
            b <<= 4;
            if (c >= '0' && c <= '9') b |= (c - '0');
            else if (c >= 'a' && c <= 'f') b |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') b |= (c - 'A' + 10);
        }
        bytes.push_back(b);
    }
    return bytes;
}

} // namespace crypto

#endif // CRYPTO_UTILS_HPP
