#ifndef FACE_FEATURE_EXTRACTOR_H
#define FACE_FEATURE_EXTRACTOR_H

#include <vector>
#include <string>
#include "cvi_tdl.h"

extern "C" {
#include <cvi_comm.h>
#include <cvi_sys.h>
#include <cvi_vb.h>
}

// Forward declaration for NCNN
namespace ncnn {
    class Net;
    class Mat;
}

/**
 * @brief 人臉特徵提取器
 * 
 * 功能：
 * 1. 使用官方 CVI_TDL_FaceAlignment 進行人臉對齊（GDC 硬體加速）
 * 2. 處理 NV21 格式轉換到 RGB 格式
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
     * @brief 檢查模型是否已載入
     */
    bool isLoaded() const { return model_loaded_; }

private:
    ncnn::Net* arcface_net_;           // NCNN ArcFace 模型
    cvitdl_handle_t tdl_handle_;       // TDL SDK 句柄
    bool use_gdc_;                      // 是否使用 GDC 硬體加速
    bool model_loaded_;                 // 模型是否載入成功
    
    const int feature_dim_ = 128;       // ArcFace 特徵維度
    const int align_size_ = 112;        // 對齊後人臉大小 112x112
    
    /**
     * @brief 使用官方 API 對齊人臉
     * @param inFrame 輸入幀（NV21）
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
     * @brief NV21 (YUV420SP) → RGB 轉換（通用實作，不依賴 NEON）
     * @param nv21_data NV21 數據指針
     * @param width 圖像寬度
     * @param height 圖像高度
     * @param rgb_data [輸出] RGB 數據（需預先分配 w*h*3 字節）
     */
    void convertNV21ToRGB(const unsigned char* nv21_data,
                         int width, int height,
                         unsigned char* rgb_data);
    
    /**
     * @brief L2 正規化特徵向量
     */
    void normalize(std::vector<float>& feature);
    
    /**
     * @brief 計算仿射變換矩陣（從 test/base.cpp 移植）
     * @param src_5pts 源5個關鍵點 [x0,x1,x2,x3,x4, y0,y1,y2,y3,y4]
     * @param dst_5pts 目標5個關鍵點（ArcFace標準位置）
     * @param M [輸出] 6參數仿射變換矩陣
     */
    void getAffineMatrix(float* src_5pts, const float* dst_5pts, float* M);
    
    /**
     * @brief 執行仿射變換（雙線性插值）
     * @param src 源圖像
     * @param dst [輸出] 目標圖像
     * @param M 仿射變換矩陣
     * @param dst_w 目標寬度
     * @param dst_h 目標高度
     */
    void warpAffineMatrix(ncnn::Mat src, ncnn::Mat& dst, float* M, int dst_w, int dst_h);
};

#endif // FACE_FEATURE_EXTRACTOR_H
