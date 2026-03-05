#ifndef FACE_FEATURE_EXTRACTOR_H
#define FACE_FEATURE_EXTRACTOR_H

#include <vector>
#include <string>
#include "cvi_tdl.h"
#include "cviruntime.h"

extern "C" {
#include <cvi_comm.h>
#include <cvi_sys.h>
#include <cvi_vb.h>
}

/**
 * @brief 人臉特徵提取器（CVI TPU 版本）
 * 
 * 功能：
 * 1. 使用 CVI TPU 硬體加速執行 ArcFace 模型推理
 * 2. 處理 NV21 格式轉換到 RGB 格式
 * 3. 使用 5 點 landmark similarity transform 對齊人臉至 112x112
 * 4. 提取特徵向量（維度由模型決定，通常為 512）
 */
class FaceFeatureExtractor {
public:
    /**
     * @brief 構造函數
     * @param cvimodel_path ArcFace .cvimodel 模型路徑
     * @param tdl_handle TDL SDK 句柄
     */
    FaceFeatureExtractor(const std::string& cvimodel_path,
                        cvitdl_handle_t tdl_handle);
    
    ~FaceFeatureExtractor();
    
    /**
     * @brief 從幀中提取指定人臉的特徵
     * @param frame 輸入幀（NV21 格式）
     * @param face_info 人臉信息（包含 bbox 和 5 個關鍵點）
     * @param feature [輸出] 特徵向量（已 L2 正規化）
     * @return CVI_SUCCESS 或錯誤碼
     */
    CVI_S32 extractFeature(VIDEO_FRAME_INFO_S* frame,
                          cvtdl_face_info_t* face_info,
                          std::vector<float>& feature);
    
    /**
     * @brief 檢查模型是否已載入
     */
    bool isLoaded() const { return model_loaded_; }

    /**
     * @brief 取得特徵維度
     */
    int getFeatureDim() const { return feature_dim_; }

private:
    CVI_MODEL_HANDLE arcface_model_;    // CVI TPU 模型句柄
    CVI_TENSOR *arc_in_;                // 輸入 tensor
    CVI_TENSOR *arc_out_;               // 輸出 tensor
    int32_t arc_in_num_;                // 輸入 tensor 數量
    int32_t arc_out_num_;               // 輸出 tensor 數量
    
    cvitdl_handle_t tdl_handle_;        // TDL SDK 句柄
    bool model_loaded_;                  // 模型是否載入成功
    
    int feature_dim_;                    // 特徵維度（從模型輸出推導）
    int face_h_;                         // 模型輸入高度
    int face_w_;                         // 模型輸入寬度
    const int align_size_ = 112;         // 對齊後人臉大小 112x112
    
    /**
     * @brief L2 正規化特徵向量
     */
    void normalize(std::vector<float>& feature);
};

#endif // FACE_FEATURE_EXTRACTOR_H
