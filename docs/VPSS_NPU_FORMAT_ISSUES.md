# VPSS 與 NPU 影像格式處理機制分析

本文件紀錄在整合「遠端下載照片進行人臉註冊」時，遭遇的 VPSS (Video Process Subsystem) 綁定崩潰問題，以及 CV181X NPU 在處理影像格式 (YUV/RGB) 時的底層邏輯與限制。

## 問題描述與根本原因 (Root Cause)

在進行遠端靜態照片辨識時，系統連續出現 `CVI_TDL_ERR_VPSS_SEND_FRAME` (0xc0010108) 與 `CVI_TDL_ERR_INIT_VPSS` (0xc0010107) 錯誤，甚至導致主攝影機執行緒同時崩潰。

### 1. VPSS 硬體輸入格式限制
CV181X 的 VPSS 硬體通道在設計上主要用於處理攝影機視訊串流，其**輸入端 (Input) 嚴格限制只接受 YUV 格式 (如 NV21, NV12)**，無法直接接收 RGB 交錯格式 (Interleaved RGB)。如果強行餵入 RGB 記憶體，硬體將直接報錯拒收。

### 2. 硬體連線鎖死 (Bind Constraint)
在系統初始化 (`system_init.cpp`) 時，`VPSS Grp 0` 已經與內部感測器 (VI, Video Input) 進行了硬體級別的強制綁定 (`bBindVI = true`)。
- **後果**：當 VPSS Group 被綁定給硬體 VI 後，它將**拒絕任何來自軟體端 (`CVI_VPSS_SendFrame`) 的記憶體寫入請求**。
- **衝突**：我們嘗試用主執行緒的 `tdlHandle` (預設走 Grp 0) 去處理下載的背景照片，導致記憶體寫入與攝影機硬體串流發生嚴重衝撞，進而引發核心崩潰。

### 3. TDL SDK 單例模型快取限制
我們曾嘗試建立第二個獨立的 `bg_handle` (走 VPSS Grp 1) 來避開 Grp 0，但 CVITDL SDK 對於同一個模型 (如 `CVI_TDL_SUPPORTED_MODEL_SCRFDFACE`) 在同一個 Process 中採用了全域單例指針。
- **後果**：動態創建與銷毀 `bg_handle` 時，SDK 會連帶將 NPU 中正在被 `tdlHandle` 使用的模型快取給拔除，導致主畫面偵測瞬間踩空當機。

---

## 歷史機制解析：為何過往不曾發生格式問題？

您可能會好奇：「過往直接吃攝影機的 YUV (NV21) 格式，為什麼不論是人臉偵測 (SCRFDFace) 還是特徵提取 (ArcFace) 都能正常運作？他們相容 RGB 嗎？還是有經過特殊轉換？」

答案是：**這兩者都有經過轉換，只是一個是硬體代勞，一個是您親手寫的軟體代勞。**

### 1. 偵測模型 (SCRFDFace) 的轉換機制：VPSS 硬體代勞
- NPU 裡面的 SCRFDFace 模型，底層要求輸入的其實是 **`BGR_888_PLANAR` (RGB 的連續平面格式)**，並非 YUV。
- **運作流程**：攝影機吐出 `NV21` (YUV) $\rightarrow$ 送入 VPSS $\rightarrow$ VPSS 在進行硬體縮放的同時，**自動將其色彩空間轉換為 `BGR_888_PLANAR`** $\rightarrow$ 輸出並餵給 NPU。
- **結論**：因為我們在 `system_init.cpp` 中將 VPSS Chn 的輸出格式設為了 `PIXEL_FORMAT_BGR_888_PLANAR`，硬體默默幫我們把 YUV 轉成了模型要的 RGB/BGR 格式，所以您過去從未感覺到格式衝突。

### 2. 辨識模型 (ArcFace) 的轉換機制：您的 CPU 軟體代勞
- ArcFace 同樣需要 RGB/BGR 格式來進行特徵提取。
- **運作流程**：在 `face_feature_extractor.cpp` 中，您並未依賴 VPSS 進行處理。如果您回去檢視程式碼，會發現您親自寫了一支 `nv21FrameToRGB()` 函數！
- **結論**：每當需要提取特徵時，您的程式會在 CPU 上把攝影機的 `NV21` 記憶體轉換為 RGB Buffer，接著利用 `warpAffine` 進行人臉裁切與對齊，最後才透過 `CVI_NN_Forward` 餵給模型。您已經在不知不覺中完美處理了 YUV 到 RGB 的軟體空間轉換。

---

## 解決方針紀錄

### ✅ 最終採用：`CVI_TDL_ReadImage` + `imgprocess_t`（官方 API）

透過 `cvi_tdl_media.h` 提供的 `CVI_TDL_ReadImage` API，使用獨立的 `imgprocess_t` 影像處理管線。
此管線內部使用 OpenCV 進行 JPEG 解碼和格式轉換，**完全不經過 VPSS 硬體**，因此不受 VI 綁定狀態影響。

```cpp
imgprocess_t img_handle = NULL;
CVI_TDL_Create_ImageProcessor(&img_handle);
CVI_TDL_ReadImage(img_handle, path, &frame, PIXEL_FORMAT_RGB_888_PLANAR);  // 不走 VPSS
// ... UnBind → FaceDetection → ReBind ...  // 只有推論需要 UnBind
CVI_TDL_ReleaseImage(img_handle, &frame);
CVI_TDL_Destroy_ImageProcessor(img_handle);
```

> **注意**：`CVI_TDL_FaceDetection` 仍需在 UnBind 狀態下呼叫，因為它內部透過 VPSS 做模型前處理。

### ❌ 已排除的方案

1. **`CVI_TDL_StbReadImage`**：內部使用 `CVI_VPSS_SendFrame` 做 RGB→NV21 轉換，VPSS 綁著 VI 時 SendFrame 被靜默拒絕（返回 SUCCESS 但 frame 為 0x0）。即使先 UnBind 再呼叫，在此 SDK 版本中仍不可靠。

2. **`CVI_VPSS_DisableChn` 排空管線**：VENC Thread 也在讀取 CHN0，DisableChn 導致 `GetChnFrame` 返回 `CVI_ERR_VPSS_NOTREADY (0xc0068010)` 而崩潰。

3. **純 CPU 推論替代 (Bypass VPSS)**：雖可行但過於複雜，需手動縮放至模型要求尺寸並跳過 VPSS 前處理。

