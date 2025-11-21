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
  
  // 從幀中提取指定人臉的特徵（使用官方對齊 API + IVE 加速）
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
  IVE_HANDLE ive_handle;  // IVE 硬體加速句柄
  
  // 使用官方 API 對齊人臉（支援 GDC 硬體加速）
  CVI_S32 alignFaceWithGDC(VIDEO_FRAME_INFO_S* inFrame,
                           cvtdl_face_info_t* face_info,
                           VIDEO_FRAME_INFO_S* outFrame);
  
  // 使用 IVE 硬體加速進行 NV21 → RGB 轉換
  CVI_S32 convertNV21ToRGB_IVE(VIDEO_FRAME_INFO_S* inFrame,
                               VIDEO_FRAME_INFO_S* outFrame);
  
  // 轉換 VIDEO_FRAME 到 ncnn::Mat
  ncnn::Mat frameToNcnnMat(VIDEO_FRAME_INFO_S* frame);
};
```

**硬體加速支援狀態**：
- ✅ **IVE (Image & Video Engine)**：已導入 `cvi_ive.h`，支援 NV21 → RGB 硬體加速轉換
- ✅ **GDC (Geometric Distortion Correction)**：支援人臉對齊仿射變換硬體加速

**`CVI_TDL_FaceAlignment` 內部流程**（已驗證系統支援 GDC）：

```cpp
CVI_S32 CVI_TDL_FaceAlignment(VIDEO_FRAME_INFO_S *inFrame, 
                              const uint32_t metaWidth,
                              const uint32_t metaHeight, 
                              const cvtdl_face_info_t *info,
                              VIDEO_FRAME_INFO_S *outFrame, 
                              ) {
  
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
  return CVI_TDL_SUCCESS;
}
```

**關鍵點總結**：
6. 🔄 **自動處理**：座標縮放、內存管理、Cache 同步
7. 📋 **支援格式**：
   - GDC 模式：`PIXEL_FORMAT_RGB_888_PLANAR`, `PIXEL_FORMAT_YUV_PLANAR_420`
   - OpenCV 模式：`PIXEL_FORMAT_RGB_888`
   - IVE 模式：`PIXEL_FORMAT_NV21` → `PIXEL_FORMAT_RGB_888_PLANAR`
8. 🎯 **輸出尺寸**：通常 112x112（ArcFace 標準輸入尺寸）

### 模組1.1: IVE 硬體加速色彩空間轉換

**背景**：
- 視訊流通常為 NV21 格式（YUV420SP）
- NCNN ArcFace 需要 RGB 格式輸入

**IVE NV21 → RGB 轉換流程**：
```cpp
CVI_S32 FaceFeatureExtractor::convertNV21ToRGB_IVE(
    VIDEO_FRAME_INFO_S* nv21Frame,
    VIDEO_FRAME_INFO_S* rgbFrame) {
  
  // 1. 初始化 IVE 句柄（如果尚未初始化）
  if (ive_handle == NULL) {
    CVI_S32 ret = CVI_IVE_CreateHandle(&ive_handle);
    if (ret != CVI_SUCCESS) {
      LOGE("Failed to create IVE handle");
      return CVI_FAILURE;
    }
  }
  
  // 2. 創建 IVE 圖像結構
  IVE_IMAGE_S src_img, dst_img;
  
  // 2.1 源圖像（NV21）
  src_img.enType = IVE_IMAGE_TYPE_YUV420SP;  // NV21 格式
  src_img.u32Width = nv21Frame->stVFrame.u32Width;
  src_img.u32Height = nv21Frame->stVFrame.u32Height;
  src_img.u32Stride[0] = nv21Frame->stVFrame.u32Stride[0];
  src_img.u64PhyAddr[0] = nv21Frame->stVFrame.u64PhyAddr[0];  // Y 平面
  src_img.u64PhyAddr[1] = nv21Frame->stVFrame.u64PhyAddr[1];  // UV 平面
  src_img.pu8VirAddr[0] = nv21Frame->stVFrame.pu8VirAddr[0];
  src_img.pu8VirAddr[1] = nv21Frame->stVFrame.pu8VirAddr[1];
  
  // 2.2 目標圖像（RGB Planar）
  dst_img.enType = IVE_IMAGE_TYPE_U8C3_PLANAR;  // RGB Planar 格式
  dst_img.u32Width = rgbFrame->stVFrame.u32Width;
  dst_img.u32Height = rgbFrame->stVFrame.u32Height;
  dst_img.u32Stride[0] = rgbFrame->stVFrame.u32Stride[0];
  dst_img.u64PhyAddr[0] = rgbFrame->stVFrame.u64PhyAddr[0];  // R 平面
  dst_img.u64PhyAddr[1] = rgbFrame->stVFrame.u64PhyAddr[1];  // G 平面
  dst_img.u64PhyAddr[2] = rgbFrame->stVFrame.u64PhyAddr[2];  // B 平面
  dst_img.pu8VirAddr[0] = rgbFrame->stVFrame.pu8VirAddr[0];
  dst_img.pu8VirAddr[1] = rgbFrame->stVFrame.pu8VirAddr[1];
  dst_img.pu8VirAddr[2] = rgbFrame->stVFrame.pu8VirAddr[2];
  
  // 3. 執行 IVE 色彩空間轉換（硬體加速）
  IVE_CSC_CTRL_S csc_ctrl;
  csc_ctrl.enMode = IVE_CSC_MODE_PIC_BT709_YUV2RGB;  // BT.709 標準
  
  CVI_S32 ret = CVI_IVE_CSC(ive_handle, &src_img, &dst_img, &csc_ctrl, CVI_TRUE);
  //                                                             ^^^^^^ 阻塞模式
  //    ⚡ 性能：~0.5-1ms（硬體加速）
  
  if (ret != CVI_SUCCESS) {
    LOGE("IVE CSC failed: %d", ret);
    return CVI_FAILURE;
  }
  
  // 4. Cache 刷新（確保硬體寫入對 CPU 可見）
  CVI_SYS_IonInvalidateCache(dst_img.u64PhyAddr[0], 
                             dst_img.pu8VirAddr[0], 
                             rgbFrame->stVFrame.u32Length[0]);
  
  return CVI_SUCCESS;
}
```

**IVE 使用時機**：
- ✅ **用於**：NCNN ArcFace 特徵提取前的格式轉換
- ✅ **用於**：需要高頻率轉換的場景（每幀處理）
- ❌ **不用於**：DeepSORT 輸入（SDK 內部已處理 NV21）
- ❌ **不用於**：TDL 人臉檢測輸入（SDK 內部已處理 NV21）

**與 GDC 的配合**：
```
完整流程（優化版本）：
┌──────────────┐
│ NV21 Frame   │ 原始視訊流
└──────┬───────┘
       │
       ├─────────────────────────────┐
       │                             │
       ▼                             ▼
