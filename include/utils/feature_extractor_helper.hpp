#include "face_feature_extractor.h"
#include <iostream>

// Disable RVV support to avoid compilation issues
#ifdef __riscv_vector
#undef __riscv_vector
#endif

#include "net.h"
#include "mat.h"

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
        std::cout << "  [DEBUG] Mapped new frame memory" << std::endl;
    } else
        std::cout << "  [DEBUG] Using already mapped memory (VirAddr: " << (void*)frame->stVFrame.pu8VirAddr[0] << ")" << std::endl;
    
    // 調試：輸出前8個Y值
    std::cout << "  [DEBUG] Y data (first 8): ";
    for (int k = 0; k < 8; k++) {
        std::cout << (int)frame->stVFrame.pu8VirAddr[0][k] << " ";
    }
    std::cout << std::endl;
    
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

    // 計算 src 的中心點
    float ptmp[2];
    ptmp[0] = ptmp[1] = 0;
    for (int i = 0; i < 5; ++i) {
        ptmp[0] += src[i];
        ptmp[1] += src[i + 5];
    }
    ptmp[0] /= 5;
    ptmp[1] /= 5;
    
    // src 和 dst 都減去 src 的中心點（這是參考實現的方式）
    for (int i = 0; i < 5; ++i) {
        src[i] -= ptmp[0];
        src[i + 5] -= ptmp[1];
        dst[i] -= ptmp[0];
        dst[i + 5] -= ptmp[1];
    }

    // 計算初始旋轉角度和縮放比例
    float dst_x = (dst[3] + dst[4] - dst[0] - dst[1]) / 2;
    float dst_y = (dst[8] + dst[9] - dst[5] - dst[6]) / 2;
    float src_x = (src[3] + src[4] - src[0] - src[1]) / 2;
    float src_y = (src[8] + src[9] - src[5] - src[6]) / 2;
    float theta = atan2(dst_x, dst_y) - atan2(src_x, src_y);

    float scale = sqrt(pow(dst_x, 2) + pow(dst_y, 2)) / sqrt(pow(src_x, 2) + pow(src_y, 2));
    
    float pts1[10];
    float pts0[2];
    float _a = sin(theta), _b = cos(theta);
    pts0[0] = pts0[1] = 0;
    
    for (int i = 0; i < 5; ++i) {
        pts1[i] = scale * (src[i] * _b + src[i + 5] * _a);
        pts1[i + 5] = scale * (-src[i] * _a + src[i + 5] * _b);
        pts0[0] += (dst[i] - pts1[i]);
        pts0[1] += (dst[i + 5] - pts1[i + 5]);
    }
    pts0[0] /= 5;
    pts0[1] /= 5;

    float sqloss = 0;
    for (int i = 0; i < 5; ++i) {
        sqloss += ((pts0[0] + pts1[i] - dst[i]) * (pts0[0] + pts1[i] - dst[i]) +
                   (pts0[1] + pts1[i + 5] - dst[i + 5]) * (pts0[1] + pts1[i + 5] - dst[i + 5]));
    }

    float square_sum = 0;
    for (int i = 0; i < 10; ++i) {
        square_sum += src[i] * src[i];
    }
    
    // 迭代優化
    for (int t = 0; t < 200; ++t) {
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

    std::cout << "  [WARP DEBUG] src size: " << src_w << "x" << src_h << ", dst size: " << dst_w << "x" << dst_h << std::endl;
    std::cout << "  [WARP DEBUG] Transform matrix: M=[" << M[0] << "," << M[1] << "," << M[2] << "; " 
              << M[3] << "," << M[4] << "," << M[5] << "]" << std::endl;

    unsigned char* src_u = new unsigned char[src_w * src_h * 3]{0};
    unsigned char* dst_u = new unsigned char[dst_w * dst_h * 3]{0};

    src.to_pixels(src_u, ncnn::Mat::PIXEL_RGB);
    
    // 調試：檢查源數據
    std::cout << "  [WARP DEBUG] src_u (first 10): ";
    for (int k = 0; k < 10; k++) {
        std::cout << (int)src_u[k] << " ";
    }
    std::cout << std::endl;

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
    
    std::cout << "  [WARP DEBUG] Inverse matrix: m=[" << m[0] << "," << m[1] << "," << m[2] << "; " 
              << m[3] << "," << m[4] << "," << m[5] << "]" << std::endl;
    
    int valid_pixels = 0;
    int total_pixels = dst_w * dst_h;

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
            
            valid_pixels++;
            
            for (int c = 0; c < 3; c++) {
                dst_u[3 * (y * dst_w + x) + c] =
                    (src_u[3 * (sy * src_w + sx) + c] * cbufx[0] * cbufy[0] +
                     src_u[3 * ((sy + 1) * src_w + sx) + c] * cbufx[0] * cbufy[1] +
                     src_u[3 * (sy * src_w + sx + 1) + c] * cbufx[1] * cbufy[0] +
                     src_u[3 * ((sy + 1) * src_w + sx + 1) + c] * cbufx[1] * cbufy[1]) >> 22;
            }
        }
    }
    
    std::cout << "  [WARP DEBUG] Valid pixels: " << valid_pixels << " / " << total_pixels << std::endl;
    std::cout << "  [WARP DEBUG] dst_u (first 10): ";
    for (int k = 0; k < 10; k++) {
        std::cout << (int)dst_u[k] << " ";
    }
    std::cout << std::endl;

    dst = ncnn::Mat::from_pixels(dst_u, ncnn::Mat::PIXEL_RGB, dst_w, dst_h);
    delete[] src_u;
    delete[] dst_u;
}
