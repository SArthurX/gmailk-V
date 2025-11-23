#include "face_feature_extractor.h"
#include <iostream>
#include <cstring>
#include <cmath>

// Disable RVV support to avoid compilation issues
#ifdef __riscv_vector
#undef __riscv_vector
#endif

#include "net.h"
#include "mat.h"

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
    
    
    // === 使用仿射變換對齊（替代 CVI_TDL_FaceAlignment） ===
    
    // 1. 將整個NV21幀轉換為RGB Mat
    ncnn::Mat full_rgb = nv21FrameToNcnnMat(frame);
    if (full_rgb.empty()) {
        std::cerr << "❌ Failed to convert frame to RGB" << std::endl;
        return CVI_FAILURE;
    }
    
    // 2. 準備源關鍵點（從face_info）和目標關鍵點（ArcFace標準）
    float src_5pts[10];  // [x0,x1,x2,x3,x4, y0,y1,y2,y3,y4]
    for (int i = 0; i < 5; i++) {
        src_5pts[i] = face_info->pts.x[i];      // x coordinates
        src_5pts[i + 5] = face_info->pts.y[i];  // y coordinates
    }
    
    // ArcFace標準關鍵點位置（112x112圖像）
    // 這些是InsightFace使用的標準位置
    const float dst_5pts[10] = {
        38.2946f, 73.5318f, 56.0252f, 41.5493f, 70.7299f,  // x coordinates
        51.6963f, 51.5014f, 71.7366f, 92.3655f, 92.2041f   // y coordinates
    };
    
    // 3. 計算仿射變換矩陣
    float M[6];
    getAffineMatrix(src_5pts, dst_5pts, M);
    
    // 4. 執行仿射變換，對齊到112x112
    ncnn::Mat rgb_mat;
    warpAffineMatrix(full_rgb, rgb_mat, M, 112, 112);
    
    if (rgb_mat.empty()) {
        std::cerr << "❌ Failed to warp face to 112x112" << std::endl;
        return CVI_FAILURE;
    }
    if (rgb_mat.empty()) {
        std::cerr << "❌ Failed to warp face to 112x112" << std::endl;
        return CVI_FAILURE;
    }
    
    // 5. 調用 ArcFace 提取特徵
    // 預處理：標準化到 [-1, 1]
    const float mean_vals[3] = {127.5f, 127.5f, 127.5f};
    const float norm_vals[3] = {1.0f/127.5f, 1.0f/127.5f, 1.0f/127.5f};
    rgb_mat.substract_mean_normalize(mean_vals, norm_vals);
    
    ncnn::Extractor ex = arcface_net_->create_extractor();
    ex.set_light_mode(true);
    ex.input("data", rgb_mat);
    
    ncnn::Mat out;
    CVI_S32 ret = ex.extract("fc1", out);
    
    if (ret != 0) {
        std::cerr << "❌ ArcFace 特徵提取失敗: " << ret << std::endl;
        return CVI_FAILURE;
    }
    
    // 6. 複製特徵並正規化
    feature.resize(feature_dim_);
    for (int i = 0; i < feature_dim_; i++) {
        feature[i] = out[i];
    }
    normalize(feature);
    
    return CVI_SUCCESS;
}

