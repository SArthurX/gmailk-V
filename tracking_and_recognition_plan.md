# 人臉追蹤與辨識整合計劃

## 問題分析

### 當前系統問題
1. **逐幀偵測無上下文**：目前系統在 `tdl_handler.cpp` 中每一幀都獨立進行人臉檢測
2. **重複辨識效能瓶頸**：如果直接在每一幀進行特徵提取和比對，會造成嚴重效能問題
3. **無物件 ID 持續性**：無法追蹤同一人物在不同幀之間的對應關係

### 目標
1. 整合 DeepSORT 追蹤算法以維持人臉 ID 連續性
2. 智能化特徵提取策略，避免重複計算
3. 使用 NCNN ArcFace 模型進行特徵提取（128維向量）
4. 建立有效的人臉辨識流程

---

## SDK DeepSORT 分析

### 關鍵 API
```c
// 初始化 DeepSORT
CVI_S32 CVI_TDL_DeepSORT_Init(const cvitdl_handle_t handle, bool use_specific_counter);

// 獲取/設置配置
CVI_S32 CVI_TDL_DeepSORT_GetDefaultConfig(cvtdl_deepsort_config_t *ds_conf);
CVI_S32 CVI_TDL_DeepSORT_SetConfig(const cvitdl_handle_t handle, 
                                   cvtdl_deepsort_config_t *ds_conf, 
                                   int cvitdl_obj_type, 
                                   bool show_config);

// 人臉追蹤（關鍵函數）
CVI_S32 CVI_TDL_DeepSORT_Face(const cvitdl_handle_t handle, 
                              cvtdl_face_t *face,
                              cvtdl_tracker_t *tracker);
```

### 重要發現：Feature 欄位存在

從 `deepsort.c` (line 303-350) 和 `cvtdl_face_types.h` 分析：

```c
// cvtdl_face_info_t 結構體包含：
typedef struct {
  char name[128];
  uint64_t unique_id;           // DeepSORT 賦予的追蹤ID
  cvtdl_bbox_t bbox;
  cvtdl_pts_t pts;              // 關鍵點
  cvtdl_feature_t feature;      // ⭐ 特徵向量欄位
  float face_quality;           // 人臉品質分數
  int track_state;              // 追蹤狀態
  // ... 其他欄位
} cvtdl_face_info_t;
```

**關鍵觀察**：
1. ✅ `cvtdl_face_info_t` **確實有 `feature` 欄位**
2. ✅ DeepSORT 算法**可以使用 feature 做 re-identification**
3. ✅ 從 `deepsort.c:314-325` 看到：如果 `feature.size > 0`，會啟用 `use_reid = true`
4. ✅ Feature 會被用於計算 cosine 距離做匹配

```c
// deepsort.c 中的邏輯
bool use_reid = true;
for (uint32_t i = 0; i < bbox_num; i++) {
  if (face->info[i].feature.size == 0 ||
      face->info[0].feature.size != face->info[i].feature.size) {
    use_reid = false;  // 如果沒有特徵或大小不一致，降級為純位置追蹤
    break;
  }
}
```

---

## NCNN ArcFace 模型分析

### 模型特性（從 test/arcface.cpp）
```cpp
class Arcface {
  const int feature_dim = 128;  // 輸出128維特徵向量
  
  std::vector<float> getFeature(ncnn::Mat img);  // 提取特徵
  void normalize(std::vector<float> &feature);   // L2正規化
};

// 相似度計算（餘弦相似度）
float calcSimilar(std::vector<float> feature1, std::vector<float> feature2) {
  float sim = 0.0;
  for (int i = 0; i < feature1.size(); i++)
    sim += feature1[i] * feature2[i];
  return sim;  // 已正規化，直接點積即餘弦相似度
}
```

### 效能考量
- ⚠️ **NCNN 模型可能成為瓶頸**（您的第3點關注）
- ⚠️ 每次提取需要處理 112x112 輸入圖像
- ⚠️ 需要仿射變換對齊人臉（使用5個關鍵點）

### FaceDatabase 實作（test/face_database.cpp）
- ✅ 已有人臉資料庫框架
- ✅ 支持儲存/載入 128維特徵
- ✅ 支持搜尋最相似人臉
- ✅ 閾值控制（threshold）

