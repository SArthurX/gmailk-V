#ifndef FEATURE_EXTRACTOR_HELPER_HPP
#define FEATURE_EXTRACTOR_HELPER_HPP

#include "face_feature_extractor.h"
#include "cviruntime.h"

#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

extern "C" {
#include <cvi_sys.h>
}

/* ArcFace 112x112 標準 5 點參考座標 (x, y) */
static const float REF_PTS[5][2] = {
    {38.2946f, 51.6963f},   /* 左眼 */
    {73.5318f, 51.5014f},   /* 右眼 */
    {56.0252f, 71.7366f},   /* 鼻尖 */
    {41.5493f, 92.3655f},   /* 左嘴角 */
    {70.7299f, 92.2041f}    /* 右嘴角 */
};

/* ============================================================
 * 輔助函數: 取得 tensor float 資料 (自動處理量化)
 * ============================================================ */
static inline void get_float_data(CVI_TENSOR *t, std::vector<float> &out) {
    size_t n = CVI_NN_TensorCount(t);
    out.resize(n);
    void *p = CVI_NN_TensorPtr(t);

    switch (t->fmt) {
    case CVI_FMT_FP32:
        memcpy(out.data(), p, n * sizeof(float));
        break;
    case CVI_FMT_INT8: {
        int8_t *ip = (int8_t *)p;
        float qs = CVI_NN_TensorQuantScale(t);
        if (qs == 0.0f) qs = 1.0f;
        for (size_t i = 0; i < n; i++) out[i] = ip[i] * qs;
        break;
    }
    case CVI_FMT_UINT8: {
        uint8_t *up = (uint8_t *)p;
        float qs = CVI_NN_TensorQuantScale(t);
        if (qs == 0.0f) qs = 1.0f;
        for (size_t i = 0; i < n; i++) out[i] = up[i] * qs;
        break;
    }
    case CVI_FMT_BF16: {
        uint16_t *bp = (uint16_t *)p;
        for (size_t i = 0; i < n; i++) {
            uint32_t tmp = (uint32_t)bp[i] << 16;
            memcpy(&out[i], &tmp, sizeof(float));
        }
        break;
    }
    default:
        fprintf(stderr, "警告: 未知 tensor 格式 %d, 假設 FP32\n", t->fmt);
        memcpy(out.data(), p, n * sizeof(float));
        break;
    }
}

/* ============================================================
 * 判斷 tensor 是 NCHW 還是 NHWC
 * ============================================================ */
static inline bool is_nhwc(const CVI_SHAPE &shape) {
    if (shape.dim_size != 4) return false;
    return (shape.dim[3] <= 4 && shape.dim[1] > 4);
}

static inline void get_tensor_dims(const CVI_SHAPE &shape, int &N, int &C, int &H, int &W) {
    if (is_nhwc(shape)) {
        N = shape.dim[0]; H = shape.dim[1]; W = shape.dim[2]; C = shape.dim[3];
    } else {
        N = shape.dim[0]; C = shape.dim[1]; H = shape.dim[2]; W = shape.dim[3];
    }
}

/* ============================================================
 * NV21 → RGB 轉換 (純手動，不依賴 OpenCV)
 * ============================================================ */
static inline void convertNV21ToRGB(const unsigned char* nv21_data,
                                     int width, int height,
                                     unsigned char* rgb_data) {
    const unsigned char* y_plane = nv21_data;
    const unsigned char* vu_plane = nv21_data + width * height;
    
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            int y_index = i * width + j;
            int uv_index = (i / 2) * width + (j / 2) * 2;
            
            int Y = y_plane[y_index];
            int V = vu_plane[uv_index];
            int U = vu_plane[uv_index + 1];
            
            int R = Y + ((1436 * (V - 128)) >> 10);
            int G = Y - ((352 * (U - 128) + 731 * (V - 128)) >> 10);
            int B = Y + ((1815 * (U - 128)) >> 10);
            
            R = R < 0 ? 0 : (R > 255 ? 255 : R);
            G = G < 0 ? 0 : (G > 255 ? 255 : G);
            B = B < 0 ? 0 : (B > 255 ? 255 : B);
            
            int rgb_index = (i * width + j) * 3;
            rgb_data[rgb_index] = R;
            rgb_data[rgb_index + 1] = G;
            rgb_data[rgb_index + 2] = B;
        }
    }
}

/* ============================================================
 * NV21 VIDEO_FRAME → RGB buffer
 *
 * 將 VIDEO_FRAME_INFO_S 的 NV21 數據轉為 RGB uint8 buffer
 * 返回 RGB buffer (需要 caller 用 delete[] 釋放)，失敗返回 nullptr
 * ============================================================ */
