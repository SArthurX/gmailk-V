# Face Detection Application - Refactored

### 目錄結構

```
gmailk-V
├── main.cpp                    # 主程序入口（簡化版）
├── Makefile                    # 編譯配置
├── include/                    # 頭文件目錄
│   ├── shared_data.h          # 共享數據和全局變量
│   ├── system_init.h          # 系統初始化
│   ├── tdl_handler.h          # TDL（人臉檢測）處理
│   └── venc_handler.h         # 視頻編碼和 RTSP 推流
└── src/                       # 源文件目錄
    ├── shared_data.cpp        # 共享數據實現
    ├── system_init.cpp        # 系統初始化實現
    ├── tdl_handler.cpp        # TDL 處理實現
    └── venc_handler.cpp       # VENC 處理實現
```

### 模組說明

#### 1. **shared_data** (共享數據模組)
- **功能**: 管理線程間共享的全局數據
- **內容**:
  - `g_bExit`: 原子布爾變量，控制程序退出
  - `g_stFaceMeta`: 全局人臉檢測結果
  - `g_ResultMutex`: 保護共享數據的互斥鎖
- **API**:
  - `SharedData_Init()`: 初始化共享數據
  - `SharedData_Cleanup()`: 清理共享數據

#### 2. **system_init** (系統初始化模組)
- **功能**: 處理所有底層系統初始化（VI/VPSS/VENC/RTSP）
- **結構**:
  - `SystemConfig_t`: 封裝系統配置參數
- **API**:
  - `SystemInit_GetSensorConfig()`: 獲取傳感器配置
  - `SystemInit_SetupVBPool()`: 配置視頻緩衝池
  - `SystemInit_SetupVPSS()`: 配置 VPSS（視頻處理子系統）
  - `SystemInit_SetupVENC()`: 配置視頻編碼器
  - `SystemInit_SetupRTSP()`: 配置 RTSP 服務
  - `SystemInit_All()`: 一鍵完成所有初始化
  - `SystemInit_Cleanup()`: 清理系統資源

#### 3. **tdl_handler** (TDL 處理模組)
- **功能**: 封裝 TDL SDK 相關操作（人臉檢測）
- **結構**:
  - `TDLHandler_t`: TDL 處理器結構，包含 TDL handle 和 service handle
- **API**:
  - `TDLHandler_Init()`: 初始化 TDL，加載人臉檢測模型
  - `TDLHandler_DetectFace()`: 執行人臉檢測
  - `TDLHandler_DrawFaceRect()`: 在畫面上繪製人臉框
  - `TDLHandler_ThreadRoutine()`: TDL 線程主函數
  - `TDLHandler_Cleanup()`: 清理 TDL 資源

#### 4. **venc_handler** (視頻編碼處理模組)
- **功能**: 處理視頻編碼和 RTSP 推流
- **結構**:
  - `VENCHandler_t`: VENC 處理器參數結構
- **API**:
  - `VENCHandler_SendFrameRTSP()`: 發送畫面到 RTSP
  - `VENCHandler_ThreadRoutine()`: VENC 線程主函數

### 主程序流程 (main.cpp)

重構後的 `main.cpp` 非常簡潔：

```cpp
1. 解析命令行參數（模型路徑）
2. 設置信號處理
3. 初始化共享數據
4. 初始化系統（SystemInit_All）
5. 初始化 TDL Handler
6. 創建兩個線程：
   - TDL 線程：負責人臉檢測
   - VENC 線程：負責視頻編碼和推流
7. 等待線程結束
8. 清理所有資源
```

### 線程架構

```
主線程 (main)
├── TDL 線程 (TDLHandler_ThreadRoutine)
│   ├── 從 VPSS CHN1 取得畫面
│   ├── 執行人臉檢測
│   └── 更新全局人臉檢測結果 (加鎖)
│
└── VENC 線程 (VENCHandler_ThreadRoutine)
    ├── 從 VPSS CHN0 取得畫面
    ├── 讀取全局人臉檢測結果 (加鎖)
    ├── 在畫面上繪製人臉框
    └── 推送到 RTSP
```

### C/C++ 混合編程處理

專案中使用了大量 C 函式庫，在各模組中妥善使用 `extern "C"` 包裝：

```cpp
// 在頭文件中
extern "C" {
#include <cvi_comm.h>
#include "cvi_tdl.h"
}

// 在源文件中
extern "C" {
#include <sample_comm.h>
#include "middleware_utils.h"
}
```

### 編譯

```bash
# 設置環境變量（根據您的芯片型號）
source envsetup.sh

# 編譯
make clean
make
```

### 運行

```bash
./main /path/to/scrfdface_model.cvimodel
```

### 後續擴展（人臉對比）

重構後的架構為人臉對比功能預留了清晰的擴展路徑：

1. **新增特徵提取模組** (`face_feature_extractor.h/cpp`)
   - 加載人臉識別模型
   - 提取人臉特徵向量

2. **新增人臉數據庫模組** (`face_database.h/cpp`)
   - 管理已註冊的人臉特徵
   - 提供人臉特徵比對功能

3. **修改 TDL Handler**
   - 在檢測到人臉後，調用特徵提取
   - 與數據庫中的特徵進行比對

4. **修改 VENC Handler**
   - 在畫面上標註人臉身份信息


### 注意事項

- 所有 C 函式庫的頭文件必須用 `extern "C"` 包裝
- 線程間共享數據訪問必須加鎖
- 資源清理順序：TDL → System → SharedData
- 編譯時需要設置正確的 include 路徑（Makefile 已配置）