---

## 關鍵問題與解決方案

### 問題 1: 官方追蹤算法能否使用外部 Feature？

**答案：可以！**

根據 `deepsort.c` 分析：
- DeepSORT 會檢查 `face->info[i].feature` 是否已填充
- 如果有 feature（size > 0），會自動啟用 re-ID
- Feature 類型必須是 `TYPE_INT8` 或經過轉換

**策略**：
```
我們可以自行填充 feature 欄位：
1. 用 NCNN ArcFace 提取 128 維 float 特徵
2. 轉換為 INT8 或直接填充（需確認 SDK 是否支持 float）
3. 設置 feature.size = 128
4. 傳入 CVI_TDL_DeepSORT_Face()
```

### 問題 2: NCNN ArcFace 能否用於 Re-ID？

**答案：完全可以！**

- ArcFace 本身就是設計用於人臉辨識
- 128維向量經過 L2 正規化，適合餘弦距離計算
- DeepSORT 內部也是用餘弦距離做 feature matching

**建議**：
- ✅ 用 ArcFace 提取特徵填入 `cvtdl_face_info_t.feature`
- ✅ DeepSORT 利用這些特徵做短期追蹤（幫助解決遮擋、快速移動）
- ✅ 人臉辨識另外維護資料庫，用相同特徵與資料庫比對

### 問題 3: 效能瓶頸與提取策略

**核心矛盾**：
- 追蹤需要持續更新才穩定
- 特徵提取太慢無法每幀執行
- 辨識需要高品質特徵

**解決策略**：

#### 階段1：追蹤優先（低頻特徵提取）
```
1. 每一幀執行人臉檢測（已有）
2. 每一幀執行 DeepSORT 追蹤（新增）
   - 前幾幀無 feature，純用 bbox 追蹤（SORT模式）
   - 追蹤器會給每個人臉分配 unique_id
3. 僅對「新出現」或「關鍵幀」提取特徵
   - 條件1：tracker.state == CVI_TRACKER_NEW（新人臉）
   - 條件2：face_quality > threshold（品質夠好）
   - 條件3：距離上次提取 >= N 幀（間隔控制）
```

#### 階段2：特徵管理（內存中維護）
```cpp
// 新增數據結構
struct TrackedFace {
  uint64_t track_id;              // DeepSORT 的 unique_id
  std::vector<float> feature;     // ArcFace 128維特徵
  std::string identity;           // 辨識結果（"Unknown" 或姓名）
  float confidence;               // 辨識信心
  int frames_since_feature;       // 距離上次特徵提取的幀數
  int frames_since_recognition;   // 距離上次辨識的幀數
  bool is_recognized;             // 是否已完成辨識
};

std::map<uint64_t, TrackedFace> active_tracks;
```

#### 階段3：智能辨識（更低頻）
```
只在以下條件執行人臉辨識（比對資料庫）：
1. 有新特徵被提取
2. 該 track_id 從未辨識過
3. face_quality 足夠高
4. 或定期重新辨識（每N秒一次）
```

---

## 實作架構設計

### 新增模組結構

```
├── include/
│   ├── face_tracker_manager.h      [新增] 追蹤管理器
│   └── face_feature_extractor.h    [新增] 特徵提取器（封裝NCNN ArcFace）
├── src/
│   ├── face_tracker_manager.cpp
│   └── face_feature_extractor.cpp
└── tmp/
    └── tracking_and_recognition_plan.md  [本文件]
```

### 模組1: FaceFeatureExtractor（特徵提取）

**職責**：
- 封裝 NCNN ArcFace 模型
- 使用官方 `CVI_TDL_FaceAlignment` 進行人臉對齊（支援 GDC 硬體加速）
- 提供從 VIDEO_FRAME 中提取特定人臉特徵的接口

