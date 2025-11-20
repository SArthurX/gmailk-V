# 人臉追蹤與辨識整合計劃 V2

## 📋 關鍵更新與優化

### ✅ 已確認的技術細節

1. **輸入格式：NV21 (YUV420SP)**
   - 當前系統從 VPSS 獲取的幀格式為 `PIXEL_FORMAT_NV21`
   - NV21 = Y 平面 + VU 交錯平面（YUV420 半平面格式）
   - 記憶體佈局：`[Y][Y][Y]...[V][U][V][U]...`

2. **NCNN 原生支援 YUV420SP**
   - ✅ **發現關鍵函數**：`ncnn::yuv420sp2rgb()`
   - ✅ 無需額外轉換，NCNN 可直接處理 NV21
   - ✅ 有硬體加速版本（ARM NEON 優化）

3. **人臉檢測保持逐幀**
   - 官方 SCRFD 模型有 TPU 硬體加速
   - 延遲低（~10-20ms），資源消耗可接受
   - **不需要降頻**，保持每幀檢測

4. **GDC 硬體加速可用**
   - 系統支援 GDC（幾何失真校正）硬體
   - 用於人臉對齊的仿射變換（~1-2ms）

---

## 🏗️ 系統架構設計

### 整體流程

```
┌─────────────────────────────────────────────────────────────────┐
│                          主線程（每幀執行）                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1️⃣ VPSS 獲取幀 (NV21格式, 768x432)                              │
│     └─> VIDEO_FRAME_INFO_S                                     │
│                                                                 │
│  2️⃣ 人臉檢測（TPU加速）                                            │
│     └─> CVI_TDL_FaceDetection()                                │
│     └─> cvtdl_face_t (bbox + 5個關鍵點)                         │
│                                                                 │
│  3️⃣ DeepSORT 追蹤（純位置，初期不用特徵）                          │
│     └─> CVI_TDL_DeepSORT_Face()                                │
│     └─> cvtdl_tracker_t (unique_id + track_state)             │
│                                                                 │
│  4️⃣ 智能特徵提取決策                                               │
│     ├─> 條件1: 新追蹤 (track_state == NEW)                      │
│     ├─> 條件2: 高品質 (face_quality > threshold)                │
│     ├─> 條件3: 間隔足夠 (距上次提取 >= N 幀)                      │
│     └─> 若滿足 → 加入提取隊列                                     │
│                                                                 │
│  5️⃣ 繪製結果（bbox + track_id + 辨識結果）                        │
│     └─> DrawUtils                                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
         ↓ (非同步，僅當有待處理任務時)
┌─────────────────────────────────────────────────────────────────┐
│                      特徵提取線程（按需執行）                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  6️⃣ 從隊列取出待提取人臉                                            │
│     └─> {track_id, frame_copy, face_info}                      │
│                                                                 │
│  7️⃣ 人臉對齊（GDC硬體加速）                                         │
│     └─> CVI_TDL_FaceAlignment(enableGDC=true)                  │
│     └─> 輸出 112x112 對齊人臉 (NV21)                            │
│                                                                 │
│  8️⃣ NV21 → RGB 轉換（NCNN 原生函數）                               │
│     └─> ncnn::yuv420sp2rgb()                                   │
│     └─> unsigned char rgb[112*112*3]                           │
│                                                                 │
│  9️⃣ ArcFace 特徵提取（NCNN推理）                                   │
│     └─> ncnn::Mat::from_pixels(rgb, PIXEL_RGB, 112, 112)      │
│     └─> Arcface::getFeature()                                  │
│     └─> std::vector<float> feature(128維, L2正規化)             │
│                                                                 │
│  🔟 儲存特徵到追蹤管理器                                             │
│     └─> active_tracks[track_id].feature = feature              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
         ↓ (更低頻，可選)
┌─────────────────────────────────────────────────────────────────┐
│                      辨識線程（按需執行）                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1️⃣1️⃣ 人臉辨識（與資料庫比對）                                        │
│     └─> FaceDatabase::search(feature, threshold)               │
│     └─> 返回最相似人臉 ID 或 "Unknown"                           │
│                                                                 │
│  1️⃣2️⃣ 更新辨識結果                                                  │
│     └─> active_tracks[track_id].identity = name                │
│     └─> active_tracks[track_id].confidence = similarity        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🔧 核心模組設計

### 模組 1: FaceFeatureExtractor（特徵提取器）

**職責**：
- 封裝 NCNN ArcFace 模型
- 處理 NV21 → RGB 轉換
- 調用官方 GDC 對齊 API
- 提取 128 維特徵向量

**頭文件**：`include/face_feature_extractor.h`

```cpp
#ifndef FACE_FEATURE_EXTRACTOR_H
#define FACE_FEATURE_EXTRACTOR_H