static inline unsigned char* nv21FrameToRGB(VIDEO_FRAME_INFO_S* frame, int &out_w, int &out_h) {
    out_w = frame->stVFrame.u32Width;
    out_h = frame->stVFrame.u32Height;
    
    bool need_unmap = false;
    if (!frame->stVFrame.pu8VirAddr[0]) {
        size_t total_size = frame->stVFrame.u32Length[0] + frame->stVFrame.u32Length[1];
        frame->stVFrame.pu8VirAddr[0] = (CVI_U8*)CVI_SYS_Mmap(
            frame->stVFrame.u64PhyAddr[0], total_size);
        if (!frame->stVFrame.pu8VirAddr[0]) {
            std::cerr << "❌ Failed to map frame memory" << std::endl;
            return nullptr;
        }
        frame->stVFrame.pu8VirAddr[1] = frame->stVFrame.pu8VirAddr[0] + frame->stVFrame.u32Length[0];
        need_unmap = true;
    }
    
    unsigned char* rgb_data = new unsigned char[out_w * out_h * 3];
    convertNV21ToRGB(frame->stVFrame.pu8VirAddr[0], out_w, out_h, rgb_data);
    
    if (need_unmap) {
        size_t total_size = frame->stVFrame.u32Length[0] + frame->stVFrame.u32Length[1];
        CVI_SYS_Munmap((void*)frame->stVFrame.pu8VirAddr[0], total_size);
        frame->stVFrame.pu8VirAddr[0] = nullptr;
        frame->stVFrame.pu8VirAddr[1] = nullptr;
    }
    
    return rgb_data;
}

/* ============================================================
 * Similarity Transform: 最小二乘法求解旋轉+縮放+平移
 *
 * 給定 5 對 src/dst 點，求解:
 *   dst_x = a * src_x - b * src_y + tx
 *   dst_y = b * src_x + a * src_y + ty
 *
 * 使用迭代優化法（從原始 feature_extractor_helper 移植）
 * 輸出 6 參數仿射矩陣 M[0..5]:
 *   dst_x = M[0]*src_x + M[1]*src_y + M[2]
 *   dst_y = M[3]*src_x + M[4]*src_y + M[5]
 * ============================================================ */