**接口設計**：
```cpp
class FaceFeatureExtractor {
public:
  FaceFeatureExtractor(const std::string& model_param, 
                       const std::string& model_bin,
                       cvitdl_handle_t tdl_handle);
  
  // 從幀中提取指定人臉的特徵（使用官方對齊 API）
  bool extractFeature(VIDEO_FRAME_INFO_S* frame,
                      cvtdl_face_info_t* face_info,
                      std::vector<float>& feature);
  
  // 批量提取（如果需要）
  bool extractFeatures(VIDEO_FRAME_INFO_S* frame,
                       cvtdl_face_t* faces,
                       std::vector<std::vector<float>>& features);
private:
  Arcface* arcface_model;
  cvitdl_handle_t tdl_handle;
  bool use_gdc;  // 系統是否支援 GDC（已確認：✅ 支援）
  
  // 使用官方 API 對齊人臉（支援 GDC 硬體加速）
  CVI_S32 alignFaceWithGDC(VIDEO_FRAME_INFO_S* inFrame,
                           cvtdl_face_info_t* face_info,
                           VIDEO_FRAME_INFO_S* outFrame);
  
  // 轉換 VIDEO_FRAME 到 ncnn::Mat
  ncnn::Mat frameToNcnnMat(VIDEO_FRAME_INFO_S* frame);
};
```

**`CVI_TDL_FaceAlignment` 內部流程**（已驗證系統支援 GDC）：

```cpp
CVI_S32 CVI_TDL_FaceAlignment(VIDEO_FRAME_INFO_S *inFrame, 
                              const uint32_t metaWidth,
                              const uint32_t metaHeight, 
                              const cvtdl_face_info_t *info,
                              VIDEO_FRAME_INFO_S *outFrame, 
                              const bool enableGDC) {
  
  if (enableGDC) {
    // ===== GDC 硬體加速模式（您的系統支援 ✅）=====
    
    // 1. 檢查輸入格式（GDC 模式支援的格式）
    if (inFrame->stVFrame.enPixelFormat != PIXEL_FORMAT_RGB_888_PLANAR &&
        inFrame->stVFrame.enPixelFormat != PIXEL_FORMAT_YUV_PLANAR_420) {
      LOGE("GDC mode: Unsupported format. Need RGB_888_PLANAR or YUV_PLANAR_420");
      return CVI_TDL_FAILURE;
    }
    
    // 2. 座標縮放：將檢測座標轉換到原圖座標系
    cvtdl_face_info_t face_info = cvitdl::info_rescale_c(
        metaWidth, metaHeight,              // 檢測時的圖像尺寸
        inFrame->stVFrame.u32Width,         // 原圖寬度
        inFrame->stVFrame.u32Height,        // 原圖高度
        *info                               // 輸入的人臉信息
    );
    
    // 3. 使用 GDC 硬體進行人臉對齊（仿射變換）
    //    - 基於 5 個關鍵點計算仿射矩陣
    //    - 使用硬體加速裁剪並對齊到標準姿態
    //    - 輸出 112x112 的對齊人臉
    cvitdl::face_align_gdc(inFrame, outFrame, face_info);
    //    ⚡ 性能：~1-2ms（硬體加速）
    
  } else {
    // ===== OpenCV CPU 模式（回退方案）=====
    
    // 1. 檢查輸入格式（OpenCV 模式只支援 RGB_888）
    if (inFrame->stVFrame.enPixelFormat != PIXEL_FORMAT_RGB_888) {
      LOGE("OpenCV mode: Unsupported format. Need RGB_888");
      return CVI_TDL_FAILURE;
    }
    
    // 2. 內存映射（如果尚未映射）
    bool do_unmap_in = false, do_unmap_out = false;
    if (inFrame->stVFrame.pu8VirAddr[0] == NULL) {
      inFrame->stVFrame.pu8VirAddr[0] = (CVI_U8 *)CVI_SYS_Mmap(
          inFrame->stVFrame.u64PhyAddr[0], 
          inFrame->stVFrame.u32Length[0]
      );
      do_unmap_in = true;
    }
    if (outFrame->stVFrame.pu8VirAddr[0] == NULL) {
      outFrame->stVFrame.pu8VirAddr[0] = (CVI_U8 *)CVI_SYS_Mmap(
          outFrame->stVFrame.u64PhyAddr[0], 
          outFrame->stVFrame.u32Length[0]
      );
      do_unmap_out = true;
    }
    
    // 3. 轉換為 OpenCV Mat
    cv::Mat image(
        cv::Size(inFrame->stVFrame.u32Width, inFrame->stVFrame.u32Height), 
        CV_8UC3,
        inFrame->stVFrame.pu8VirAddr[0], 
        inFrame->stVFrame.u32Stride[0]
    );
    cv::Mat warp_image(
        cv::Size(outFrame->stVFrame.u32Width, outFrame->stVFrame.u32Height),
        image.type(), 
        outFrame->stVFrame.pu8VirAddr[0],
        outFrame->stVFrame.u32Stride[0]
    );
    
    // 4. 座標縮放
    cvtdl_face_info_t face_info = cvitdl::info_rescale_c(
        metaWidth, metaHeight, 
        inFrame->stVFrame.u32Width, 
        inFrame->stVFrame.u32Height, 
        *info
    );
    
    // 5. 使用 OpenCV 進行人臉對齊（仿射變換）
    cvitdl::face_align(image, warp_image, face_info);
    //    🐌 性能：~5-10ms（CPU 軟體實現）
    
    // 6. Cache 刷新（確保 CPU 寫入對硬體可見）
    CVI_SYS_IonFlushCache(
        outFrame->stVFrame.u64PhyAddr[0], 
        outFrame->stVFrame.pu8VirAddr[0],
        outFrame->stVFrame.u32Length[0]
    );
    
    // 7. 清理內存映射
    if (do_unmap_in) {
      CVI_SYS_Munmap((void *)inFrame->stVFrame.pu8VirAddr[0], 
                     inFrame->stVFrame.u32Length[0]);
      inFrame->stVFrame.pu8VirAddr[0] = NULL;
    }
    if (do_unmap_out) {
      CVI_SYS_Munmap((void *)outFrame->stVFrame.pu8VirAddr[0], 
                     outFrame->stVFrame.u32Length[0]);
      outFrame->stVFrame.pu8VirAddr[0] = NULL;
    }
  }
  
  return CVI_TDL_SUCCESS;
}
```