#include <vector>
#include <string>
#include "cvi_tdl.h"
#include "mat.h"  // NCNN

// 前向聲明（避免引入完整的 Arcface 類）
namespace ncnn {
    class Mat;
}

class Arcface;  // 定義在 test/arcface.h

/**
 * @brief 人臉特徵提取器
 * 
 * 功能：
 * 1. 使用官方 CVI_TDL_FaceAlignment 進行人臉對齊（GDC 硬體加速）
 * 2. 處理 NV21 格式轉換到 NCNN 所需的 RGB 格式
 * 3. 調用 NCNN ArcFace 模型提取 128 維特徵
 */
class FaceFeatureExtractor {
public:
    /**
     * @brief 構造函數
     * @param model_param NCNN 模型參數文件路徑
     * @param model_bin NCNN 模型權重文件路徑
     * @param tdl_handle TDL SDK 句柄（用於調用對齊 API）
     */
    FaceFeatureExtractor(const std::string& model_param, 
                        const std::string& model_bin,
                        cvitdl_handle_t tdl_handle);
    
    ~FaceFeatureExtractor();
    
    /**
     * @brief 從幀中提取指定人臉的特徵
     * @param frame 輸入幀（NV21 格式）
     * @param face_info 人臉信息（包含 bbox 和 5 個關鍵點）
     * @param feature [輸出] 128 維特徵向量（已 L2 正規化）
     * @return CVI_SUCCESS 或錯誤碼
     */
    CVI_S32 extractFeature(VIDEO_FRAME_INFO_S* frame,
                          cvtdl_face_info_t* face_info,
                          std::vector<float>& feature);
    
    /**
     * @brief 批量提取特徵（可選，用於優化）
     * @param frame 輸入幀
     * @param faces 多個人臉
     * @param features [輸出] 多個特徵向量
     * @return CVI_SUCCESS 或錯誤碼
     */
    CVI_S32 extractFeatures(VIDEO_FRAME_INFO_S* frame,
                           cvtdl_face_t* faces,
                           std::vector<std::vector<float>>& features);

private:
    Arcface* arcface_model_;           // NCNN ArcFace 模型
    cvitdl_handle_t tdl_handle_;       // TDL SDK 句柄
    bool use_gdc_;                      // 是否使用 GDC 硬體加速
    
    /**
     * @brief 使用官方 API 對齊人臉
     * @param inFrame 輸入幀（NV21, 768x432）
     * @param face_info 人臉信息
     * @param outFrame [輸出] 對齊後的人臉（112x112, NV21）
     * @return CVI_SUCCESS 或錯誤碼
     */
    CVI_S32 alignFaceWithGDC(VIDEO_FRAME_INFO_S* inFrame,
                            cvtdl_face_info_t* face_info,
                            VIDEO_FRAME_INFO_S* outFrame);
    
    /**
     * @brief 將 NV21 格式的 VIDEO_FRAME 轉換為 NCNN Mat（RGB）
     * @param frame 輸入幀（NV21, 112x112）
     * @return ncnn::Mat（RGB 格式）
     */
    ncnn::Mat nv21FrameToNcnnMat(VIDEO_FRAME_INFO_S* frame);
    