static inline void computeAffineMatrix(const float landmarks[10], float M[6]) {
    /* landmarks: [x0,y0, x1,y1, ..., x4,y4] → 轉為 src_5pts 佈局 */
    float src_5pts[10]; // [x0,x1,x2,x3,x4,  y0,y1,y2,y3,y4]
    for (int i = 0; i < 5; i++) {
        src_5pts[i]     = landmarks[2 * i];      // x
        src_5pts[i + 5] = landmarks[2 * i + 1];  // y
    }
    
    float dst_5pts[10] = {
        REF_PTS[0][0], REF_PTS[1][0], REF_PTS[2][0], REF_PTS[3][0], REF_PTS[4][0],
        REF_PTS[0][1], REF_PTS[1][1], REF_PTS[2][1], REF_PTS[3][1], REF_PTS[4][1]
    };

    float src[10], dst[10];
    memcpy(src, src_5pts, sizeof(float) * 10);
    memcpy(dst, dst_5pts, sizeof(float) * 10);

    float ptmp[2] = {0, 0};
    for (int i = 0; i < 5; ++i) {
        ptmp[0] += src[i];
        ptmp[1] += src[i + 5];
    }
    ptmp[0] /= 5; ptmp[1] /= 5;

    for (int i = 0; i < 5; ++i) {
        src[i] -= ptmp[0]; src[i + 5] -= ptmp[1];
        dst[i] -= ptmp[0]; dst[i + 5] -= ptmp[1];
    }

    float dst_x = (dst[3] + dst[4] - dst[0] - dst[1]) / 2;
    float dst_y = (dst[8] + dst[9] - dst[5] - dst[6]) / 2;
    float src_x = (src[3] + src[4] - src[0] - src[1]) / 2;
    float src_y = (src[8] + src[9] - src[5] - src[6]) / 2;
    float theta = atan2(dst_x, dst_y) - atan2(src_x, src_y);
    float scale = sqrt(dst_x*dst_x + dst_y*dst_y) / sqrt(src_x*src_x + src_y*src_y);

    float pts1[10], pts0[2];
    float _a = sin(theta), _b = cos(theta);
    pts0[0] = pts0[1] = 0;
    for (int i = 0; i < 5; ++i) {
        pts1[i]     = scale * (src[i] * _b + src[i + 5] * _a);
        pts1[i + 5] = scale * (-src[i] * _a + src[i + 5] * _b);
        pts0[0] += (dst[i] - pts1[i]);
        pts0[1] += (dst[i + 5] - pts1[i + 5]);
    }
    pts0[0] /= 5; pts0[1] /= 5;

    float sqloss = 0;
    for (int i = 0; i < 5; ++i) {
        sqloss += ((pts0[0] + pts1[i] - dst[i]) * (pts0[0] + pts1[i] - dst[i]) +
                   (pts0[1] + pts1[i + 5] - dst[i + 5]) * (pts0[1] + pts1[i + 5] - dst[i + 5]));
    }

    float square_sum = 0;
    for (int i = 0; i < 10; ++i) square_sum += src[i] * src[i];

    for (int t = 0; t < 200; ++t) {
        _a = 0; _b = 0;
        for (int i = 0; i < 5; ++i) {
            _a += ((pts0[0] - dst[i]) * src[i + 5] - (pts0[1] - dst[i + 5]) * src[i]);
            _b += ((pts0[0] - dst[i]) * src[i] + (pts0[1] - dst[i + 5]) * src[i + 5]);
        }
        if (_b < 0) { _b = -_b; _a = -_a; }
        float _s = sqrt(_a * _a + _b * _b);
        _b /= _s; _a /= _s;

        for (int i = 0; i < 5; ++i) {
            pts1[i]     = scale * (src[i] * _b + src[i + 5] * _a);
            pts1[i + 5] = scale * (-src[i] * _a + src[i + 5] * _b);
        }

        float _scale = 0;
        for (int i = 0; i < 5; ++i)
            _scale += ((dst[i] - pts0[0]) * pts1[i] + (dst[i + 5] - pts0[1]) * pts1[i + 5]);
        _scale /= (square_sum * scale);
        for (int i = 0; i < 10; ++i) pts1[i] *= (_scale / scale);
        scale = _scale;

        pts0[0] = pts0[1] = 0;
        for (int i = 0; i < 5; ++i) {
            pts0[0] += (dst[i] - pts1[i]);
            pts0[1] += (dst[i + 5] - pts1[i + 5]);
        }
        pts0[0] /= 5; pts0[1] /= 5;

        float _sqloss = 0;
        for (int i = 0; i < 5; ++i) {
            _sqloss += ((pts0[0] + pts1[i] - dst[i]) * (pts0[0] + pts1[i] - dst[i]) +
                       (pts0[1] + pts1[i + 5] - dst[i + 5]) * (pts0[1] + pts1[i + 5] - dst[i + 5]));
        }
        if (fabs(_sqloss - sqloss) < 1e-2) break;
        sqloss = _sqloss;
    }

    for (int i = 0; i < 5; ++i) {
        pts1[i]     += (pts0[0] + ptmp[0]);
        pts1[i + 5] += (pts0[1] + ptmp[1]);
    }

    M[0] = _b * scale;
    M[1] = _a * scale;
    M[3] = -_a * scale;
    M[4] = _b * scale;
    M[2] = pts0[0] + ptmp[0] - scale * (ptmp[0] * _b + ptmp[1] * _a);
    M[5] = pts0[1] + ptmp[1] - scale * (-ptmp[0] * _a + ptmp[1] * _b);
}

/* ============================================================
 * 仿射變換 (雙線性插值，純手動)
 *
 * 將 src (RGB, src_w x src_h) 經仿射變換 M 輸出為 dst (RGB, dst_w x dst_h)
 * dst 需預先分配 dst_w * dst_h * 3 bytes
 * ============================================================ */
static inline void warpAffine(const unsigned char* src, int src_w, int src_h,
                               unsigned char* dst, int dst_w, int dst_h,
                               const float M[6]) {
    // 計算逆矩陣
    float m[6];
    memcpy(m, M, sizeof(float) * 6);
    float D = m[0] * m[4] - m[1] * m[3];
    D = D != 0 ? 1.0f / D : 0;
    float A11 = m[4] * D, A22 = m[0] * D;
    m[0] = A11;
    m[1] *= -D;
    m[3] *= -D;
    m[4] = A22;
    float b1 = -m[0] * M[2] - m[1] * M[5];
    float b2 = -m[3] * M[2] - m[4] * M[5];
    m[2] = b1;
    m[5] = b2;

    memset(dst, 0, dst_w * dst_h * 3);

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            float fx = m[0] * x + m[1] * y + m[2];
            float fy = m[3] * x + m[4] * y + m[5];

            int sy = (int)floor(fy);
            fy -= sy;
            if (sy < 0 || sy >= src_h - 1) continue;

            int sx = (int)floor(fx);
            fx -= sx;
            if (sx < 0 || sx >= src_w - 1) continue;

            short cbufy0 = (short)((1.f - fy) * 2048);
            short cbufy1 = 2048 - cbufy0;
            short cbufx0 = (short)((1.f - fx) * 2048);
            short cbufx1 = 2048 - cbufx0;

            for (int c = 0; c < 3; c++) {
                dst[3 * (y * dst_w + x) + c] =
                    (src[3 * (sy * src_w + sx) + c] * cbufx0 * cbufy0 +
                     src[3 * ((sy + 1) * src_w + sx) + c] * cbufx0 * cbufy1 +
                     src[3 * (sy * src_w + sx + 1) + c] * cbufx1 * cbufy0 +
                     src[3 * ((sy + 1) * src_w + sx + 1) + c] * cbufx1 * cbufy1) >> 22;
            }
        }
    }
}

