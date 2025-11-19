### 專案概述

**gmailk-V** 是一個針對 CVITEK CV181X/CV180X RISC-V 嵌入式平台設計的高效能人臉檢測應用程式。具備即時人臉檢測、RTSP 串流以及為嵌入式系統優化的模組化架構。

### 功能特色

- 🎯 **即時人臉檢測** - 基於 CVITEK TDL SDK
- 📹 **RTSP 串流** - H.264 視訊編碼與即時人臉框疊加
- 🔧 **模組化架構** - 清晰的關注點分離，易於維護
- 🚀 **多執行緒設計** - 檢測與編碼採用獨立執行緒
- 🔌 **OpenCV 與 NCNN 整合** - 支援自訂圖像處理與神經網路
- 💻 **RISC-V 優化** - 專為 CVITEK CV181X/CV180X 平台打造

### 系統需求

- **硬體**: CVITEK CV181X 或 CV180X SoC
- **工具鏈**: RISC-V GCC 交叉編譯器 (musl)
- **相依套件**: 
  - CVITEK TDL SDK
  - CVITEK Media SDK
  - OpenCV
  - NCNN

### 專案結構

```
gmailk-V/
├── CMakeLists.txt          # CMake 設定檔
├── build.sh                # 編譯腳本
├── include/                # 標頭檔
│   ├── shared_data.h       # 共享資料結構
│   ├── system_init.h       # 系統初始化
│   ├── tdl_handler.h       # TDL 人臉檢測處理器
│   ├── venc_handler.h      # 視訊編碼處理器
│   └── button_handler.h    # 按鈕輸入處理器
├── src/                    # 原始碼檔案
│   ├── main.cpp            # 主程式入口
│   ├── shared_data.cpp
│   ├── system_init.cpp
│   ├── tdl_handler.cpp
│   ├── venc_handler.cpp
│   └── button_handler.cpp
├── common/                 # 共用工具
├── lib/                    # 第三方函式庫
│   ├── opencv/             # OpenCV 函式庫
│   ├── ncnn/               # NCNN 函式庫
│   ├── system/             # 系統函式庫
│   └── tdl/                # TDL 函式庫
├── models/                 # 人臉檢測模型
└── tools/                  # 編譯工具與腳本
    ├── build_opencv.sh
    └── build_ncnn.sh
```

### 編譯專案

#### 步驟 1: 編譯第三方函式庫（僅首次需要）

```bash
cd tools

# 編譯 OpenCV
./build_opencv.sh

# 編譯 NCNN
./build_ncnn.sh
```

#### 步驟 2: 編譯主程式

```bash
# 為 CV181X 配置（預設）
./build.sh

# 或為 CV180X 配置
./build.sh -c CV180X

# 清理編譯
./build.sh -c
```

編譯後的執行檔位於：`build/main`

### 執行應用程式

```bash
# 基本用法
./build/main /path/to/face_detection_model.cvimodel

# 範例
./build/main models/scrfd_det_face_432_768_INT8_cv181x.cvimodel
```

### RTSP 串流

啟動應用程式後，您可以透過以下方式觀看帶有人臉檢測框的視訊串流：

```bash
# 使用 VLC
vlc rtsp://<設備IP>:554/h264

# 使用 ffplay
ffplay rtsp://<設備IP>:554/h264
```

### 模組概覽

#### 1. **shared_data** - 共享資料模組
管理檢測與編碼執行緒間的執行緒安全共享資料。

**核心組件:**
- `g_bExit` - 用於優雅關閉的原子旗標
- `g_stFaceMeta` - 全域人臉檢測結果
- `g_ResultMutex` - 執行緒同步互斥鎖

#### 2. **system_init** - 系統初始化模組
處理底層系統初始化（VI/VPSS/VENC/RTSP）。

**核心函式:**
- `SystemInit_All()` - 一鍵完成初始化
- `SystemInit_Cleanup()` - 資源清理

#### 3. **tdl_handler** - TDL 檢測模組
封裝 CVITEK TDL SDK 進行人臉檢測。

**核心函式:**
- `TDLHandler_Init()` - 初始化 TDL 並載入模型
- `TDLHandler_DetectFace()` - 執行人臉檢測
- `TDLHandler_ThreadRoutine()` - 檢測執行緒主迴圈

#### 4. **venc_handler** - 視訊編碼模組
處理 H.264 編碼與 RTSP 串流。

**核心函式:**
- `VENCHandler_SendFrameRTSP()` - 發送畫面至 RTSP
- `VENCHandler_ThreadRoutine()` - 編碼執行緒主迴圈

### 執行緒架構

```
主執行緒
├── TDL 執行緒（人臉檢測）
│   ├── 從 VPSS CHN1 取得畫面
│   ├── 執行人臉檢測
│   └── 更新全域人臉資料（加鎖）
│
└── VENC 執行緒（視訊編碼）
    ├── 從 VPSS CHN0 取得畫面
    ├── 讀取人臉資料（加鎖）
    ├── 繪製人臉矩形框
    └── 發送至 RTSP 串流
```

### 配置設定

編輯 `config.json` 進行自訂設定：

```json
{
  "chip": "CV181X",
  "video": {
    "width": 1920,
    "height": 1080,
    "fps": 30
  },
  "detection": {
    "threshold": 0.5,
    "model": "models/scrfd_det_face_432_768_INT8_cv181x.cvimodel"
  }
}
```



**找不到 OpenCV/NCNN 函式庫：**
- 請確保先使用 `tools/` 目錄下的腳本編譯函式庫。

**無法存取 RTSP 串流：**
- 檢查防火牆設定
- 驗證設備 IP 位址
- 確保埠口 554 未被封鎖

### 貢獻

歡迎貢獻！請隨時提交 issue 或 pull request。

### 授權

詳見 [LICENSE](LICENSE) 檔案。