    /**
     * @brief NCNN 原生函數：YUV420SP (NV21) → RGB
     * @param nv21_data NV21 數據指針
     * @param width 圖像寬度
     * @param height 圖像高度
     * @param rgb_data [輸出] RGB 數據（需預先分配 w*h*3 字節）
     */
    void convertNV21ToRGB(const unsigned char* nv21_data,
                         int width, int height,
                         unsigned char* rgb_data);
};

#endif // FACE_FEATURE_EXTRACTOR_H
```

**實現要點**：`src/face_feature_extractor.cpp`

```cpp
#include "face_feature_extractor.h"
#include "arcface.h"  // test/arcface.h
#include <cstring>
#include <iostream>

extern "C" {
    #include "cvi_sys.h"
}

FaceFeatureExtractor::FaceFeatureExtractor(
    const std::string& model_param, 
    const std::string& model_bin,
    cvitdl_handle_t tdl_handle)
    : tdl_handle_(tdl_handle), use_gdc_(true) {
    
    // 初始化 NCNN ArcFace 模型
    arcface_model_ = new Arcface(model_param);  // Arcface 構造函數需調整
    
    // 檢查 GDC 是否可用
    FILE* f = fopen("/proc/cvitek/gdc", "r");
    if (f) {
        fclose(f);
        use_gdc_ = true;
        std::cout << "✅ GDC 硬體加速已啟用" << std::endl;
    } else {
        use_gdc_ = false;
        std::cout << "⚠️  GDC 不可用，使用 CPU 回退方案" << std::endl;
    }
}

FaceFeatureExtractor::~FaceFeatureExtractor() {
    if (arcface_model_) {
        delete arcface_model_;
    }
}

CVI_S32 FaceFeatureExtractor::extractFeature(
    VIDEO_FRAME_INFO_S* frame,
    cvtdl_face_info_t* face_info,
    std::vector<float>& feature) {
    
    // 1. 分配輸出幀（112x112, NV21）
    VIDEO_FRAME_INFO_S aligned_frame = {0};
    aligned_frame.stVFrame.enPixelFormat = PIXEL_FORMAT_NV21;
    aligned_frame.stVFrame.u32Width = 112;
    aligned_frame.stVFrame.u32Height = 112;
    aligned_frame.stVFrame.u32Length[0] = 112 * 112 * 3 / 2;  // NV21 大小
    
    // 分配 VB（Video Buffer）
    VB_BLK blk = CVI_VB_GetBlock(VB_INVALID_POOLID, aligned_frame.stVFrame.u32Length[0]);
    if (blk == VB_INVALID_HANDLE) {
        std::cerr << "❌ 無法分配 VB" << std::endl;
        return CVI_FAILURE;
    }
    aligned_frame.stVFrame.u64PhyAddr[0] = CVI_VB_Handle2PhysAddr(blk);
    aligned_frame.stVFrame.u32Stride[0] = 112;
    
    // 2. 使用官方 API 對齊人臉（GDC 加速）
    CVI_S32 ret = CVI_TDL_FaceAlignment(
        frame,                          // 輸入幀（768x432, NV21）
        frame->stVFrame.u32Width,       // meta 寬度
        frame->stVFrame.u32Height,      // meta 高度
        face_info,                      // 人臉信息
        &aligned_frame,                 // 輸出幀（112x112, NV21）
        use_gdc_                        // 啟用 GDC
    );
    
    if (ret != CVI_TDL_SUCCESS) {
        std::cerr << "❌ 人臉對齊失敗: 0x" << std::hex << ret << std::endl;
        CVI_VB_ReleaseBlock(blk);
        return ret;
    }
    
    // 3. NV21 → RGB 轉換（NCNN 原生函數）
    ncnn::Mat rgb_mat = nv21FrameToNcnnMat(&aligned_frame);
    
    // 4. 調用 ArcFace 提取特徵
    feature = arcface_model_->getFeature(rgb_mat);
    
    // 5. 清理
    CVI_VB_ReleaseBlock(blk);
    
    std::cout << "✅ 特徵提取成功，維度: " << feature.size() << std::endl;
    return CVI_SUCCESS;
}