ncnn::Mat FaceFeatureExtractor::nv21FrameToNcnnMat(VIDEO_FRAME_INFO_S* frame) {
    int width = frame->stVFrame.u32Width;
    int height = frame->stVFrame.u32Height;
    
    // 檢查虛擬地址是否已映射
    bool need_unmap = false;
    if (!frame->stVFrame.pu8VirAddr[0]) {
        size_t total_size = frame->stVFrame.u32Length[0] + frame->stVFrame.u32Length[1];
        frame->stVFrame.pu8VirAddr[0] = (CVI_U8*)CVI_SYS_Mmap(
            frame->stVFrame.u64PhyAddr[0], total_size);
        if (!frame->stVFrame.pu8VirAddr[0]) {
            std::cerr << "❌ Failed to map frame memory" << std::endl;
            return ncnn::Mat();
        }
        frame->stVFrame.pu8VirAddr[1] = frame->stVFrame.pu8VirAddr[0] + frame->stVFrame.u32Length[0];
        need_unmap = true;
    } else {
    }
    
    // 分配 RGB 緩衝區
    unsigned char* rgb_data = new unsigned char[width * height * 3];
    
    // NV21 → RGB 轉換
    convertNV21ToRGB(
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
    
    // 如果我們映射了記憶體，需要解除映射
    if (need_unmap) {
        size_t total_size = frame->stVFrame.u32Length[0] + frame->stVFrame.u32Length[1];
        CVI_SYS_Munmap((void*)frame->stVFrame.pu8VirAddr[0], total_size);
        frame->stVFrame.pu8VirAddr[0] = nullptr;
        frame->stVFrame.pu8VirAddr[1] = nullptr;
    }
    
    return result;
}

void FaceFeatureExtractor::convertNV21ToRGB(
    const unsigned char* nv21_data,
    int width, int height,
    unsigned char* rgb_data) {
    
    // NV21 格式: YYYYYYYY VU VU VU VU
    // Y plane: width * height
    // VU plane: width * height / 2 (交錯存儲)
    
    const unsigned char* y_plane = nv21_data;
    const unsigned char* vu_plane = nv21_data + width * height;
    
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            int y_index = i * width + j;
            int uv_index = (i / 2) * width + (j / 2) * 2;
            
            int Y = y_plane[y_index];
            int V = vu_plane[uv_index];
            int U = vu_plane[uv_index + 1];
            
            // YUV to RGB conversion
            // R = Y + 1.402 * (V - 128)
            // G = Y - 0.344 * (U - 128) - 0.714 * (V - 128)
            // B = Y + 1.772 * (U - 128)
            
            int R = Y + ((1436 * (V - 128)) >> 10);
            int G = Y - ((352 * (U - 128) + 731 * (V - 128)) >> 10);
            int B = Y + ((1815 * (U - 128)) >> 10);
            
            // Clamp to [0, 255]
            R = R < 0 ? 0 : (R > 255 ? 255 : R);
            G = G < 0 ? 0 : (G > 255 ? 255 : G);
            B = B < 0 ? 0 : (B > 255 ? 255 : B);
            
            // RGB 輸出
            int rgb_index = (i * width + j) * 3;
            rgb_data[rgb_index] = R;
            rgb_data[rgb_index + 1] = G;
            rgb_data[rgb_index + 2] = B;
        }
    }
}

void FaceFeatureExtractor::normalize(std::vector<float>& feature) {
    float sum = 0.0f;
    for (auto it = feature.begin(); it != feature.end(); it++) {
        sum += (*it) * (*it);
    }
    sum = sqrt(sum);
    
    if (sum > 0) {
        for (auto it = feature.begin(); it != feature.end(); it++) {
            *it /= sum;
        }
    }
}

// ========== Affine Transform Functions (from test/base.cpp) ==========