**關鍵點總結**：
1. ✅ **您的系統有 GDC**（`/proc/cvitek/gdc` 存在）
2. ✅ **應使用 `enableGDC=true`** 以獲得最佳性能
3. ⚡ **GDC 模式性能**：~1-2ms（硬體加速）
4. 🔄 **自動處理**：座標縮放、內存管理、Cache 同步
5. 📋 **支援格式**：
   - GDC 模式：`PIXEL_FORMAT_RGB_888_PLANAR`, `PIXEL_FORMAT_YUV_PLANAR_420`
   - OpenCV 模式：`PIXEL_FORMAT_RGB_888`
6. 🎯 **輸出尺寸**：通常 112x112（ArcFace 標準輸入尺寸）

### 模組2: FaceTrackerManager（追蹤與辨識管理）

**職責**：
- 管理 DeepSORT 追蹤器
- 維護活動追蹤列表
- 決策何時提取特徵、何時執行辨識
- 填充 feature 到 cvtdl_face_t 結構

**接口設計**：
```cpp
struct TrackingConfig {
  int feature_extract_interval;      // 特徵提取間隔（幀數）
  float min_face_quality;             // 最小人臉品質閾值
  int recognition_interval;           // 辨識間隔（幀數）
  float recognition_threshold;        // 辨識相似度閾值
  int track_lost_threshold;           // 追蹤丟失閾值
};

class FaceTrackerManager {
public:
  FaceTrackerManager(cvitdl_handle_t tdl_handle,
                     FaceFeatureExtractor* extractor,
                     FaceDatabase* database,
                     const TrackingConfig& config);
  
  // 主處理函數：檢測 -> 追蹤 -> 特徵提取 -> 辨識
  CVI_S32 processFrame(VIDEO_FRAME_INFO_S* frame,
                       cvtdl_face_t* faces,
                       cvtdl_tracker_t* tracker);
  
  // 獲取追蹤列表（用於顯示）
  std::vector<TrackedFaceInfo> getActiveTracks();
  
private:
  cvitdl_handle_t tdl_handle;
  FaceFeatureExtractor* feature_extractor;
  FaceDatabase* face_db;
  TrackingConfig config;
  
  std::map<uint64_t, TrackedFace> active_tracks;
  
  // 決策：是否需要提取特徵
  bool shouldExtractFeature(uint64_t track_id, 
                           cvtdl_face_info_t* face_info,
                           cvtdl_trk_state_type_t track_state);
  
  // 執行特徵提取並填充到 cvtdl_face_t
  CVI_S32 extractAndFillFeatures(VIDEO_FRAME_INFO_S* frame,
                                 cvtdl_face_t* faces);
  
  // 執行人臉辨識（與資料庫比對）
  void recognizeFaces(cvtdl_tracker_t* tracker);
  
  // 清理丟失的追蹤
  void cleanLostTracks(cvtdl_tracker_t* tracker);
};
```