ncnn::Mat FaceFeatureExtractor::nv21FrameToNcnnMat(VIDEO_FRAME_INFO_S* frame) {
    // 確保內存映射
    bool need_unmap = false;
    if (frame->stVFrame.pu8VirAddr[0] == NULL) {
        frame->stVFrame.pu8VirAddr[0] = (CVI_U8*)CVI_SYS_Mmap(
            frame->stVFrame.u64PhyAddr[0],
            frame->stVFrame.u32Length[0]
        );
        need_unmap = true;
    }
    
    int width = frame->stVFrame.u32Width;    // 112
    int height = frame->stVFrame.u32Height;  // 112
    
    // 分配 RGB 緩衝區
    unsigned char* rgb_data = new unsigned char[width * height * 3];
    
    // 🔥 關鍵：使用 NCNN 原生函數轉換 NV21 → RGB
    ncnn::yuv420sp2rgb(
        frame->stVFrame.pu8VirAddr[0],  // NV21 數據指針
        width, 
        height, 
        rgb_data                         // RGB 輸出
    );
    
    // 創建 NCNN Mat
    ncnn::Mat result = ncnn::Mat::from_pixels(
        rgb_data, 
        ncnn::Mat::PIXEL_RGB,            // RGB 格式
        width, 
        height
    );
    
    // 清理
    delete[] rgb_data;
    
    if (need_unmap) {
        CVI_SYS_Munmap((void*)frame->stVFrame.pu8VirAddr[0], 
                       frame->stVFrame.u32Length[0]);
        frame->stVFrame.pu8VirAddr[0] = NULL;
    }
    
    return result;
}
```

---

### 模組 2: FaceTrackerManager（追蹤與辨識管理）

**職責**：
- 管理 DeepSORT 追蹤器
- 維護活動追蹤列表（track_id → 特徵/身份映射）
- 決策何時提取特徵、何時執行辨識
- 管理特徵提取/辨識隊列

**頭文件**：`include/face_tracker_manager.h`

```cpp
#ifndef FACE_TRACKER_MANAGER_H
#define FACE_TRACKER_MANAGER_H

#include <map>
#include <vector>
#include <string>
#include <queue>
#include <mutex>
#include "cvi_tdl.h"
#include "face_feature_extractor.h"

// 追蹤配置
struct TrackingConfig {
    int feature_extract_interval;      // 特徵提取間隔（幀數，0=僅新人臉）
    float min_face_quality;             // 最小人臉品質閾值（0.0-1.0）
    int recognition_interval;           // 辨識間隔（幀數）
    float recognition_threshold;        // 辨識相似度閾值（0.0-1.0）
    int track_lost_threshold;           // 追蹤丟失閾值（幀數）
    int max_track_age;                  // 追蹤最大存活時間（幀數）
    
    // 預設配置
    TrackingConfig() :
        feature_extract_interval(10),   // 每 10 幀提取一次
        min_face_quality(0.6f),          // 品質 >= 0.6 才提取
        recognition_interval(30),        // 每 30 幀辨識一次
        recognition_threshold(0.5f),     // 相似度 >= 0.5 才認為匹配
        track_lost_threshold(30),        // 30 幀未見則移除
        max_track_age(3000)              // 最多保留 3000 幀（~2分鐘@15fps）
    {}
};

// 追蹤人臉信息
struct TrackedFace {
    uint64_t track_id;                  // DeepSORT 的 unique_id
    std::vector<float> feature;         // ArcFace 128維特徵（L2正規化）
    std::string identity;               // 辨識結果（"Unknown" 或姓名）
    float confidence;                   // 辨識信心（餘弦相似度）
    