void FaceFeatureExtractor::getAffineMatrix(float* src_5pts, const float* dst_5pts, float* M) {
    float src[10], dst[10];
    memcpy(src, src_5pts, sizeof(float) * 10);
    memcpy(dst, dst_5pts, sizeof(float) * 10);

    float ptmp[2];
    ptmp[0] = ptmp[1] = 0;
    for (int i = 0; i < 5; ++i) {
        ptmp[0] += src[i];
        ptmp[1] += src[i + 5];
    }
    ptmp[0] /= 5;
    ptmp[1] /= 5;

    float _a = 0, _b = 0;
    for (int i = 0; i < 5; ++i) {
        src[i] -= ptmp[0];
        src[i + 5] -= ptmp[1];
        _a += (src[i] * src[i] + src[i + 5] * src[i + 5]);
        _b += (dst[i] * dst[i] + dst[i + 5] * dst[i + 5]);
    }
    float square_sum = _a;
    float scale = sqrt(_b / _a);

    float pts0[2], pts1[10];
    pts0[0] = pts0[1] = 0;

    float sqloss = 1e8;
    for (int i = 0; i < 200; ++i) {
        _a = 0;
        _b = 0;
        for (int i = 0; i < 5; ++i) {
            _a += ((pts0[0] - dst[i]) * src[i + 5] - (pts0[1] - dst[i + 5]) * src[i]);
            _b += ((pts0[0] - dst[i]) * src[i] + (pts0[1] - dst[i + 5]) * src[i + 5]);
        }
        if (_b < 0) {
            _b = -_b;
            _a = -_a;
        }
        float _s = sqrt(_a * _a + _b * _b);
        _b /= _s;
        _a /= _s;

        for (int i = 0; i < 5; ++i) {
            pts1[i] = scale * (src[i] * _b + src[i + 5] * _a);
            pts1[i + 5] = scale * (-src[i] * _a + src[i + 5] * _b);
        }

        float _scale = 0;
        for (int i = 0; i < 5; ++i) {
            _scale += ((dst[i] - pts0[0]) * pts1[i] + (dst[i + 5] - pts0[1]) * pts1[i + 5]);
        }
        _scale /= (square_sum * scale);
        for (int i = 0; i < 10; ++i) {
            pts1[i] *= (_scale / scale);
        }
        scale = _scale;

        pts0[0] = pts0[1] = 0;
        for (int i = 0; i < 5; ++i) {
            pts0[0] += (dst[i] - pts1[i]);
            pts0[1] += (dst[i + 5] - pts1[i + 5]);
        }
        pts0[0] /= 5;
        pts0[1] /= 5;

        float _sqloss = 0;
        for (int i = 0; i < 5; ++i) {
            _sqloss += ((pts0[0] + pts1[i] - dst[i]) * (pts0[0] + pts1[i] - dst[i]) +
                       (pts0[1] + pts1[i + 5] - dst[i + 5]) * (pts0[1] + pts1[i + 5] - dst[i + 5]));
        }
        if (abs(_sqloss - sqloss) < 1e-2) {
            break;
        }
        sqloss = _sqloss;
    }

    for (int i = 0; i < 5; ++i) {
        pts1[i] += (pts0[0] + ptmp[0]);
        pts1[i + 5] += (pts0[1] + ptmp[1]);
    }

    M[0] = _b * scale;
    M[1] = _a * scale;
    M[3] = -_a * scale;
    M[4] = _b * scale;
    M[2] = pts0[0] + ptmp[0] - scale * (ptmp[0] * _b + ptmp[1] * _a);
    M[5] = pts0[1] + ptmp[1] - scale * (-ptmp[0] * _a + ptmp[1] * _b);
}

void FaceFeatureExtractor::warpAffineMatrix(ncnn::Mat src, ncnn::Mat& dst, float* M, int dst_w, int dst_h) {
    int src_w = src.w;
    int src_h = src.h;

    unsigned char* src_u = new unsigned char[src_w * src_h * 3]{0};
    unsigned char* dst_u = new unsigned char[dst_w * dst_h * 3]{0};

    src.to_pixels(src_u, ncnn::Mat::PIXEL_RGB);

    float m[6];
    for (int i = 0; i < 6; i++)
        m[i] = M[i];
    float D = m[0] * m[4] - m[1] * m[3];
    D = D != 0 ? 1. / D : 0;
    float A11 = m[4] * D, A22 = m[0] * D;
    m[0] = A11;
    m[1] *= -D;
    m[3] *= -D;
    m[4] = A22;
    float b1 = -m[0] * m[2] - m[1] * m[5];
    float b2 = -m[3] * m[2] - m[4] * m[5];
    m[2] = b1;
    m[5] = b2;

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            float fx = m[0] * x + m[1] * y + m[2];
            float fy = m[3] * x + m[4] * y + m[5];

            int sy = (int)floor(fy);
            fy -= sy;
            if (sy < 0 || sy >= src_h) continue;

            short cbufy[2];
            cbufy[0] = (short)((1.f - fy) * 2048);
            cbufy[1] = 2048 - cbufy[0];

            int sx = (int)floor(fx);
            fx -= sx;
            if (sx < 0 || sx >= src_w) continue;

            short cbufx[2];
            cbufx[0] = (short)((1.f - fx) * 2048);
            cbufx[1] = 2048 - cbufx[0];

            if (sy == src_h - 1 || sx == src_w - 1)
                continue;
            for (int c = 0; c < 3; c++) {
                dst_u[3 * (y * dst_w + x) + c] =
                    (src_u[3 * (sy * src_w + sx) + c] * cbufx[0] * cbufy[0] +
                     src_u[3 * ((sy + 1) * src_w + sx) + c] * cbufx[0] * cbufy[1] +
                     src_u[3 * (sy * src_w + sx + 1) + c] * cbufx[1] * cbufy[0] +
                     src_u[3 * ((sy + 1) * src_w + sx + 1) + c] * cbufx[1] * cbufy[1]) >> 22;
            }
        }
    }

    dst = ncnn::Mat::from_pixels(dst_u, ncnn::Mat::PIXEL_BGR, dst_w, dst_h);
    delete[] src_u;
    delete[] dst_u;
}
