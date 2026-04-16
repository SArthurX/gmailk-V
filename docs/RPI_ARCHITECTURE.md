# RPi 遠端模板儲存架構

> **建立日期**：2026-04-07  
> **狀態**：Phase 2 完成 ✅

---

## 1. 概述

將 BioHash 模板儲存從 CV181X 本地 JSON 搬移到 Raspberry Pi，透過 CDC-NCM + HTTP API 進行資料交換。RPi 同時提供手機可存取的 Web UI，支援**註冊**（上傳照片產生碼字）和**管理**（查看/編輯/刪除/分享碼字）。

### 核心原則

| 原則 | 說明 |
|------|------|
| **特徵不離裝置** | 原始 512-dim ArcFace 特徵向量永遠不經網路傳輸 |
| **RPi 不碰加密** | RPi 只存 opaque hex blob + 照片，不理解模板內容 |
| **BioHash 全在 CV181X** | 投影、二值化、BCH 編碼/解碼、Fuzzy Commitment 全部在裝置端完成 |
| **加密酬載不可直接寫入** | 酬載是裝置端用人臉衍生金鑰加密的，只有驗證時才能解密 |
| **假設始終連線** | CDC-NCM 固定 IP，不實作離線快取 |

---

## 2. 系統架構

```
手機 📱 ──Wi-Fi AP──> RPi 🍓 (wlan0)
                       │
                       ├── FastAPI HTTP Server (0.0.0.0:3000)
                       ├── SQLite (gvw.db)
                       ├── uploads/ (註冊照片存放)
                       └── 靜態 Web UI (index.html)
                       │
CV181X 📷 ──CDC-NCM──> RPi 🍓 (usb0: 192.168.42.1)
```

---

## 3. 流程設計

### 3.1 註冊流程 (Enroll)

```
使用者 (手機 Web UI)                RPi                         CV181X
      │                             │                             │
      ├── 上傳照片 + 資訊 ──────────>│                             │
      │   + 日期種子範圍             │                             │
      │                             ├── 儲存照片                   │
      │                             ├── 建立 pending 記錄          │
      │                             │                             │
      │                             ├── (Phase 2) 轉發照片 ──────>│
      │                             │                    ArcFace + │
      │                             │                    BioHash + │
      │                             │            Fuzzy Commitment  │
      │                             │<───── 回傳碼字 hex ──────────│
      │                             │                             │
      │                             ├── 更新狀態 = completed       │
      │                             ├── 存入碼字                   │
      │<──── 回傳碼字 ──────────────│                             │
      │                             │                             │
      ├── 複製碼字分享給他人         │                             │
```

### 3.2 驗證流程 (Verify)

```
碼字已存在 RPi 資料庫中

CV181X 攝影機看到某人:
  1. GET /api/templates → 取得所有已完成的碼字
  2. ArcFace 特徵提取 → BioHash 投影 → 二值化
  3. 對每個碼字: XOR sketch → BCH decode → 嘗試恢復金鑰 K
  4. SHA-256(K) == commitment ✓ → 身份確認
  5. AES(K, encrypted_payload) → 解密出資訊
  6. OLED / RTSP 顯示: ✅ Alice | ABC科技
```

### 3.3 裝置按鈕註冊 (Device Direct)

```
CV181X 長按按鈕 → 本地 BioHash → POST /api/persons → 直接存入 (completed)
```

---

## 4. 技術選型

| 組件 | 選擇 | 理由 |
|------|------|------|
| RPi 語言 | Python | 開發快、生態好、RPi Zero 2W 純前後端還行 |
| Web 框架 | FastAPI | async、自動文件、輕量 |
| 資料庫 | SQLite + aiosqlite | 零部署、無需額外服務、512MB RAM 友好 |
| 套件管理 | uv | 快速、現代化的 Python 套件管理 |
| CV181X HTTP | cpp-httplib (header-only) | 零依賴、適合嵌入式 |
| 前端 | 單頁 HTML (inline) | 手機友好、分頁式 UI |
| RPi 部署 | 直接在 RPi 上執行 | `uv run python main.py` |

---

## 5. API 設計

```
GET    /                              # Web UI (分頁: 註冊 / 管理)
POST   /api/enroll                    # 註冊: 上傳照片 + 資訊 + 日期種子
GET    /api/persons                   # 列出所有人員 (含 pending + completed)
GET    /api/persons/{id}              # 取得單一人員
PUT    /api/persons/{id}              # 更新名稱/描述
DELETE /api/persons/{id}              # 刪除 (含清理照片)
POST   /api/persons/{id}/complete     # 裝置回傳: 填入碼字完成註冊
POST   /api/persons                   # CV181X 裝置端: 直接寫入完成的碼字
GET    /api/templates                 # CV181X 專用: 只取已完成的碼字
GET    /api/pending                   # CV181X 專用: 只取 pending 照片資訊（輕量）
GET    /api/status                    # 系統狀態
GET    /uploads/{filename}            # 取得上傳的照片
```