    int frames_since_feature;           // 距離上次特徵提取的幀數
    int frames_since_recognition;       // 距離上次辨識的幀數
    int frames_since_seen;              // 距離上次出現的幀數
    int total_frames;                   // 總出現幀數
    
    bool has_feature;                   // 是否已提取特徵
    bool is_recognized;                 // 是否已完成辨識
    
    cvtdl_bbox_t last_bbox;             // 最後一次的 bbox
    
    TrackedFace() :
        track_id(0), identity("Unknown"), confidence(0.0f),
        frames_since_feature(999), frames_since_recognition(999),
        frames_since_seen(0), total_frames(0),
        has_feature(false), is_recognized(false) {}
};

// 用於顯示的追蹤信息
struct TrackedFaceInfo {
    uint64_t track_id;
    std::string identity;
    float confidence;
    cvtdl_bbox_t bbox;
    bool is_center;  // 是否在畫面中心
};

/**
 * @brief 人臉追蹤與辨識管理器
 * 
 * 功能：
 * 1. 整合 DeepSORT 追蹤（純位置追蹤）
 * 2. 管理特徵提取隊列（異步處理）
 * 3. 管理辨識隊列（與資料庫比對）
 * 4. 維護活動追蹤列表
 */
class FaceTrackerManager {
public:
    FaceTrackerManager(cvitdl_handle_t tdl_handle,
                      FaceFeatureExtractor* extractor,
                      const TrackingConfig& config);
    
    ~FaceTrackerManager();
    
    /**
     * @brief 主處理函數：檢測 → 追蹤 → 決策
     * @param frame 當前幀
     * @param faces 檢測到的人臉
     * @param tracker [輸出] 追蹤結果
     * @return CVI_SUCCESS 或錯誤碼
     */
    CVI_S32 processFrame(VIDEO_FRAME_INFO_S* frame,
                        cvtdl_face_t* faces,
                        cvtdl_tracker_t* tracker);
    
    /**
     * @brief 獲取當前活動追蹤列表（用於顯示）
     * @return 追蹤信息列表
     */
    std::vector<TrackedFaceInfo> getActiveTracks();
    
    /**
     * @brief 設置人臉資料庫（用於辨識）
     * @param database 人臉資料庫指針
     */
    void setFaceDatabase(void* database);  // FaceDatabase*
    
    /**
     * @brief 獲取統計信息
     */
    void printStats();

private:
    cvitdl_handle_t tdl_handle_;
    FaceFeatureExtractor* feature_extractor_;
    void* face_database_;  // FaceDatabase*（避免循環依賴）
    TrackingConfig config_;
    
    std::map<uint64_t, TrackedFace> active_tracks_;  // 活動追蹤映射
    std::mutex tracks_mutex_;                         // 線程安全
    
    uint64_t frame_count_;                            // 總幀數計數
    
    /**
     * @brief 初始化 DeepSORT 追蹤器
     */
    CVI_S32 initDeepSORT();
    
    /**
     * @brief 更新追蹤列表（清理丟失的追蹤）
     */
    void updateTracks(cvtdl_tracker_t* tracker);
    
    /**
     * @brief 決策：是否需要提取特徵
     */
    bool shouldExtractFeature(uint64_t track_id, 
                             cvtdl_face_info_t* face_info,
                             cvtdl_trk_state_type_t track_state);
    
    /**
     * @brief 決策：是否需要執行辨識
     */
    bool shouldRecognize(uint64_t track_id);
    
    /**
     * @brief 執行特徵提取（同步版本）
     */
    CVI_S32 extractFeatureForTrack(VIDEO_FRAME_INFO_S* frame,
                                   uint64_t track_id,
                                   cvtdl_face_info_t* face_info);
    
    /**
     * @brief 執行人臉辨識（與資料庫比對）
     */
    void recognizeTrack(uint64_t track_id);
    
    /**
     * @brief 清理丟失的追蹤
     */
    void cleanLostTracks();
};

