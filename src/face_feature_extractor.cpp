#include "face_feature_extractor.h"
#include "feature_extractor_helper.hpp"

FaceFeatureExtractor::FaceFeatureExtractor(
    const std::string& model_param, 
    const std::string& model_bin,
    cvitdl_handle_t tdl_handle)
    : tdl_handle_(tdl_handle),
      use_gdc_(true),
      model_loaded_(false),
      arcface_net_(nullptr) {
    
    // 初始化 NCNN ArcFace 模型
    arcface_net_ = new ncnn::Net();
    
    // 載入模型
    int ret_param = arcface_net_->load_param(model_param.c_str());
    int ret_model = arcface_net_->load_model(model_bin.c_str());
    
    if (ret_param == 0 && ret_model == 0) {
        model_loaded_ = true;
        std::cout << "✅ ArcFace model loaded successfully" << std::endl;
        std::cout << "   - Param: " << model_param << std::endl;
        std::cout << "   - Model: " << model_bin << std::endl;
    } else {
        std::cerr << "❌ Failed to load ArcFace model" << std::endl;
        std::cerr << "   - Param ret: " << ret_param << std::endl;
        std::cerr << "   - Model ret: " << ret_model << std::endl;
    }
}

FaceFeatureExtractor::~FaceFeatureExtractor() {
    if (arcface_net_) {
        arcface_net_->clear();
        delete arcface_net_;
        arcface_net_ = nullptr;
    }
}