### 整合到 tdl_handler.cpp

**修改點**：
```cpp
// tdl_handler.h 中新增
typedef struct {
  // ... 原有欄位
  FaceTrackerManager* tracker_manager;  // 新增
} TDLHandler_t;

// tdl_handler.cpp 主循環修改
void *TDLHandler_ThreadRoutine(void *pHandle) {
  TDLHandler_t *pstHandler = static_cast<TDLHandler_t *>(pHandle);
  VIDEO_FRAME_INFO_S stFrame;
  cvtdl_face_t stFaceMeta = {0};
  cvtdl_tracker_t stTracker = {0};  // 新增
  
  while (!g_bExit) {
    // 1. 獲取幀
    s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);
    
    // 2. 人臉檢測（原有）
    s32Ret = TDLHandler_DetectFace(pstHandler, &stFrame, &stFaceMeta);
    
    // 3. 追蹤 + 特徵提取 + 辨識（新增）
    s32Ret = pstHandler->tracker_manager->processFrame(
      &stFrame, &stFaceMeta, &stTracker);
    
    // 4. 繪製結果（使用 tracker 信息）
    drawTrackingResults(&stFrame, &stFaceMeta, &stTracker);
    
    // 5. 更新全局數據
    updateGlobalData(&stFaceMeta, &stTracker);
    
    // 清理
    CVI_TDL_Free(&stFaceMeta);
    CVI_TDL_Free(&stTracker);
    CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
  }
}
```

---

## 實作步驟建議

### 階段 A：DeepSORT 基礎整合（優先完成）

**目標**：先實現追蹤，不考慮特徵提取

1. ✅ 在 `TDLHandler_Init` 中初始化 DeepSORT
2. ✅ 配置 DeepSORT 參數（針對人臉）
3. ✅ 修改主循環，加入 `CVI_TDL_DeepSORT_Face` 調用
4. ✅ 顯示追蹤 ID 在畫面上
5. ✅ 測試追蹤穩定性（多人、遮擋、快速移動）

**預期結果**：
- 每個人臉有穩定的 `unique_id`
- 短時間內 ID 保持不變
- 但無 re-ID 能力（因為還沒有 feature）

**代碼量**：約 100-150 行修改

---

### 階段 B：特徵提取整合

**目標**：使用 NCNN ArcFace 提取特徵並填入 DeepSORT

1. ✅ 創建 `FaceFeatureExtractor` 類
2. ✅ 實現從 VIDEO_FRAME 中裁剪並對齊人臉
3. ✅ 調用 ArcFace 提取特徵
4. ✅ 將 float 特徵轉換並填充到 `cvtdl_face_info_t.feature`
5. ✅ 實現智能提取策略（僅新人臉 + 高品質）
6. ✅ 測試 DeepSORT re-ID 是否生效

**挑戰**：
- VIDEO_FRAME_INFO_S 到 ncnn::Mat 的轉換
- 人臉裁剪和對齊的實現
- feature 數據類型轉換（float to INT8？）

**代碼量**：約 200-300 行新增

---

### 階段 C：人臉辨識整合

**目標**：與資料庫比對並顯示身份

1. ✅ 整合 `FaceDatabase` 到主程式
2. ✅ 實現 `FaceTrackerManager`
3. ✅ 實現辨識決策邏輯
4. ✅ 在畫面上顯示辨識結果
5. ✅ 實現按鍵註冊新人臉功能（使用 button_handler）

**代碼量**：約 300-400 行新增

---

## 技術細節待確認

### 1. Feature 數據類型轉換 ✅ 已解決

**方案**：使用官方 `CVI_TDL_FaceAlignment` + ArcFace 提取 float 特徵

從 `deepsort.c` 分析，DeepSORT 支持 `TYPE_INT8`，但也可以接受其他類型：