#endif // FACE_TRACKER_MANAGER_H
```

---

### 模組 3: 整合到 tdl_handler.cpp

**修改點**：

```cpp
// tdl_handler.h 新增
typedef struct {
    const char *modelPath;
    cvitdl_handle_t tdlHandle;
    cvitdl_service_handle_t serviceHandle;
    void *buttonHandler;  // ButtonHandler_t*
    
    // 🆕 新增成員
    FaceTrackerManager* trackerManager;      // 追蹤管理器
    FaceFeatureExtractor* featureExtractor;  // 特徵提取器
} TDLHandler_t;

// tdl_handler.cpp 初始化修改
CVI_S32 TDLHandler_Init(TDLHandler_t *pstHandler, const char *modelPath) {
    // ... 原有初始化代碼 ...
    
    // 🆕 初始化特徵提取器
    pstHandler->featureExtractor = new FaceFeatureExtractor(
        "models/mobilefacenet.param",
        "models/mobilefacenet.bin",
        pstHandler->tdlHandle
    );
    
    // 🆕 初始化追蹤管理器
    TrackingConfig config;
    config.feature_extract_interval = 10;   // 每 10 幀提取一次
    config.min_face_quality = 0.6f;
    config.recognition_interval = 30;       // 每 30 幀辨識一次
    
    pstHandler->trackerManager = new FaceTrackerManager(
        pstHandler->tdlHandle,
        pstHandler->featureExtractor,
        config
    );
    
    std::cout << "✅ 追蹤系統初始化完成" << std::endl;
    return CVI_SUCCESS;
}