CVI_S32 FaceFeatureExtractor::extractFeature(
    VIDEO_FRAME_INFO_S* frame,
    cvtdl_face_info_t* face_info,
    std::vector<float>& feature) {
    
    
    if (!model_loaded_) {
        std::cerr << "❌ Model not loaded" << std::endl;
        return CVI_FAILURE;
    }
    
    // 檢查輸入幀是否有5個關鍵點
    if (face_info->pts.size != 5) {
        std::cerr << "❌ Face info missing landmarks (need 5 points, got " << (int)face_info->pts.size << ")" << std::endl;
        return CVI_FAILURE;
    }
    
    // 調試：輸出關鍵點位置
    std::cout << "  Landmarks: ";
    for (int i = 0; i < 5; i++) {
        std::cout << "(" << face_info->pts.x[i] << "," << face_info->pts.y[i] << ") ";
    }
    std::cout << std::endl;
    
    
    // === 使用仿射變換對齊（替代 CVI_TDL_FaceAlignment） ===
    
    // 1. 將整個NV21幀轉換為RGB Mat
    std::cout << "  Frame addr: " << (void*)frame 
              << ", PhyAddr: 0x" << std::hex << frame->stVFrame.u64PhyAddr[0] << std::dec
              << ", Size: " << frame->stVFrame.u32Width << "x" << frame->stVFrame.u32Height << std::endl;
    
    ncnn::Mat full_rgb = nv21FrameToNcnnMat(frame);
    if (full_rgb.empty()) {
        std::cerr << "❌ Failed to convert frame to RGB" << std::endl;
        return CVI_FAILURE;
    }
    
    // 2. 準備源關鍵點（從face_info）和目標關鍵點（ArcFace標準）
    // 注意：源關鍵點使用原始圖像座標（在1280x720圖像上）
    float src_5pts[10];  // [x0,x1,x2,x3,x4, y0,y1,y2,y3,y4]
    for (int i = 0; i < 5; i++) {
        src_5pts[i] = face_info->pts.x[i];      // x coordinates
        src_5pts[i + 5] = face_info->pts.y[i];  // y coordinates
    }
    
    std::cout << "  [DEBUG] src_5pts: [";
    for (int i = 0; i < 10; i++) {
        std::cout << src_5pts[i];
        if (i < 9) std::cout << ", ";
    }
    std::cout << "]" << std::endl;
    
    // ArcFace標準關鍵點位置（112x112圖像上的目標位置）
    const float dst_5pts[10] = {
        38.2946f, 73.5318f, 56.0252f, 41.5493f, 70.7299f,  // x coordinates
        51.6963f, 51.5014f, 71.7366f, 92.3655f, 92.2041f   // y coordinates
    };
    
    std::cout << "  [DEBUG] dst_5pts: [";
    for (int i = 0; i < 10; i++) {
        std::cout << dst_5pts[i];
        if (i < 9) std::cout << ", ";
    }
    std::cout << "]" << std::endl;
    
    // 3. 計算仿射變換矩陣
    // 注意：getAffineMatrix 計算的是 src→dst 的變換
    // 但 warpAffineMatrix 內部會計算逆矩陣來做反向採樣
    // 所以參數順序是正確的：src (大圖關鍵點) → dst (小圖關鍵點)
    float M[6];
    getAffineMatrix(src_5pts, dst_5pts, M);
    
    // 調試：輸出變換矩陣（僅顯示前2個參數以檢查是否變化）
    std::cout << "  Affine Matrix (M[0], M[1]): " << M[0] << ", " << M[1] << std::endl;
    
    // 驗證：計算第一個關鍵點的變換結果
    float test_x = M[0] * src_5pts[0] + M[1] * src_5pts[5] + M[2];
    float test_y = M[3] * src_5pts[0] + M[4] * src_5pts[5] + M[5];
    std::cout << "  [VERIFY] src(" << src_5pts[0] << "," << src_5pts[5] 
              << ") -> dst(" << test_x << "," << test_y 
              << "), expected(" << dst_5pts[0] << "," << dst_5pts[5] << ")" << std::endl;
    
    // 4. 執行仿射變換，對齊到112x112
    ncnn::Mat rgb_mat;  // 確保是空的
    rgb_mat.release();  // 釋放舊資料
    warpAffineMatrix(full_rgb, rgb_mat, M, 112, 112);
    
    if (rgb_mat.empty()) {
        std::cerr << "❌ Failed to warp face to 112x112" << std::endl;
        return CVI_FAILURE;
    }
    
    // 調試：檢查 warp 後的原始數據
    std::cout << "  [DEBUG] Warped mat (first 5 pixels R channel): ";
    for (int k = 0; k < 5; k++) {
        std::cout << (int)((unsigned char*)rgb_mat.data)[k*3] << " ";
    }
    std::cout << std::endl;
    
    // 5. 調用 ArcFace 提取特徵
    // 注意：模型內部已經包含預處理 (x - 127.5) * 0.007812
    // 所以這裡不需要再做預處理，直接傳入 0-255 的原始像素值
    
    // 調試：檢查輸入數據（應該是 0-255 範圍）
    std::cout << "  [DEBUG] Input mat size: " << rgb_mat.w << "x" << rgb_mat.h 
              << ", channels: " << rgb_mat.c << std::endl;
    std::cout << "  [DEBUG] Input data (first 5 pixels, should be 0-255): ";
    for (int k = 0; k < 5; k++) {
        std::cout << (int)((unsigned char*)rgb_mat.data)[k*3] << " ";
    }
    std::cout << std::endl;
    
    // 每次都創建新的 Extractor（避免狀態殘留）
    ncnn::Extractor ex = arcface_net_->create_extractor();
    ex.set_light_mode(true);
    
    // 關鍵：確保輸入是新的數據
    int ret_input = ex.input("data", rgb_mat);
    if (ret_input != 0) {
        std::cerr << "❌ Failed to input data to extractor: " << ret_input << std::endl;
        return CVI_FAILURE;
    }
    
    ncnn::Mat out;
    CVI_S32 ret = ex.extract("fc1", out);
    
    if (ret != 0) {
        std::cerr << "❌ ArcFace 特徵提取失敗: " << ret << std::endl;
        return CVI_FAILURE;
    }
    
    // 調試：檢查輸出
    std::cout << "  [DEBUG] Output mat size: " << out.w << ", type: " << out.elemsize << std::endl;
    std::cout << "  [DEBUG] Output raw data (first 10): ";
    for (int k = 0; k < std::min(10, out.w); k++) {
        std::cout << out[k] << " ";
    }
    std::cout << std::endl;
    
    // 6. 複製特徵並正規化
    feature.resize(feature_dim_);
    for (int i = 0; i < feature_dim_; i++) {
        feature[i] = out[i];
    }
    
    // 調試：正規化前的數據
    std::cout << "  [DEBUG] Before normalize (first 5): ";
    for (int k = 0; k < 5; k++) {
        std::cout << feature[k] << " ";
    }
    std::cout << std::endl;
    
    normalize(feature);
    
    // 調試：正規化後的數據
    std::cout << "  [DEBUG] After normalize (first 5): ";
    for (int k = 0; k < 5; k++) {
        std::cout << feature[k] << " ";
    }
    std::cout << std::endl;
    
    return CVI_SUCCESS;
}