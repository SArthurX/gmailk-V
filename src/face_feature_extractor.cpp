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
    
    // 根據幀格式選擇正確的轉換路徑
    int img_w, img_h;
    unsigned char* rgb_full = nullptr;
    
    PIXEL_FORMAT_E fmt = frame->stVFrame.enPixelFormat;
    if (fmt == PIXEL_FORMAT_NV21 || fmt == PIXEL_FORMAT_NV12) {
        // 攝影機即時幀（NV21）
        rgb_full = nv21FrameToRGB(frame, img_w, img_h);
    } else if (fmt == PIXEL_FORMAT_RGB_888_PLANAR) {
        // CVI_TDL_ReadImage 載入的照片（RGB planar: R plane → G plane → B plane）
        img_w = frame->stVFrame.u32Width;
        img_h = frame->stVFrame.u32Height;
        
        bool need_unmap = false;
        if (!frame->stVFrame.pu8VirAddr[0]) {
            size_t total_size = frame->stVFrame.u32Length[0] + frame->stVFrame.u32Length[1] + frame->stVFrame.u32Length[2];
            frame->stVFrame.pu8VirAddr[0] = (CVI_U8*)CVI_SYS_Mmap(
                frame->stVFrame.u64PhyAddr[0], total_size);
            if (!frame->stVFrame.pu8VirAddr[0]) {
                std::cerr << "❌ Failed to map RGB planar frame memory" << std::endl;
                return CVI_FAILURE;
            }
            frame->stVFrame.pu8VirAddr[1] = frame->stVFrame.pu8VirAddr[0] + frame->stVFrame.u32Length[0];
            frame->stVFrame.pu8VirAddr[2] = frame->stVFrame.pu8VirAddr[1] + frame->stVFrame.u32Length[1];
            need_unmap = true;
        }
        
        uint32_t stride = frame->stVFrame.u32Stride[0];
        rgb_full = new unsigned char[img_w * img_h * 3];
        const uint8_t* r_plane = frame->stVFrame.pu8VirAddr[0];
        const uint8_t* g_plane = frame->stVFrame.pu8VirAddr[1];
        const uint8_t* b_plane = frame->stVFrame.pu8VirAddr[2];
        
        for (int y = 0; y < img_h; y++) {
            for (int x = 0; x < img_w; x++) {
                int src_idx = y * stride + x;
                int dst_idx = (y * img_w + x) * 3;
                rgb_full[dst_idx + 0] = r_plane[src_idx];
                rgb_full[dst_idx + 1] = g_plane[src_idx];
                rgb_full[dst_idx + 2] = b_plane[src_idx];
            }
        }
        
        if (need_unmap) {
            size_t total_size = frame->stVFrame.u32Length[0] + frame->stVFrame.u32Length[1] + frame->stVFrame.u32Length[2];
            CVI_SYS_Munmap((void*)frame->stVFrame.pu8VirAddr[0], total_size);
            frame->stVFrame.pu8VirAddr[0] = nullptr;
            frame->stVFrame.pu8VirAddr[1] = nullptr;
            frame->stVFrame.pu8VirAddr[2] = nullptr;
        }
    } else {
        std::cerr << "❌ Unsupported pixel format for feature extraction: " << fmt << std::endl;
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