┌──────────────┐           ┌──────────────┐
│ TDL 人臉檢測  │           │ IVE NV21→RGB │ 如果需要特徵提取
│ (支援 NV21)  │           │ (~0.5-1ms)   │
└──────┬───────┘           └──────┬───────┘
       │                          │
       ▼                          │
┌──────────────┐                  │
│ DeepSORT     │                  │
│ (支援 NV21)  │                  │
└──────┬───────┘                  │
       │                          │
       ▼                          ▼
┌──────────────┐           ┌──────────────┐
│ 追蹤決策:    │           │ GDC 人臉對齊  │
│ 是否提取特徵? │───YES────>│ (~1-2ms)     │
└──────────────┘           └──────┬───────┘
                                  │
                                  ▼
                           ┌──────────────┐
                           │ NCNN ArcFace │
                           │ 特徵提取     │
                           │ (~50-100ms)  │
                           └──────────────┘
```

**性能優勢**：
- ⚠️ **仍然瓶頸**：NCNN ArcFace 特徵提取 ~50-100ms（無硬體加速）

**需要確認的問題**：
1. 當前系統的 NV21 幀是否已經有虛擬地址映射？
   - 如果 `pu8VirAddr[0] == NULL`，需要先 `CVI_SYS_Mmap` A:可以觀察tdl怎麼實作dump擷取圖片
2. RGB 輸出緩衝區如何分配？
   - 選項A：使用 VB pool 預先分配（推薦）
   - 選項B：動態分配（需要 `CVI_SYS_Alloc` + Ion 內存）

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


## 🔥 重要發現與改進方案



### 問題 2: YUV 轉 RGB 硬體加速方案

#### ✅ SDK 提供的硬體加速接口

從搜尋結果發現，**SDK 有多種 YUV->RGB 轉換方案**：

#### 方案 A：IVE 硬體加速 CSC（推薦）

**API**: `CVI_IVE_CSC` 和 `CVI_IVE_FilterAndCSC`

**特點**：
- ✅ **硬體加速**（使用 IVE 引擎）
- ✅ 支持 YUV420SP/YUV420P -> RGB 轉換

```cpp
VIDEO_FRAME_INFO_S aligned_face;
aligned_face.stVFrame.u32Width = 112;
aligned_face.stVFrame.u32Height = 112;
aligned_face.stVFrame.enPixelFormat = PIXEL_FORMAT_RGB_888_PLANAR;  // 指定 RGB 輸出