---

## 6. 資料庫 Schema

```sql
CREATE TABLE persons (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    name              TEXT NOT NULL,
    description       TEXT DEFAULT '',
    photo_path        TEXT DEFAULT '',         -- 註冊照片檔名
    seed_start        TEXT DEFAULT '',         -- 日期種子起始 (YYYY-MM-DD)
    seed_end          TEXT DEFAULT '',         -- 日期種子結束 (YYYY-MM-DD)
    status            TEXT DEFAULT 'pending',  -- pending / completed
    biohash_template  TEXT DEFAULT '',         -- 碼字 hex (裝置處理後填入)
    encrypted_payload TEXT DEFAULT '',         -- 加密酬載 (裝置處理後填入)
    created_at        TEXT DEFAULT (datetime('now')),
    updated_at        TEXT DEFAULT (datetime('now'))
);
```

**重要**：`encrypted_payload` 不可由前端直接寫入。它是裝置端在 BioHash + Fuzzy Commitment 流程中，用人臉衍生金鑰加密使用者資訊後產生的。只有驗證時（正確的臉 + BCH 糾錯恢復金鑰）才能解密。

---

## 7. 網路配置

```
CV181X (192.168.42.2) ──CDC-NCM──> RPi (usb0: 192.168.42.1)
手機                  ──Wi-Fi AP──> RPi (wlan0: AP mode)

RPi FastAPI: 0.0.0.0:3000
CV181X 存取: http://192.168.42.1:3000
手機存取:    http://<RPi-wlan0-ip>:3000
```

---

## 8. 實作進度

### Phase 1：RPi HTTP Server + Web UI ✅
- [x] 建立 FastAPI 專案 (uv + aiosqlite)
- [x] 實作 CRUD API + 自動建表
- [x] 新增 `/api/status` 端點
- [x] 分頁式 Web UI：註冊 (照片上傳 + 資訊 + 日期種子) / 管理 (人員列表)
- [x] 照片上傳 + 靜態存取
- [x] 註冊/驗證流程分離，移除加密酬載手動輸入
- [x] 新增 `/api/enroll` + `/api/persons/{id}/complete` + `/api/templates`

### Phase 2：CV181X HTTP Client（基礎完成）
- [x] 引入 cpp-httplib.h (header-only, `src/3rdparty/httplib/`)
- [x] 新增 remote_database.cpp/h (HTTP client 封裝)
- [x] 修改 main.cpp 初始化 (`--rpi <url>` 參數)
- [x] 修改 btn_helpers.hpp 註冊流程 (遠端優先 + 本地 fallback)
- [x] 修改驗證流程：啟動時 GET /api/templates (帶快取)
- [x] 裝置端處理 pending 註冊：任務佇列架構（背景執行緒下載照片 → TDL Thread 處理 → 背景執行緒回傳碼字）
- [x] 使用 `CVI_TDL_ReadImage` + `imgprocess_t` API 讀取靜態照片（不依賴 VPSS 綁定狀態）
- [x] `extractFeature` 格式感知：自動處理 NV21（攝影機）和 RGB_888_PLANAR（照片）
- [x] 新增 RPi `GET /api/pending` 輕量端點
- [x] 新增 `include/tdl/cvi_tdl_media.h` 標頭檔

### Phase 3：面部衍生金鑰 + 加密酬載（設計中）
- [ ] 參見 `docs/FACE_DERIVED_KEY_CONCEPT.md`

---

## 9. 檔案索引

| 檔案 | 說明 |
|------|------|
| `gmailk-VVeb/py/main.py` | RPi FastAPI HTTP Server |
| `gmailk-VVeb/py/pyproject.toml` | Python 依賴 (uv) |
| `gmailk-VVeb/py/uploads/` | 註冊照片存放目錄 |
| `gmailk-VVeb/index.html` | Web UI 前端 (分頁式) |
| `gmailk-VVeb/src/main.rs` | 舊版 Rust Server (已棄用) |
| `docs/RPI_ARCHITECTURE.md` | 本文件 |
| `docs/FACE_DERIVED_KEY_CONCEPT.md` | 面部衍生金鑰 + Fuzzy Commitment 設計 |
| `docs/PROJECT_STATUS.md` | 專案全景狀態文件 |
