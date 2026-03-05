#include "face_feature_extractor.h"
#include "feature_extractor_helper.hpp"

FaceFeatureExtractor::FaceFeatureExtractor(
    const std::string& cvimodel_path,
    cvitdl_handle_t tdl_handle)
    : arcface_model_(nullptr),
      arc_in_(nullptr),
      arc_out_(nullptr),
      arc_in_num_(0),
      arc_out_num_(0),
      tdl_handle_(tdl_handle),
      model_loaded_(false),
      feature_dim_(0),
      face_h_(112),
      face_w_(112) {
    
    // 載入 CVI ArcFace 模型
    CVI_RC ret = CVI_NN_RegisterModel(cvimodel_path.c_str(), &arcface_model_);
    if (ret != CVI_RC_SUCCESS) {
        std::cerr << "❌ Failed to load ArcFace cvimodel: " << cvimodel_path 
                  << " (err=" << ret << ")" << std::endl;
        return;
    }
    
    // 取得輸入/輸出 tensor
    ret = CVI_NN_GetInputOutputTensors(arcface_model_,
                                       &arc_in_, &arc_in_num_,
                                       &arc_out_, &arc_out_num_);
    if (ret != CVI_RC_SUCCESS) {
        std::cerr << "❌ Failed to get ArcFace tensors" << std::endl;
        CVI_NN_CleanupModel(arcface_model_);
        arcface_model_ = nullptr;
        return;
    }
    
    // 從模型推導輸入尺寸和特徵維度
    CVI_SHAPE in_shape = CVI_NN_TensorShape(&arc_in_[0]);
    int N, C;
    get_tensor_dims(in_shape, N, C, face_h_, face_w_);
    
    feature_dim_ = (int)CVI_NN_TensorCount(&arc_out_[0]);
    
    model_loaded_ = true;
    std::cout << "✅ ArcFace TPU model loaded successfully" << std::endl;
    std::cout << "   - Path: " << cvimodel_path << std::endl;
    std::cout << "   - Input: " << face_w_ << "x" << face_h_ << std::endl;
    std::cout << "   - Feature dim: " << feature_dim_ << std::endl;
}

FaceFeatureExtractor::~FaceFeatureExtractor() {
    if (arcface_model_) {
        CVI_NN_CleanupModel(arcface_model_);
        arcface_model_ = nullptr;
    }
}

CVI_S32 FaceFeatureExtractor::extractFeature(
    VIDEO_FRAME_INFO_S* frame,
    cvtdl_face_info_t* face_info,
    std::vector<float>& feature) {
    
    if (!model_loaded_) {
        std::cerr << "❌ ArcFace model not loaded" << std::endl;
        return CVI_FAILURE;
    }
    
    // 檢查輸入幀是否有 5 個關鍵點
    if (face_info->pts.size != 5) {
        std::cerr << "❌ Face info missing landmarks (need 5 points, got " 
                  << (int)face_info->pts.size << ")" << std::endl;
        return CVI_FAILURE;
    }
    
    // 1. 將 NV21 幀轉換為 RGB buffer
    int img_w, img_h;
    unsigned char* rgb_full = nv21FrameToRGB(frame, img_w, img_h);
    if (!rgb_full) {
        std::cerr << "❌ Failed to convert frame to RGB" << std::endl;
        return CVI_FAILURE;
    }
    
    // 2. 準備 landmarks (格式: [x0,y0, x1,y1, ..., x4,y4])
    float landmarks[10];
    for (int i = 0; i < 5; i++) {
        landmarks[2 * i]     = face_info->pts.x[i];
        landmarks[2 * i + 1] = face_info->pts.y[i];
    }
    
    // 3. 計算仿射變換矩陣
    float M[6];
    computeAffineMatrix(landmarks, M);
    
    // 4. 執行仿射變換 → 對齊到 face_h_ x face_w_ (通常 112x112)
    unsigned char* aligned = new unsigned char[face_w_ * face_h_ * 3];
    warpAffine(rgb_full, img_w, img_h, aligned, face_w_, face_h_, M);
    
    // 釋放原始 RGB
    delete[] rgb_full;
    
    // 5. 填入 ArcFace 輸入 tensor
    fill_tensor_rgb(&arc_in_[0], aligned, face_w_, face_h_);
    
    // 釋放對齊後的 RGB
    delete[] aligned;
    
    // 6. TPU 推理
    CVI_RC ret = CVI_NN_Forward(arcface_model_, arc_in_, arc_in_num_,
                                 arc_out_, arc_out_num_);
    if (ret != CVI_RC_SUCCESS) {
        std::cerr << "❌ ArcFace TPU inference failed (err=" << ret << ")" << std::endl;
        return CVI_FAILURE;
    }
    
    // 7. 取得特徵向量（自動處理量化）
    get_float_data(&arc_out_[0], feature);
    
    // 8. L2 正規化
    normalize(feature);
    
    return CVI_SUCCESS;
}