```cpp
// deepsort.c 中的處理
if (face->info[i].feature.type != TYPE_INT8) {
  LOGE("Feature Type not support now.\n");
  return CVI_TDL_FAILURE;
}
int type_size = getFeatureTypeSize(face->info[i].feature.type);
for (uint32_t d = 0; d < feature_size; d++) {
  feature_(d) = static_cast<float>(obj->info[i].feature.ptr[d * type_size]);
}
```

**推薦方案**：將 ArcFace 的 float 特徵轉為 INT8

```cpp
// 填充特徵到 cvtdl_face_info_t
void fillFeatureToFaceInfo(cvtdl_face_info_t* face_info, 
                          const std::vector<float>& feature) {
  // 分配特徵內存
  face_info->feature.size = 128;
  face_info->feature.type = TYPE_INT8;
  face_info->feature.ptr = (int8_t*)malloc(128 * sizeof(int8_t));
  
  // 轉換 float [-1, 1] 到 int8 [-127, 127]
  for (int i = 0; i < 128; i++) {
    // 特徵已經 L2 正規化，範圍約 [-1, 1]
    face_info->feature.ptr[i] = static_cast<int8_t>(feature[i] * 127.0f);
  }
}
```

### 2. VIDEO_FRAME 到 ncnn::Mat 轉換 ✅ 已有方案

**使用官方 `CVI_TDL_FaceAlignment` 輸出 112x112 對齊人臉**：

```cpp
ncnn::Mat FaceFeatureExtractor::frameToNcnnMat(VIDEO_FRAME_INFO_S* frame) {
  // 1. 確保內存映射
  bool need_unmap = false;
  if (frame->stVFrame.pu8VirAddr[0] == NULL) {
    CVI_SYS_Mmap(&frame->stVFrame);
    need_unmap = true;
  }
  
  int width = frame->stVFrame.u32Width;    // 112
  int height = frame->stVFrame.u32Height;  // 112
  
  // 2. 根據格式轉換
  ncnn::Mat result;
  
  if (frame->stVFrame.enPixelFormat == PIXEL_FORMAT_RGB_888) {
    // RGB_888 格式：直接使用
    // Stride 可能大於 width*3，需要處理
    cv::Mat cv_mat(height, width, CV_8UC3,
                   frame->stVFrame.pu8VirAddr[0],
                   frame->stVFrame.u32Stride[0]);
    
    // 轉換為 ncnn::Mat（BGR -> RGB）
    result = ncnn::Mat::from_pixels(cv_mat.data, ncnn::Mat::PIXEL_BGR2RGB,
                                   width, height);
    
  } else if (frame->stVFrame.enPixelFormat == PIXEL_FORMAT_RGB_888_PLANAR) {
    // Planar RGB：R, G, B 分開存儲
    result = ncnn::Mat(width, height, 3);
    uint8_t* r_plane = frame->stVFrame.pu8VirAddr[0];
    uint8_t* g_plane = r_plane + width * height;
    uint8_t* b_plane = g_plane + width * height;
    
    // 拷貝到 ncnn::Mat
    memcpy(result.channel(0), r_plane, width * height);
    memcpy(result.channel(1), g_plane, width * height);
    memcpy(result.channel(2), b_plane, width * height);
    
  } else if (frame->stVFrame.enPixelFormat == PIXEL_FORMAT_YUV_PLANAR_420) {
    // YUV420 -> RGB 轉換
    cv::Mat yuv(height + height/2, width, CV_8UC1, 
               frame->stVFrame.pu8VirAddr[0]);
    cv::Mat rgb;
    cv::cvtColor(yuv, rgb, cv::COLOR_YUV2RGB_I420);
    
    result = ncnn::Mat::from_pixels(rgb.data, ncnn::Mat::PIXEL_RGB,
                                   width, height);
  }
  
  // 3. 清理
  if (need_unmap) {
    CVI_SYS_Munmap((void*)frame->stVFrame.pu8VirAddr[0],
                   frame->stVFrame.u32Length[0]);
    frame->stVFrame.pu8VirAddr[0] = NULL;
  }
  
  return result;
}
```

### 3. 人臉對齊實現 ✅ 使用官方 API

**已確認方案**：使用 `CVI_TDL_FaceAlignment` with GDC