/* ============================================================
 * 將 RGB buffer 填入 CVI tensor (不依賴 OpenCV)
 *
 * 根據 tensor 格式自動判斷:
 *   - pixel_format → RGB/BGR 順序
 *   - fmt → UINT8 直接填 / FP32 做 normalize
 *   - NCHW / NHWC 排列
 * ============================================================ */
static inline void fill_tensor_rgb(CVI_TENSOR *t, const unsigned char* rgb,
                                    int img_w, int img_h) {
    CVI_SHAPE shape = CVI_NN_TensorShape(t);
    int N, C, H, W;
    get_tensor_dims(shape, N, C, H, W);
    bool nhwc = is_nhwc(shape);

    /* 需要 BGR 嗎？ */
    bool need_bgr = (t->pixel_format == CVI_NN_PIXEL_BGR_PACKED ||
                     t->pixel_format == CVI_NN_PIXEL_BGR_PLANAR);

    void *ptr = CVI_NN_TensorPtr(t);

    /* 假設 img_w == W && img_h == H (已由 caller 保證 112x112) */

    if (t->fmt == CVI_FMT_UINT8 || t->fmt == CVI_FMT_INT8) {
        uint8_t *dst = (uint8_t *)ptr;
        if (nhwc) {
            for (int h = 0; h < H; h++) {
                for (int w = 0; w < W; w++) {
                    int si = (h * img_w + w) * 3;
                    int di = (h * W + w) * C;
                    if (need_bgr) {
                        dst[di + 0] = rgb[si + 2]; // B
                        dst[di + 1] = rgb[si + 1]; // G
                        dst[di + 2] = rgb[si + 0]; // R
                    } else {
                        dst[di + 0] = rgb[si + 0]; // R
                        dst[di + 1] = rgb[si + 1]; // G
                        dst[di + 2] = rgb[si + 2]; // B
                    }
                }
            }
        } else {
            /* NCHW: planar */
            for (int c = 0; c < C; c++) {
                int sc = need_bgr ? (2 - c) : c;
                for (int h = 0; h < H; h++)
                    for (int w = 0; w < W; w++)
                        dst[c * H * W + h * W + w] = rgb[(h * img_w + w) * 3 + sc];
            }
        }
    } else {
        /* FP32: 手動 normalize */
        float *dst = (float *)ptr;
        float m[3] = {t->mean[0], t->mean[1], t->mean[2]};
        float s[3] = {t->scale[0], t->scale[1], t->scale[2]};

        if (s[0] == 0.0f && s[1] == 0.0f && s[2] == 0.0f) {
            m[0] = m[1] = m[2] = 127.5f;
            s[0] = s[1] = s[2] = 1.0f / 128.0f;
        }

        if (nhwc) {
            for (int h = 0; h < H; h++)
                for (int w = 0; w < W; w++)
                    for (int c = 0; c < C; c++) {
                        int sc = need_bgr ? (2 - c) : c;
                        dst[h * W * C + w * C + c] =
                            ((float)rgb[(h * img_w + w) * 3 + sc] - m[c]) * s[c];
                    }
        } else {
            for (int c = 0; c < C; c++) {
                int sc = need_bgr ? (2 - c) : c;
                for (int h = 0; h < H; h++)
                    for (int w = 0; w < W; w++)
                        dst[c * H * W + h * W + w] =
                            ((float)rgb[(h * img_w + w) * 3 + sc] - m[c]) * s[c];
            }
        }
    }
}

/* ============================================================
 * L2 正規化
 * ============================================================ */
inline void FaceFeatureExtractor::normalize(std::vector<float>& feature) {
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

#endif // FEATURE_EXTRACTOR_HELPER_HPP
