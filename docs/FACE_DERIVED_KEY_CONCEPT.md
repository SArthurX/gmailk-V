# 面部衍生金鑰 (Face-Derived Key) — Fuzzy Commitment 方案

> **建立日期**：2026-04-07  
> **狀態**：✅ v3 實作完成 (2048-dim 全域 XOR + BCH_T=42)  
> **前置文件**：`bioh-bch/process_explanation.md`、`docs/RPI_ARCHITECTURE.md`  
> **參考理論**：Juels & Wattenberg, "A Fuzzy Commitment Scheme" (1999)

---

## 目錄

1. [問題：為什麼不能直接用碼字當金鑰](#1-問題為什麼不能直接用碼字當金鑰)
2. [解法：Fuzzy Commitment 原理](#2-解法fuzzy-commitment-原理)
3. [完整流程（逐步圖解）](#3-完整流程逐步圖解)
4. [模板格式變化](#4-模板格式變化)
5. [程式碼改動規劃](#5-程式碼改動規劃)
6. [安全性分析](#6-安全性分析)
7. [使用場景](#7-使用場景)
8. [實作擴展規劃](#8-實作擴展規劃)
9. [待決定事項](#9-待決定事項)

---

## 1. 問題：為什麼不能直接用碼字當金鑰

### 1.1 現有模板結構

```
biohash_template_hex = [ecc_len: 1B][indices: 256B][codeword: 16B data + ecc]
```

查看 `biohash_processor.cpp` L152-155：

```cpp
// 組裝碼字 = data + ECC
std::vector<uint8_t> codeword(BIOHASH_K_BYTES + ecc_bytes_len);
memcpy(codeword.data(), data_bytes, BIOHASH_K_BYTES);        // ← 明文！
memcpy(codeword.data() + BIOHASH_K_BYTES, ecc_bytes.data(), ecc_bytes_len);
```

**Systematic BCH**（系統碼）的特性：碼字 = `原始資料 + 校驗碼`。資料部分以明文形式嵌在碼字的前 16 bytes。

### 1.2 攻擊方式

如果用「BCH 解碼恢復的 selected_bits」當金鑰加密酬載：

```
攻擊者拿到模板 hex
  → 跳過 1 byte (ecc_len)
  → 跳過 256 bytes (128 個 indices，每個 2 bytes)
  → 讀前 16 bytes = selected_bits 明文 = 金鑰
  → SHA-256 → AES key → 解密酬載
  → 不需要看到你的臉
```

**結論**：在現有的 Systematic BCH 架構下，碼字中的 data bits 就是明文，直接拿來當金鑰等於沒加密。

---

## 2. 解法：Fuzzy Commitment 原理

### 2.1 核心思想

> **不儲存碼字本身，而是儲存「碼字被人臉遮蔽後的結果」。**
> 只有正確的人臉才能把碼字從遮蔽中還原出來。

用一個數學運算來實現：**XOR (⊕)**。

```
sketch δ = 人臉位元 B ⊕ 隨機碼字 C
```

- 知道 δ 但不知道 B → 推不出 C
- 知道 δ 而且提供 B' ≈ B → `B' ⊕ δ = B' ⊕ B ⊕ C ≈ C`（帶少量錯誤）→ BCH 糾錯 → 恢復 C

### 2.2 為什麼 XOR 是安全的

XOR 在密碼學中是**完美保密**的工具（One-Time Pad 原理）：

```
δ = B ⊕ C

已知 δ，對每個可能的 C 值，都恰好存在一個 B 使等式成立。
→ 攻擊者無法從 δ 分辨哪個 C 是正確的。
→ 唯一的「解密方式」是取得 B（拿臉去拍）。
```

### 2.3 與現行方案的對照

```
現行方案 (直接 BCH):
  儲存: {indices, codeword}
  碼字 = data_明文 + ECC
  驗證: BCH_decode(received_bits, codeword) → match?
  ❌ data_明文 可直接讀取

Fuzzy Commitment 方案:
  儲存: {indices, δ(sketch), h(hash_commitment)}
  sketch = 人臉位元 ⊕ 隨機碼字
  驗證: C' = received_bits ⊕ δ → BCH_decode(C') → K → SHA-256(K)==h?
  ✅ 隨機碼字被 XOR 隱藏，只有正確的臉能恢復
```

---

## 3. 完整流程（逐步圖解）

### 3.1 註冊 (Enroll)

以下是改造後的完整註冊流程。**步驟 1-4 與現行完全一樣**，差異從步驟 5 開始。

```
步驟 1: 特徵提取                          [不變]
  ArcFace TPU → feature (512 維 float, L2 正規化)

步驟 2: 隨機投影                          [不變]
  seed = 當前日期 (YYYYMMDD)
  matrix = generate_random_matrix(seed)     // 512×512 偽隨機矩陣
  projected = matrix × feature              // 512 維投影值

步驟 3: 二值化                            [不變]
  median = 中位數(projected)
  all_bits[i] = (projected[i] > median) ? 1 : 0    // 512 bits

步驟 4: 可靠位元選擇                      [不變]
  按 |projected[i]| 排序，取前 K=128 個
  → selected_bits: B (128 bits)             ← 這是「人臉位元」
  → selected_indices (128 個索引位置)

═══════════════ 以下是改動的部分 ═══════════════

步驟 5: 生成隨機金鑰                      [新增]
  K = random_bytes(16)                      // 128 bits 真隨機金鑰
  這把金鑰 K 就是之後加解密酬載用的鑰匙

步驟 6: BCH 編碼金鑰                      [改動]
  C = BCH_encode(K)                         // C = K(16B) + ECC
  ↑ 注意：編碼的是隨機金鑰 K，不是人臉位元 B

步驟 7: 計算 sketch                       [新增]
  把 B 和 C 都表示為 bit 陣列
  B_bytes = bits_to_bytes(B)                // 16 bytes
  C_data  = C[0..16]                        // 碼字的 data 部分 = K 本身

  δ_data = B_bytes ⊕ C_data                // 遮蔽 data
  δ_ecc  = C[16..]                          // ECC 保持原樣
  δ = δ_data + δ_ecc                        // 完整 sketch

步驟 8: 計算承諾 (commitment)              [新增]
  h = SHA-256(K)                            // 金鑰的雜湊

步驟 9: 組裝模板                          [改動]
  模板 = {indices, δ(sketch), h(commitment), ecc_bytes_len}

步驟 10 (可選): 加密酬載                   [新增]
  payload = {"name": "Alice", "age": 28, ...}
  aes_key = K (或 SHA-256(K) 的前 32 bytes)
  encrypted_payload = AES-256-GCM(key=aes_key, plaintext=JSON(payload))
```

**視覺化**：

```
        人臉位元 B                隨機金鑰 K
     [10110100...]              [01001110...]
            │                         │
            │                    BCH_encode
            │                         │
            │                    碼字 C = [K + ECC]
            │                         │
            └────── XOR ──────────────┘
                     │
              sketch δ = B ⊕ C
                     │
              ┌──────┴──────┐
              │             │
            儲存 δ      SHA-256(K)
                          = h
              │             │
              └──────┬──────┘
                     │
                 模板存入 RPi
            {indices, δ, h}
```

### 3.2 驗證 (Verify)

```
步驟 1-4: 與註冊完全相同                   [不變]
  feature' → projection(seed) → binarize → select_reliable_bits
  → B' (128 bits, 與 B 有少量錯誤)

步驟 5: 從模板取出 sketch δ               [新增]
  從 RPi GET 模板 → 解析出 {indices, δ, h}

步驟 6: XOR 恢復碼字                      [改動]
  B'_bytes = bits_to_bytes(B')
  δ_data = δ[0..16]
  
  C'_data = B'_bytes ⊕ δ_data
  
  展開:
    C'_data = B' ⊕ (B ⊕ K)
            = (B' ⊕ B) ⊕ K
            = errors ⊕ K       ← K 帶了少量位元錯誤

  C' = C'_data + δ_ecc         ← 完整的帶噪碼字

步驟 7: BCH 解碼                          [改動]
  K* = BCH_decode(C')           ← BCH 糾錯恢復原始金鑰
  
  如果 B' 和 B 的差異 ≤ BCH_T (25 bits) → 解碼成功
  如果差異 > 25 bits → 解碼失敗 → 臉不對

步驟 8: 驗證承諾                          [新增]
  SHA-256(K*) == h ?
  → 是: 身份確認，K* 就是原始的金鑰 K
  → 否: 解碼出錯（理論上極罕見，除非模板被篡改）

步驟 9 (可選): 解密酬載                   [新增]
  aes_key = K*
  plaintext = AES-256-GCM_decrypt(key=aes_key, ciphertext=encrypted_payload)
  → {"name": "Alice", "age": 28, ...}
```

**視覺化**：

```
        人臉位元 B'               儲存的 sketch δ
    (帶少量錯誤的 B)               = B ⊕ C
     [10110101...]              [11111010...]
            │                         │
            └────── XOR ──────────────┘
                     │
                C' = B' ⊕ δ
                   = (B' ⊕ B) ⊕ C      ← errors ⊕ C
                     │
                BCH 解碼
                (糾正 errors)
                     │
                K* = 恢復的金鑰
                     │
            ┌────────┼────────┐
            │        │        │
     SHA-256(K*)  解密酬載   身份確認
       == h?     AES(K*,    ✅ 通過
                 payload)
```

### 3.3 候選種子掃描（不變）

驗證時仍然使用 `generate_candidate_seeds()` 從當前時間往回推：

```
for seed in [月種子, 日種子×31, 時種子×24]:
    matrix = generate_random_matrix(seed)
    projected = matrix × feature'
    all_bits = binarize(projected)
    B' = all_bits[stored_indices]
    
    C' = B' ⊕ δ_data + δ_ecc
    K* = BCH_decode(C')
    
    if K* 有效 && SHA-256(K*) == h:
        → 找到匹配！金鑰 = K*
        break
```

這部分邏輯與現行的 `verify_multiple()` 結構完全一致，只是內迴圈從「直接 BCH 解碼」變成「XOR + BCH 解碼 + hash 驗證」。

---

## 4. 模板格式變化

### 4.1 現有格式 (v1)

```
hex 序列化:
  [ecc_len: 1B][indices: 256B][codeword: 16B + eccB]

BioHashTemplate struct:
  indices:       vector<int>     // 128 個索引
  codeword:      vector<uint8_t> // data(16B) + ECC
  ecc_bytes_len: int
```

### 4.2 新格式 (v2 — Fuzzy Commitment)

```
hex 序列化:
  [version: 1B = 0x02]
  [ecc_len: 1B]
  [indices: 256B]
  [sketch: 16B + eccB]         ← 取代 codeword
  [commitment: 32B]            ← SHA-256(K)
  [encrypted_payload_len: 2B]  ← 0 表示無酬載
  [encrypted_payload: 變長]    ← AES-GCM 密文 (可選)

BioHashTemplate struct (改動):
  uint8_t version;                      // = 2
  std::vector<int> indices;             // 128 個索引 [不變]
  std::vector<uint8_t> sketch;          // δ = B ⊕ C  [取代 codeword]
  std::array<uint8_t, 32> commitment;   // SHA-256(K) [新增]
  int ecc_bytes_len;                    // [不變]
  std::vector<uint8_t> encrypted_payload; // AES 密文 [新增，可選]
```

### 4.3 向下兼容

- v1 模板（無 version byte，首 byte 是 ecc_len，值通常 < 64）
- v2 模板（首 byte = 0x02）
- 解析時檢查首 byte：`if (data[0] == 0x02) → v2，else → v1 (legacy)`
- v1 模板仍可用於**純身份驗證**，但不支持金鑰解密

---

## 5. 程式碼改動規劃

### 5.1 需要改動的檔案

| 檔案 | 改動程度 | 說明 |
|------|----------|------|
| `include/biohash_processor.h` | **中等** | BioHashTemplate 結構新增欄位 |
| `src/biohash_processor.cpp` | **大** | enroll/verify 邏輯改為 Fuzzy Commitment |
| `include/face_database.h` | **小** | PersonInfo_t 可選新增 encrypted_payload |
| `src/face_database.cpp` | **小** | 解析/存儲新欄位 |
| `src/tdl_handler.cpp` | **小** | 驗證成功後取得 key + 呼叫解密 |
| RPi `main.rs` | **小** | persons 表加 encrypted_payload 欄位 |
| RPi `index.html` | **小** | UI 新增酬載輸入區 |

### 5.2 biohash_processor.h 改動

```cpp
// 新增常量
#define COMMITMENT_SIZE 32  // SHA-256 output

struct BioHashTemplate {
    uint8_t version = 2;                        // 新增
    std::vector<int> indices;                    // 不變
    std::vector<uint8_t> sketch;                 // 取代 codeword
    std::array<uint8_t, 32> commitment;          // 新增: SHA-256(K)
    int ecc_bytes_len;                           // 不變
    std::vector<uint8_t> encrypted_payload;      // 新增: 可選

    std::string to_hex() const;
    static BioHashTemplate from_hex(const std::string& hex);
    bool is_valid() const;
};

class BioHashProcessor {
public:
    // enroll 改為返回模板 + 金鑰
    struct EnrollResult {
        BioHashTemplate tmpl;
        std::vector<uint8_t> key;   // 128-bit 隨機金鑰（呼叫者可用於加密酬載）
    };
    
    EnrollResult enroll_v2(const std::vector<float>& feature, uint64_t seed);
    
    // verify 改為返回金鑰（可選）
    struct VerifyResult {
        bool matched;
        int num_errors;
        std::vector<uint8_t> recovered_key;  // 恢復的金鑰
    };
    
    VerifyResult verify_v2(const std::vector<float>& feature, 
                           const BioHashTemplate& tmpl);
    
    // ... 保留原有 enroll/verify 做 v1 相容 ...
};
```

### 5.3 biohash_processor.cpp 核心改動

```cpp
// ═══ enroll_v2 ═══
EnrollResult BioHashProcessor::enroll_v2(const std::vector<float>& feature, 
                                          uint64_t seed) {
    EnrollResult result;
    
    // 步驟 1-4: 與現有完全相同
    auto matrix = generate_random_matrix(seed);
    auto biohash = biohash_projection(feature, matrix);
    auto all_bits = binarize(biohash);
    
    std::vector<uint8_t> selected_bits;  // B
    select_reliable_bits(biohash, all_bits, selected_bits, result.tmpl.indices);
    
    // 步驟 5: 生成隨機金鑰 K
    result.key.resize(BIOHASH_K_BYTES);   // 16 bytes
    // 使用 /dev/urandom 或 std::random_device
    std::random_device rd;
    for (auto& byte : result.key) {
        byte = rd() & 0xFF;
    }
    
    // 步驟 6: BCH 編碼金鑰 → C
    // 把 K 轉為 bit 陣列
    std::vector<uint8_t> key_bits(BIOHASH_K);
    for (int i = 0; i < BIOHASH_K_BYTES; i++) {
        for (int j = 0; j < 8 && (i*8+j) < BIOHASH_K; j++) {
            key_bits[i*8+j] = (result.key[i] >> (7-j)) & 1;
        }
    }
    int ecc_len = 0;
    auto codeword = bch_encode_bits(key_bits, ecc_len);  // C = K + ECC
    
    // 步驟 7: sketch δ = B_bytes ⊕ C_data, δ_ecc = C_ecc
    std::vector<uint8_t> B_bytes(BIOHASH_K_BYTES, 0);
    for (int i = 0; i < BIOHASH_K_BYTES; i++) {
        for (int j = 0; j < 8 && (i*8+j) < BIOHASH_K; j++) {
            B_bytes[i] |= (selected_bits[i*8+j] << (7-j));
        }
    }
    
    result.tmpl.sketch.resize(codeword.size());
    // data 部分 XOR
    for (int i = 0; i < BIOHASH_K_BYTES; i++) {
        result.tmpl.sketch[i] = B_bytes[i] ^ codeword[i];
    }
    // ECC 部分直接複製
    for (size_t i = BIOHASH_K_BYTES; i < codeword.size(); i++) {
        result.tmpl.sketch[i] = codeword[i];
    }
    
    // 步驟 8: commitment = SHA-256(K)
    result.tmpl.commitment = sha256(result.key);
    
    result.tmpl.ecc_bytes_len = ecc_len;
    result.tmpl.version = 2;
    
    return result;
}

// ═══ verify_v2 (單模板) ═══
VerifyResult BioHashProcessor::verify_v2(const std::vector<float>& feature,
                                          const BioHashTemplate& tmpl) {
    VerifyResult result = {false, 0, {}};
    
    auto candidate_seeds = generate_candidate_seeds();
    
    for (uint64_t seed : candidate_seeds) {
        // 步驟 1-4: 投影 + 二值化 + 根據 indices 取位元
        auto matrix = generate_random_matrix(seed);
        auto biohash = biohash_projection(feature, matrix);
        auto all_bits = binarize(biohash);
        
        // B'_bytes
        uint8_t B_prime_bytes[BIOHASH_K_BYTES] = {0};
        for (int i = 0; i < BIOHASH_K; i++) {
            int idx = tmpl.indices[i];
            if (idx >= 0 && idx < BIOHASH_DIM) {
                int byte_pos = i / 8;
                int bit_pos = 7 - (i % 8);
                B_prime_bytes[byte_pos] |= (all_bits[idx] << bit_pos);
            }
        }
        
        // 步驟 6: C' = B' ⊕ δ
        uint8_t C_prime_data[BIOHASH_K_BYTES];
        for (int i = 0; i < BIOHASH_K_BYTES; i++) {
            C_prime_data[i] = B_prime_bytes[i] ^ tmpl.sketch[i];
        }
        // ECC 直接取 sketch 的後半段
        
        // 步驟 7: BCH 解碼 C' → K*
        // ... BCH decode C_prime_data with sketch ECC ...
        int nerr = 0;
        bool decoded = bch_decode_fuzzy(C_prime_data, tmpl.sketch, 
                                         tmpl.ecc_bytes_len, nerr);
        
        if (decoded && nerr >= 0) {
            // 步驟 8: 驗證 SHA-256(K*) == h
            std::vector<uint8_t> K_star(C_prime_data, 
                                         C_prime_data + BIOHASH_K_BYTES);
            auto hash = sha256(K_star);
            
            if (hash == tmpl.commitment) {
                result.matched = true;
                result.num_errors = nerr;
                result.recovered_key = K_star;
                return result;
            }
        }
    }
    
    return result;  // 未匹配
}
```

---

## 6. 安全性分析

### 6.1 攻擊者有什麼

模板是公開發送的，攻擊者可以拿到：

| 已知 | 能推斷什麼 |
|------|-----------|
| `indices` (128 個位置) | 知道「用哪些維度」，但不知道這些維度的值 |
| `sketch δ` = B ⊕ C | 每個可能的 B 都對應唯一的 C，無法分辨正確答案 |
| `commitment h` = SHA-256(K) | SHA-256 單向，推不回 K |
| `encrypted_payload` | 沒有 K 就解不開 |

### 6.2 攻擊場景

| 攻擊 | 可行性 | 原因 |
|------|--------|------|
| 直接從模板讀金鑰 | ❌ 不可能 | K 被 B 的 XOR 遮蔽，不以明文存在 |
| 暴力猜 K | ❌ 2^128 | 128-bit 金鑰空間 |
| 暴力猜 B | ❌ 2^128 | 128 bits 的二值序列 |
| 從 commitment 反推 K | ❌ | SHA-256 preimage resistance |
| 拿別人的臉嘗試 | ❌ | 錯誤率 > BCH_T(25)，解碼失敗 |
| 拿正確的臉解密 | ✅ 預期行為 | 這就是系統的設計目的 |

### 6.3 對比現行方案

```
現行 (v1):
  模板 = {indices, codeword}
  codeword 前 16B = 人臉位元明文
  安全性: 驗證身份 → ✅  衍生金鑰 → ❌ (明文洩漏)

Fuzzy Commitment (v2):
  模板 = {indices, sketch, commitment}
  sketch = 人臉位元 ⊕ 隨機碼字 (兩者都被隱藏)
  安全性: 驗證身份 → ✅  衍生金鑰 → ✅
```

> **附註**：即使不使用加密酬載功能，v2 方案的身份驗證也比 v1 更安全 — v1 的 codeword 直接洩漏了人臉位元本身（雖然是經過投射和二值化的，但它就是 BCH 會糾正到的目標值）。v2 的 sketch 中，人臉位元和隨機碼字互相遮蔽，兩者都不可見。

---

## 7. 使用場景

### 場景 A：透過 Web UI 註冊碼字（主要流程）

```
1. Alice 在手機 Web UI 上：
   - 上傳自己的人臉照片
   - 輸入個人資訊: {"name":"Alice", "company":"ABC科技"}
   - 選擇日期種子範圍: 2026-04-01 ~ 2026-05-01
   - 點擊「開始註冊」

2. RPi 儲存照片，建立 pending 記錄，等待裝置處理

3. CV181X 裝置處理 (Phase 2+)：
   - 讀取照片 → ArcFace → BioHash + Fuzzy Commitment
   - 生成隨機金鑰 K → BCH(K) → sketch = B ⊕ C
   - AES(K, 個人資訊) → encrypted_payload
   - 組裝碼字 hex = indices + sketch + commitment + payload
   - POST /api/persons/{id}/complete → 狀態變為 completed

4. Alice 取得碼字，複製分享給他人

5. 他人的攝影機看到 Alice → BioHash 驗證 → 恢復 K → 解密
   → OLED 顯示: ✅ Alice | ABC科技

6. 站在攝影機前的是路人甲 → 驗證失敗 → Unknown
   → 路人甲拿不到 K，看不到 Alice 的任何資訊
```

### 場景 B：裝置按鈕直接註冊

```
CV181X 長按按鈕 → 本地 ArcFace + BioHash + Fuzzy Commitment
→ POST /api/persons → 直接存入 RPi (狀態: completed)
→ 這個流程不經過 Web UI
```

### 場景 C：分級存取控制

```
管理員封包: 碼字 + AES(K, {"role":"admin", "zones":["A","B","C"]})
工程師封包: 碼字 + AES(K, {"role":"engineer", "zones":["A"]})

→ 攝影機辨識出管理員 → 解密出完整權限 → 開門 A+B+C
→ 攝影機辨識出工程師 → 解密出限定權限 → 只開門 A
→ 其他人 → 無法解密 → 無權限
```

### 場景 D：定時自銷訊息

```
封包: 碼字(seed=20260407) + AES(K, {"message":"今天的密碼是42"})

→ 4/7 當天：攝影機辨識成功 → 看到訊息
→ 4/8 之後：seed 20260407 不在候選清單 → 驗證失敗 → 訊息自動過期
```

---

## 8. 實作擴展規劃

### Phase 概覽

```
Phase 1 (已完成): RPi HTTP Server + 分頁式 Web UI
  - SQLite + FastAPI + 註冊/管理分頁
  - 照片上傳 + 日期種子選擇 + pending/completed 狀態
  - 移除加密酬載手動輸入（由裝置端自動產生）

Phase 2 (已完成): CV181X ↔ RPi HTTP 對接
  - cpp-httplib + remote_database
  - 裝置啟動時 GET /api/templates
  - 長按 POST 新模板到 RPi
  - 處理 pending 註冊

Phase 3 (✅ 已完成): Fuzzy Commitment v3 改造
  ├── 3a: 2048 維投影擴展 (512×512 → 2048×512)
  ├── 3b: 511-bit 全域 XOR (消除 ECC 明文洩漏漏洞)
  ├── 3c: BCH_T=42 (量化分析驗證)
  ├── 3d: 模板格式 v3 (sketch + commitment, version=0x03)
  ├── 3e: AES-128-CTR + HMAC-SHA256 加密酬載
  └── 3f: RPi API 支援 encrypted_payload

Phase 4 (未來): 進階功能
  ├── 4a: 外部工具 (手機端 enroll 工具，可獨立產生碼字封包)
  ├── 4b: 多重酬載 (一個人多個加密資料包)
  └── 4c: 酬載過期策略
```

### Phase 3 詳解：Fuzzy Commitment 改造

```
Step 1: biohash_processor.h
  - BioHashTemplate 新增 version, sketch, commitment, encrypted_payload
  - BioHashProcessor 新增 enroll_v2(), verify_v2()
  - 保留 enroll(), verify() 做 v1 向下兼容

Step 2: biohash_processor.cpp
  - 新增 sha256() 輔助函式 (可用 mbedtls 或手寫 minimal impl)
  - 新增 enroll_v2(): 隨機金鑰 + BCH encode + XOR sketch + commitment
  - 新增 verify_v2(): XOR 恢復 + BCH decode + hash 驗證 + 返回 key
  - to_hex() / from_hex() 支持 v2 格式

Step 3: face_database.cpp
  - JSON 格式新增 "version" 欄位
  - 解析時自動偵測 v1/v2

Step 4: btn_helpers.hpp
  - 長按時呼叫 enroll_v2() 取代 enroll()
  - 取得 key 但暫不加密（Phase 4 才做酬載）

Step 5: tdl_handler.cpp
  - verify_multiple 改呼叫 verify_v2()
  - 驗證成功時從 VerifyResult 取 recovered_key
```

### Phase 4 詳解：加密酬載

```
AES 實作選擇 (CV181X RISC-V musl):
  選項 A: tiny-AES-c (header-only, ~200 行 C，支持 AES-128/256-CBC)
  選項 B: mbedtls (更完整，支持 GCM，但較大)
  選項 C: 手寫 ChaCha20-Poly1305 (更適合無 AES 硬體的嵌入式)

建議: tiny-AES-c 做 AES-128-CBC + HMAC 或 mbedtls 做 AES-128-GCM

RPi 側:
  - persons 表新增 encrypted_payload TEXT 欄位
  - Web UI 新增「加密酬載」輸入區 (base64)
  - API 支持帶酬載的 create/update
```

---

## 9. 待決定事項

- [ ] **SHA-256 實作**：CV181X 上用 mbedtls、手寫 minimal、還是其他？
- [ ] **AES 實作**：tiny-AES-c (輕量) vs mbedtls (功能全)?
- [ ] **AES 模式**：AES-128-GCM (認證加密) vs AES-128-CBC + HMAC？
- [ ] **外部 enroll 工具**：手機端是否需要獨立工具來產生碼字封包？（若需要，技術選型？）
- [ ] **Phase 2 和 Phase 3 先後**：是先做 CV181X ↔ RPi 對接，還是先改造 Fuzzy Commitment？
- [ ] **OLED 顯示解密資訊**：128×64 像素是否夠用？還是只在 RTSP 上顯示？