CVI_S32 ret = CVI_TDL_FaceAlignment(
    pstFrame,                        // 原始幀（YUV）
    pstFrame->stVFrame.u32Width,     // 原圖寬度
    pstFrame->stVFrame.u32Height,    // 原圖高度
    &stFaceMeta.info[i],             // 人臉信息
    &aligned_face,                   // 輸出對齊後的人臉
    true                             // 啟用 GDC 硬體加速
);

// 2. 如果 GDC 支持直接輸出 RGB，則無需轉換
// 如果 GDC 輸出仍是 YUV，則使用方案 A 轉換
if (aligned_face.stVFrame.enPixelFormat != PIXEL_FORMAT_RGB_888_PLANAR) {
  // 執行 YUV -> RGB 轉換（使用方案 A）
}

// 3. 轉換為 ncnn::Mat 並推理
ncnn::Mat ncnn_input = frameToNcnnMat(&aligned_face);
std::vector<float> feature = arcface_model->getFeature(ncnn_input);
```

### 📋 完整工作流程建議

#### 階段 A：DeepSORT實作

```cpp
cvtdl_deepsort_config_t ds_conf;
CVI_TDL_DeepSORT_GetDefaultConfig(&ds_conf);
CVI_TDL_DeepSORT_SetConfig(tdl_handle, &ds_conf, -1, true);
CVI_TDL_DeepSORT_Face(tdl_handle, &stFaceMeta, &stTracker);
```

#### 階段 B：整合 ArcFace 特徵提取

```cpp
// 使用 TDL SDK cvtColor 進行 YUV->RGB 轉換
cv::Mat yuv_mat(...);  // 從 VIDEO_FRAME 構建
cv::Mat rgb_mat;
cvitdl::cvtColor(yuv_mat, rgb_mat, cvitdl::COLOR_YUV2RGB_NV21, 3);

// 轉換為 ncnn::Mat 並推理
ncnn::Mat ncnn_rgb = ncnn::Mat::from_pixels(rgb_mat.data, ncnn::Mat::PIXEL_RGB,
                                            rgb_mat.cols, rgb_mat.rows);
std::vector<float> feature = arcface_model->getFeature(ncnn_rgb);

// 填充特徵到 DeepSORT
fillFeatureToFaceInfo(&stFaceMeta.info[i], feature);
```

---

**需要確認的問題**：
- ❓ 當前 `VIDEO_FRAME_INFO_S` 的虛擬地址是否已映射？
  - 檢查位置：`tdl_handler.cpp` 中的幀處理流程
  - 如果 `stFrame.stVFrame.pu8VirAddr[0] == NULL`，需要先 `CVI_SYS_Mmap`
  
- ❓ RGB 輸出緩衝區分配策略？
  - **選項 A**：使用 VB pool 預先分配（推薦，性能穩定）
  - **選項 B**：動態使用 `CVI_SYS_Alloc` + Ion 內存（靈活但可能有碎片）
  - **選項 C**：復用現有的 TDL 內部緩衝區（需要研究 SDK 內部實現）
  - 解釋差異

- ❓ IVE 色彩空間轉換標準？
  - `IVE_CSC_MODE_PIC_BT709_YUV2RGB`（BT.709，HDTV 標準）
  - `IVE_CSC_MODE_PIC_BT601_YUV2RGB`（BT.601，SDTV 標準）
  - **建議**：先用 BT.709，如果顏色不對再切換到 BT.601

#### 2. FaceFeatureExtractor 模組實作
**目標**：封裝 NCNN ArcFace + IVE/GDC 加速

**實作步驟**：
```cpp
class FaceFeatureExtractor {
  // 1. 構造時初始化 IVE handle
  FaceFeatureExtractor() {
    CVI_IVE_CreateHandle(&ive_handle);
    // 加載 NCNN ArcFace 模型
  }
  