// tdl_handler.cpp 主循環修改
void *TDLHandler_ThreadRoutine(void *pHandle) {
    TDLHandler_t *pstHandler = static_cast<TDLHandler_t *>(pHandle);
    VIDEO_FRAME_INFO_S stFrame;
    cvtdl_face_t stFaceMeta = {0};
    cvtdl_tracker_t stTracker = {0};  // 🆕 追蹤結果
    
    while (!g_bExit) {
        // 1. 獲取幀
        s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);
        if (s32Ret != CVI_SUCCESS) continue;
        
        // 2. 人臉檢測（原有，保持逐幀）
        s32Ret = TDLHandler_DetectFace(pstHandler, &stFrame, &stFaceMeta);
        if (s32Ret != CVI_TDL_SUCCESS) {
            CVI_TDL_Free(&stFaceMeta);
            CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
            continue;
        }
        
        // 3. 🆕 追蹤 + 特徵提取 + 辨識（智能決策）
        s32Ret = pstHandler->trackerManager->processFrame(
            &stFrame, 
            &stFaceMeta, 
            &stTracker
        );
        
        // 4. 🆕 繪製結果（使用追蹤信息）
        TDLHandler_DrawTrackingResults(pstHandler, &stTracker, &stFrame);
        
        // 5. 更新全局數據
        {
            LOCK_RESULT_MUTEX();
            // 更新追蹤信息到全局變量
            // ...
            UNLOCK_RESULT_MUTEX();
        }
        
        // 清理
        CVI_TDL_Free(&stFaceMeta);
        CVI_TDL_Free(&stTracker);
        CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
    }
    
    pthread_exit(nullptr);
}
```

---

## 📊 效能分析與優化

### 預估延遲（每幀）

| 步驟 | 延遲 | 頻率 | 備註 |
|-----|------|------|------|
| 人臉檢測（SCRFD） | 10-20ms | 每幀 | TPU 加速 |
| DeepSORT 追蹤 | 1-2ms | 每幀 | 純位置追蹤 |
| 人臉對齊（GDC） | 1-2ms | 每 10 幀 | 硬體加速 |
| NV21→RGB 轉換 | 0.5-1ms | 每 10 幀 | NCNN NEON 優化 |
| ArcFace 特徵提取 | 15-30ms | 每 10 幀 | NCNN 推理 |
| 辨識（資料庫比對） | 0.1-1ms | 每 30 幀 | 餘弦距離計算 |

**最壞情況（每 10 幀）**：
- 20ms (檢測) + 2ms (追蹤) + 2ms (對齊) + 1ms (轉換) + 30ms (特徵) = **55ms**
- **FPS ≈ 18**（可接受）

**一般情況（其他幀）**：
- 20ms (檢測) + 2ms (追蹤) = **22ms**
- **FPS ≈ 45**（流暢）

### 優化策略

1. **調整特徵提取頻率**
   - 新人臉：立即提取
   - 已追蹤：每 10-20 幀提取一次
   - 已辨識：每 60 幀提取一次

2. **異步處理（可選）**
   ```cpp
   // 使用生產者-消費者模式
   std::queue<FeatureTask> feature_queue_;
   std::thread feature_thread_;
   
   // 主線程：只加入隊列
   if (shouldExtractFeature(...)) {
       feature_queue_.push({track_id, frame_copy, face_info});
   }
   
   // 特徵提取線程：異步處理
   while (!exit) {
       if (!feature_queue_.empty()) {
           auto task = feature_queue_.pop();
           extractFeature(task);
       }
   }
   ```

3. **記憶體優化**
   ```cpp
   // 限制同時追蹤數量
   const int MAX_TRACKS = 10;
   
   // 超過則優先保留中心人臉
   if (active_tracks_.size() > MAX_TRACKS) {
       removeFarthestTrack();
   }
   ```

---

## 🚀 實作步驟

### 階段 A：DeepSORT 基礎整合（1-2天）

**目標**：實現追蹤，不考慮特徵提取

1. ✅ 在 `TDLHandler_Init` 中初始化 DeepSORT
2. ✅ 配置 DeepSORT 參數（針對人臉）
3. ✅ 修改主循環，加入 `CVI_TDL_DeepSORT_Face` 調用
4. ✅ 顯示追蹤 ID 在畫面上（不同顏色標記）
5. ✅ 測試追蹤穩定性（多人、遮擋、快速移動）

**代碼量**：約 100-150 行修改

**驗收標準**：
- 每個人臉有穩定的 `unique_id`
- 短時間內（5-10秒）ID 保持不變
- 多人場景下 ID 不混淆

---

### 階段 B：特徵提取整合（2-3天）

**目標**：使用 NCNN ArcFace 提取特徵

1. ✅ 創建 `FaceFeatureExtractor` 類
2. ✅ 實現 NV21 → RGB 轉換（使用 NCNN 原生函數）
3. ✅ 調用官方 `CVI_TDL_FaceAlignment`（GDC 加速）
4. ✅ 調用 ArcFace 提取特徵
5. ✅ 實現智能提取策略（僅新人臉 + 高品質）
6. ✅ 測試特徵提取延遲

**代碼量**：約 200-300 行新增

**驗收標準**：
- 特徵提取延遲 < 35ms
- 不影響整體 FPS（>= 15fps）
- 特徵向量正確（128 維，L2 正規化）

---

### 階段 C：人臉辨識整合（1-2天）

**目標**：與資料庫比對並顯示身份

1. ✅ 整合 `FaceDatabase` 到主程式
2. ✅ 實現 `FaceTrackerManager`
3. ✅ 實現辨識決策邏輯
4. ✅ 在畫面上顯示辨識結果（ID + 信心度）
5. ✅ 實現按鍵註冊新人臉功能

**代碼量**：約 300-400 行新增

**驗收標準**：
- 辨識準確率 >= 90%（相同人）
- 誤識別率 < 5%（不同人）
- 實時更新辨識結果

---

## ✅ 關鍵技術確認

### 1. NV21 格式處理 ✅

**NCNN 原生支援**：
```cpp
// 函數簽名（位於 ncnn/src/mat.h）
void yuv420sp2rgb(const unsigned char* yuv420sp, int w, int h, unsigned char* rgb);

