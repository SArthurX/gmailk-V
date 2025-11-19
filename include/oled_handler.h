#ifndef OLED_HANDLER_H
#define OLED_HANDLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// OLED 螢幕尺寸常數
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

// 原始影像尺寸常數
#define FRAME_WIDTH 1920
#define FRAME_HEIGHT 1080

// 人臉框結構
typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    uint8_t is_center;  // 是否為中心對準的人臉
} OLEDFaceBox_t;

// OLED 處理器結構
typedef struct {
    uint8_t i2c_dev;        // I2C 設備節點 (0, 1, 2...)
    uint8_t initialized;    // 初始化標誌
    uint8_t frame_buffer[OLED_WIDTH * OLED_HEIGHT / 8];  // 顯示緩衝區
} OLEDHandler_t;

/**
 * @brief 初始化 OLED 處理器
 * @param pstHandler OLED 處理器指針
 * @param i2c_dev I2C 設備節點 (通常是 2)
 * @return 0 成功, 非 0 失敗
 */
int OLEDHandler_Init(OLEDHandler_t *pstHandler, uint8_t i2c_dev);

/**
 * @brief 清理 OLED 處理器
 * @param pstHandler OLED 處理器指針
 */
void OLEDHandler_Cleanup(OLEDHandler_t *pstHandler);

/**
 * @brief 清空 OLED 螢幕
 * @param pstHandler OLED 處理器指針
 * @return 0 成功, 非 0 失敗
 */
int OLEDHandler_ClearScreen(OLEDHandler_t *pstHandler);

/**
 * @brief 更新 OLED 顯示內容
 * @param pstHandler OLED 處理器指針
 * @param faces 人臉框陣列
 * @param face_count 人臉數量
 * @param fps 當前 FPS
 * @return 0 成功, 非 0 失敗
 */
int OLEDHandler_UpdateDisplay(OLEDHandler_t *pstHandler, 
                              const OLEDFaceBox_t *faces, 
                              uint32_t face_count,
                              float fps);

/**
 * @brief 將原始坐標轉換為 OLED 坐標
 * @param src_x 原始 X 坐標 (0-1920)
 * @param src_y 原始 Y 坐標 (0-1080)
 * @param dst_x 輸出 OLED X 坐標 (0-128)
 * @param dst_y 輸出 OLED Y 坐標 (0-64)
 */
void OLEDHandler_ConvertCoordinate(float src_x, float src_y, 
                                   uint8_t *dst_x, uint8_t *dst_y);

/**
 * @brief 在 frame buffer 中繪製矩形框
 * @param pstHandler OLED 處理器指針
 * @param x1 左上角 X
 * @param y1 左上角 Y
 * @param x2 右下角 X
 * @param y2 右下角 Y
 * @param filled 是否填充 (0: 空心, 1: 實心)
 */
void OLEDHandler_DrawRect(OLEDHandler_t *pstHandler,
                         uint8_t x1, uint8_t y1,
                         uint8_t x2, uint8_t y2,
                         uint8_t filled);

/**
 * @brief 在 frame buffer 中繪製十字準星
 * @param pstHandler OLED 處理器指針
 */
void OLEDHandler_DrawCrosshair(OLEDHandler_t *pstHandler);

/**
 * @brief 在 frame buffer 中繪製像素點
 * @param pstHandler OLED 處理器指針
 * @param x X 坐標
 * @param y Y 坐標
 * @param color 顏色 (0: 黑, 1: 白)
 */
void OLEDHandler_DrawPixel(OLEDHandler_t *pstHandler,
                          uint8_t x, uint8_t y,
                          uint8_t color);

/**
 * @brief 將 frame buffer 刷新到 OLED 螢幕
 * @param pstHandler OLED 處理器指針
 * @return 0 成功, 非 0 失敗
 */
int OLEDHandler_FlushBuffer(OLEDHandler_t *pstHandler);

/**
 * @brief 顯示文字信息 (FPS, 人臉數量等)
 * @param pstHandler OLED 處理器指針
 * @param face_count 人臉數量
 * @param fps 當前 FPS
 * @return 0 成功, 非 0 失敗
 */
int OLEDHandler_DisplayInfo(OLEDHandler_t *pstHandler,
                            uint32_t face_count,
                            float fps);

#ifdef __cplusplus
}
#endif

#endif // OLED_HANDLER_H