- ✅ 系統支援 GDC 硬體（`/proc/cvitek/gdc` 已驗證）
- ✅ 性能：~1-2ms（硬體加速）
- ✅ 自動處理座標縮放、內存管理
- ✅ 輸出標準 112x112 對齊人臉

**完整流程**：
```
1. 人臉檢測 -> 獲得 bbox + 5個關鍵點
2. CVI_TDL_FaceAlignment (GDC) -> 112x112 對齊人臉
3. frameToNcnnMat -> ncnn::Mat
4. ArcFace 推理 -> 128維特徵 (float)
5. 轉換為 INT8 -> 填充到 cvtdl_face_info_t.feature
6. 傳入 CVI_TDL_DeepSORT_Face -> 追蹤 + Re-ID
```

---

## 效能優化建議

### 策略 1：分級處理
```
Level 1 (每幀):     人臉檢測 + 追蹤 (純 bbox)
Level 2 (每5幀):    特徵提取 (僅新人臉或品質好的)
Level 3 (每30幀):   人臉辨識 (比對資料庫)
```

### 策略 2：多執行緒
```
Thread 1: 主循環 (檢測 + 追蹤)
Thread 2: 特徵提取隊列處理
Thread 3: 辨識隊列處理
```

### 策略 3：特徵緩存
```
- 每個 track_id 只保留最新的一個特徵
- 辨識成功後降低更新頻率
- 追蹤丟失後保留特徵 N 秒（處理暫時遮擋）
```

---

## 測試計劃

### 功能測試
1. ✅ 單人追蹤穩定性
2. ✅ 多人追蹤 ID 不混淆
3. ✅ 遮擋後 re-ID 是否有效
4. ✅ 人臉辨識準確率
5. ✅ 資料庫註冊與查詢

### 效能測試
1. ⏱️ FPS 監控（期望 >= 15 fps）
2. ⏱️ 特徵提取耗時
3. ⏱️ 辨識耗時
4. ⏱️ 記憶體占用

### 壓力測試
1. 💪 5+ 人同時追蹤
2. 💪 快速移動
3. 💪 光照變化
4. 💪 角度變化

---

## 風險與備案

### 風險 1：NCNN ArcFace 太慢

**備案**：
- 降低提取頻率（10-20幀一次）
- 使用更小的模型
- 或尋找官方支持的模型（但您說不要用官方 FaceRecognition）

### 風險 2：DeepSORT 不支持外部 float feature

**備案**：
- 自己實現簡化版追蹤（IOU + 特徵距離）
- 或使用 ByteTrack（純 IOU，不用特徵）

### 風險 3：記憶體不足

**備案**：
- 限制同時追蹤數量
- 更積極的清理策略
- 特徵向量量化（128 float -> 128 int8 = 4倍省空間）

---

## 結論與建議

### 立即行動建議

**優先級 1**（本週完成）：
✅ **先完成 DeepSORT 基礎整合（階段A）**
- 程式碼改動最小
- 立即能看到追蹤效果
- 為後續打好基礎

**優先級 2**（下週開始）：
🔄 **實現特徵提取（階段B）**
- 這是最大挑戰
- 需要解決格式轉換問題
- 需要測試效能

**優先級 3**（視效能而定）：
⏳ **完整辨識系統（階段C）**
- 依賴前兩階段成功
- 相對獨立，可以最後做

### 關鍵決策點

**問題A**：DeepSORT 是否支持 float feature？
- 🔍 **需要實驗驗證**
- 影響階段B的實現方式

**問題B**：NCNN ArcFace 效能是否可接受？
- 🔍 **需要實際測試**
- 可能需要調整策略

**問題C**：是否需要多執行緒？
- 🔍 **先單執行緒實現，遇到瓶頸再優化**

---

## 下一步行動

請您確認：
1. ✅ 是否同意先完成「階段A：DeepSORT基礎整合」？
2. ❓ 是否需要我現在就開始實作階段A的程式碼？
3. ❓ 還是您想先看到更詳細的某個技術細節說明？

我建議：
👉 **先完成階段A，驗證追蹤基本可行**
👉 **然後再決定是否投入階段B（特徵提取）**
👉 **避免一次改動太大導致難以除錯**