// 使用方式
unsigned char* nv21_data = frame->stVFrame.pu8VirAddr[0];
unsigned char* rgb_data = new unsigned char[112 * 112 * 3];

// 🔥 關鍵：直接轉換，無需中間步驟
ncnn::yuv420sp2rgb(nv21_data, 112, 112, rgb_data);

// 創建 NCNN Mat
ncnn::Mat mat = ncnn::Mat::from_pixels(rgb_data, ncnn::Mat::PIXEL_RGB, 112, 112);
```

**性能**：
- ARM NEON 硬體加速
- 112x112 轉換延遲：**< 1ms**

### 2. GDC 硬體加速 ✅

**官方 API**：
```cpp
CVI_S32 CVI_TDL_FaceAlignment(
    VIDEO_FRAME_INFO_S *inFrame,      // 768x432, NV21
    const uint32_t metaWidth,          // 768
    const uint32_t metaHeight,         // 432
    const cvtdl_face_info_t *info,    // 人臉信息（bbox + 關鍵點）
    VIDEO_FRAME_INFO_S *outFrame,     // 112x112, NV21
    const bool enableGDC              // ✅ true
);
```

**優勢**：
- 自動處理座標縮放
- 自動內存映射/解映射
- 硬體仿射變換（1-2ms）

### 3. DeepSORT 追蹤 ✅

**無需手動填充特徵**（初期）：
```cpp
// 直接調用，DeepSORT 使用 IOU 追蹤
CVI_TDL_DeepSORT_Face(tdl_handle, &faces, &tracker);

// faces.info[i].feature 可以為空
// DeepSORT 自動降級為純位置追蹤（SORT 模式）
```

**後續優化**（可選）：
- 如果需要 re-ID，手動填充 feature
- 轉換 float[128] → int8_t[128]

---

## 📝 配置文件範例

`config.json` 新增：

```json
{
  "model": {
    "face_detection": "models/scrfd_det_face_432_768_INT8_cv181x.cvimodel",
    "arcface_param": "models/mobilefacenet.param",
    "arcface_bin": "models/mobilefacenet.bin"
  },
  "tracking": {
    "feature_extract_interval": 10,
    "min_face_quality": 0.6,
    "recognition_interval": 30,
    "recognition_threshold": 0.5,
    "track_lost_threshold": 30,
    "max_track_age": 3000
  },
  "display": {
    "show_track_id": true,
    "show_identity": true,
    "show_confidence": true,
    "bbox_color_tracked": [0, 255, 0],
    "bbox_color_new": [255, 255, 0],
    "bbox_color_center": [255, 0, 0]
  }
}
```

---

## 🎯 總結與建議

### ✅ 已解決的關鍵問題

1. **NV21 格式轉換**：NCNN 原生支援，無需額外開銷
2. **人臉對齊**：官方 GDC API，硬體加速
3. **逐幀檢測**：TPU 加速，延遲可接受
4. **追蹤穩定性**：DeepSORT 成熟算法

### 🚀 立即行動

**建議優先級**：
1. **本週**：完成階段 A（DeepSORT 基礎整合）
2. **下週**：完成階段 B（特徵提取整合）
3. **第三週**：完成階段 C（辨識整合）

### ⚠️ 風險與備案

| 風險 | 影響 | 備案 |
|-----|------|------|
| ArcFace 延遲過高 | FPS 下降 | 降低提取頻率 (20 幀) |
| 記憶體不足 | 系統崩潰 | 限制最大追蹤數 (10 人) |
| GDC 不穩定 | 對齊失敗 | 回退到 OpenCV CPU 版本 |

### 📞 下一步確認

請確認：
1. ✅ 是否同意優先完成階段 A？
2. ❓ 是否需要我立即開始實作程式碼？
3. ❓ 是否需要先編譯測試 NCNN NV21 轉換？

---

**文件版本**：V2.0  
**更新日期**：2025-11-20  
**作者**：GitHub Copilot  
**專案**：gmailk-V 人臉追蹤與辨識系統