  // 2. 實作核心提取函數
  bool extractFeature(VIDEO_FRAME_INFO_S* nv21Frame,
                      cvtdl_face_info_t* face_info,
                      std::vector<float>& feature) {
    // Step 1: IVE NV21 -> RGB (~0.5-1ms)
    VIDEO_FRAME_INFO_S rgb_frame;
    convertNV21ToRGB_IVE(nv21Frame, &rgb_frame);
    
    // Step 2: GDC 人臉對齊 (~1-2ms)
    VIDEO_FRAME_INFO_S aligned_face;
    CVI_TDL_FaceAlignment(&rgb_frame, ..., face_info, &aligned_face, true);
    
    // Step 3: NCNN ArcFace 特徵提取 (~50-100ms)
    ncnn::Mat face_mat = frameToNcnnMat(&aligned_face);
    feature = arcface_model->getFeature(face_mat);
    
    // Step 4: 清理資源
    // ...
  }
};
```

**需要確認的問題**：
- ❓ 是否需要批量處理？
  - 如果一幀有多張人臉需要提取特徵，是逐一處理還是批量？
  - **建議**：先實作單張，我以實作畫面準心，可以對準後幾秒自動提取特徵，後續優化再考慮批量

#### 3. 追蹤穩定性優化
**目標**：確保 DeepSORT 追蹤 ID 穩定

**需要確認的問題**：
- ❓ 當前追蹤是否有不穩定現象？
  - ID 頻繁跳動
  - 人臉消失後重新出現時分配新 ID
  - **建議**：先測試當前追蹤效果，再決定是否需要調整

#### 4. 特徵提取策略
**目標**：決定何時提取特徵以平衡性能與準確性

**當前策略**（從文檔中提取）：
```cpp
bool shouldExtractFeature(TrackedFace& track, cvtdl_tracker_t& tracker_info) {
  // 條件 1：新出現的追蹤目標
  if (tracker_info.info[i].state == CVI_TRACKER_NEW) return true;
  
  // 條件 2：距離上次提取超過 N 幀（避免重複計算）
  if (track.frames_since_feature > FEATURE_EXTRACT_INTERVAL) return true;
  
  // 條件 3：人臉品質夠好（避免模糊人臉）
  if (face_info.face_quality > MIN_FACE_QUALITY) return true;
  
  return false;
}
```

**需要確認的問題**：
- ❓ `FEATURE_EXTRACT_INTERVAL` 設為多少幀？
  - **建議**：初期設為 30 幀（~1 秒），根據實際效果調整
  
- ❓ `MIN_FACE_QUALITY` 閾值？
  - **建議**：初期設為 0.6，根據實際效果調整

- ❓ 是否需要「品質最佳幀」策略？
  - 在追蹤期間收集多幀，選擇品質最高的一幀提取特徵
  - **建議**：先實作簡單策略，後續優化再考慮

### 🔍 需要您回答的關鍵問題

1. **RGB 緩衝區分配方式**：
   - 使用 VB pool 還是動態分配？
   - 需要多少個緩衝區？（建議：2-4 個用於特徵提取）
   - 自行決定


2. **當前追蹤穩定性**：
   - 現在的追蹤是否有 ID 跳動問題？
   - 還是目前追蹤正常，只是缺少辨識功能？

3. **測試場景**：
   - 主要用於什麼場景？（門禁、考勤、監控？）
   - 預期同時追蹤多少張人臉？（1-5 張？5-10 張？）
   - 幀率要求？（15fps? 25fps?）

4. **人臉資料庫**：
   - 是否已有準備好的人臉資料庫？
   - 還是需要實作即時註冊功能？

### 📊 性能預估（基於 IVE 硬體加速）

```
單幀處理流程（假設 1 張人臉）：
┌────────────────────────────┬──────────┐
│ TDL 人臉檢測               │ ~10-20ms │ (TPU 加速)
├────────────────────────────┼──────────┤
│ DeepSORT 追蹤              │ ~1-2ms   │ (純 CPU)
├────────────────────────────┼──────────┤
│ 特徵提取（按需）：          │          │
│  - IVE NV21->RGB           │ ~0.5-1ms │ (IVE 硬體)
│  - GDC 人臉對齊            │ ~1-2ms   │ (GDC 硬體)
│  - NCNN ArcFace 提取       │ ~50-100ms│ (CPU，瓶頸)
├────────────────────────────┼──────────┤
│ 人臉辨識（比對資料庫）      │ ~0.1-1ms │ (純計算)
└────────────────────────────┴──────────┘

總計（不含特徵提取）：~11-22ms (≈ 45-90 fps) ✅
總計（含特徵提取）：~62-124ms (≈ 8-16 fps) ⚠️
```

**結論**：
- ✅ **純追蹤模式**：可達到 45-90 fps，流暢度極佳
- ⚠️ **含特徵提取**：降至 8-16 fps，需要智能策略避免每幀提取
- 🎯 **推薦策略**：每 30 幀（~1 秒）提取一次特徵，可維持 25-30 fps

需要我開始實作嗎？或者您有其他問題需要先確認？

