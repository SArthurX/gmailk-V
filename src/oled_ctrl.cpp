#include "oled_ctrl.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

extern "C" {
#include "DEV_Config.h"
#include "OLED_1in51.h"
#include "GUI_Paint.h"
}

// 繪製線段 (Bresenham's line algorithm)
static void draw_line(OLEDHandler_t *pstHandler, 
                     int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        OLEDHandler_DrawPixel(pstHandler, x0, y0, 1);
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

int OLEDHandler_Init(OLEDHandler_t *pstHandler) {
    if (!pstHandler) {
        std::cerr << "OLEDHandler_Init: Invalid handler pointer" << std::endl;
        return -1;
    }

    std::memset(pstHandler, 0, sizeof(OLEDHandler_t));

    // 初始化 DEV_Config (wiringX + SPI + GPIO)
    if (DEV_ModuleInit() != 0) {
        std::cerr << "Failed to initialize DEV_Module (wiringX/SPI)" << std::endl;
        return -1;
    }

    // SPI bus warm-up: 開機後 SPI driver 剛 probe 完成，
    // 前幾筆 transaction 可能被靜默丟棄。
    // 先送幾個無害的 dummy byte 確保 SPI bus 已穩定。
    DEV_Delay_ms(50);

    // 使用重試機制初始化 1.51" 透明 OLED (SPI)
    // 最多重試 3 次，每次包含完整硬體重置 + 暫存器初始化
    int init_ret = OLED_1in51_InitWithRetry(3);
    if (init_ret != 0) {
        std::cerr << "OLED_1in51_InitWithRetry failed after all retries" << std::endl;
        DEV_ModuleExit();
        return -1;
    }

    // 分配 GUI_Paint 影像緩衝區
    // OLED_1in51_WIDTH=64, OLED_1in51_HEIGHT=128
    uint16_t imagesize = ((OLED_1in51_WIDTH % 8 == 0) ? 
                          (OLED_1in51_WIDTH / 8) : 
                          (OLED_1in51_WIDTH / 8 + 1)) * OLED_1in51_HEIGHT;
    
    pstHandler->image_buffer = (uint8_t *)malloc(imagesize);
    if (!pstHandler->image_buffer) {
        std::cerr << "Failed to allocate OLED image buffer" << std::endl;
        DEV_ModuleExit();
        return -1;
    }

    // 初始化 GUI_Paint，旋轉 270 度使邏輯坐標為 128x64 (寬x高)
    Paint_NewImage(pstHandler->image_buffer, 
                   OLED_1in51_WIDTH, OLED_1in51_HEIGHT, 
                   270, BLACK);
    Paint_SelectImage(pstHandler->image_buffer);
    Paint_Clear(BLACK);

    pstHandler->initialized = 1;
    
    std::cout << "OLED Handler initialized successfully (128x64, SPI, 1.51\" transparent)" << std::endl;
    
    return 0;
}

void OLEDHandler_Cleanup(OLEDHandler_t *pstHandler) {
    if (pstHandler && pstHandler->initialized) {
        OLEDHandler_ClearScreen(pstHandler);
        
        // 釋放影像緩衝區
        if (pstHandler->image_buffer) {
            free(pstHandler->image_buffer);
            pstHandler->image_buffer = nullptr;
        }
        
        DEV_ModuleExit();
        pstHandler->initialized = 0;
        std::cout << "OLED Handler cleaned up" << std::endl;
    }
}

int OLEDHandler_ClearScreen(OLEDHandler_t *pstHandler) {
    if (!pstHandler || !pstHandler->initialized)
        return -1;

    Paint_SelectImage(pstHandler->image_buffer);
    Paint_Clear(BLACK);
    OLED_1in51_Clear();
    
    return 0;
}

void OLEDHandler_ConvertCoordinate(float src_x, float src_y, 
                                   uint8_t *dst_x, uint8_t *dst_y) {
    //  1280x720 mapping to 128x64
    float scale_x = (float)OLED_WIDTH / (float)FRAME_WIDTH;
    float scale_y = (float)OLED_HEIGHT / (float)FRAME_HEIGHT;
    
    int x = (int)(src_x * scale_x);
    int y = (int)(src_y * scale_y);
    

    // flip vertically if needed
    // x = OLED_WIDTH - 1 - x;
    // y = OLED_HEIGHT - 1 - y;
    
    if (x < 0) x = 0;
    if (x >= OLED_WIDTH) x = OLED_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= OLED_HEIGHT) y = OLED_HEIGHT - 1;
    
    *dst_x = (uint8_t)x;
    *dst_y = (uint8_t)y;
}

void OLEDHandler_DrawPixel(OLEDHandler_t *pstHandler,
                          uint8_t x, uint8_t y,
                          uint8_t color) {
    if (!pstHandler || x >= OLED_WIDTH || y >= OLED_HEIGHT)
        return;

    // 使用 GUI_Paint 的 SetPixel 繪製像素
    // Paint_NewImage 已旋轉 270 度，邏輯坐標系為 128(W) x 64(H)
    Paint_SetPixel(x, y, color ? WHITE : BLACK);
}

void OLEDHandler_DrawRect(OLEDHandler_t *pstHandler,
                         uint8_t x1, uint8_t y1,
                         uint8_t x2, uint8_t y2,
                         uint8_t filled) {
    if (!pstHandler) return;

    // 確保 x1 < x2, y1 < y2
    if (x1 > x2) std::swap(x1, x2);
    if (y1 > y2) std::swap(y1, y2);

    if (filled) {
        // 填充矩形
        for (uint8_t y = y1; y <= y2; y++) {
            for (uint8_t x = x1; x <= x2; x++) {
                OLEDHandler_DrawPixel(pstHandler, x, y, 1);
            }
        }
    } else {
        // 空心矩形 - 繪製四條邊
        draw_line(pstHandler, x1, y1, x2, y1);  // 上邊
        draw_line(pstHandler, x2, y1, x2, y2);  // 右邊
        draw_line(pstHandler, x2, y2, x1, y2);  // 下邊
        draw_line(pstHandler, x1, y2, x1, y1);  // 左邊
    }
}

void OLEDHandler_DrawCrosshair(OLEDHandler_t *pstHandler) {
    if (!pstHandler) return;

    uint8_t center_x = OLED_WIDTH / 2;
    uint8_t center_y = OLED_HEIGHT / 2;
    uint8_t size = 5;

    // 繪製十字準星
    draw_line(pstHandler, center_x - size, center_y, center_x + size, center_y);
    draw_line(pstHandler, center_x, center_y - size, center_x, center_y + size);

    // 在中心畫一個小圓 (用正方形代替)
    OLEDHandler_DrawRect(pstHandler, center_x - 1, center_y - 1, 
                        center_x + 1, center_y + 1, 0);
}

int OLEDHandler_FlushBuffer(OLEDHandler_t *pstHandler) {
    if (!pstHandler || !pstHandler->initialized || !pstHandler->image_buffer)
        return -1;

    // 使用 waveshare 驅動直接刷新影像緩衝區到 OLED
    OLED_1in51_Display(pstHandler->image_buffer);

    return 0;
}

int OLEDHandler_DisplayInfo(OLEDHandler_t *pstHandler,
                            uint32_t face_count,
                            float fps,
                            const char *match_name,
                            const char *match_payload) {
    if (!pstHandler || !pstHandler->initialized)
        return -1;

    char info_line[32];
    
    // === 右上角：FPS 資訊 ===
    snprintf(info_line, sizeof(info_line), "F:%u %.0f", face_count, fps);
    int text_len = (int)strlen(info_line);
    // Font8: 5px wide per char, 右對齊到 128px 寬螢幕
    int fps_x = OLED_WIDTH - text_len * 5;
    if (fps_x < 0) fps_x = 0;
    
    Paint_SelectImage(pstHandler->image_buffer);
    Paint_DrawString_EN(fps_x, 0, info_line, &Font8, BLACK, WHITE);

    // === 左上角：匹配結果 (姓名 + 酬載摘要) ===
    if (match_name && match_name[0] != '\0') {
        // 第一行：姓名
        char name_line[22]; // 128/5 = 25 chars max, 留些空間
        snprintf(name_line, sizeof(name_line), "%s", match_name);
        Paint_DrawString_EN(0, 0, name_line, &Font8, BLACK, WHITE);
        
        // 第二行：酬載摘要 (y=8, Font8 高度為 8px)
        if (match_payload && match_payload[0] != '\0') {
            char payload_line[22];
            snprintf(payload_line, sizeof(payload_line), "%s", match_payload);
            Paint_DrawString_EN(0, 8, payload_line, &Font8, BLACK, WHITE);
        }
    }

    return 0;
}

int OLEDHandler_UpdateDisplay(OLEDHandler_t *pstHandler, 
                              const OLEDFaceBox_t *faces, 
                              uint32_t face_count,
                              float fps,
                              const char *match_name,
                              const char *match_payload) {
    if (!pstHandler || !pstHandler->initialized)
        return -1;

    // 選擇影像緩衝區並清空
    Paint_SelectImage(pstHandler->image_buffer);
    Paint_Clear(BLACK);

    // 繪製十字準星
    OLEDHandler_DrawCrosshair(pstHandler);

    // 繪製所有人臉框
    for (uint32_t i = 0; i < face_count; i++) {
        uint8_t x1, y1, x2, y2;
        
        // 轉換坐標
        OLEDHandler_ConvertCoordinate(faces[i].x1, faces[i].y1, &x1, &y1);
        OLEDHandler_ConvertCoordinate(faces[i].x2, faces[i].y2, &x2, &y2);

        // 繪製矩形框
        OLEDHandler_DrawRect(pstHandler, x1, y1, x2, y2, 0);

        // 如果是中心對準的人臉，在框內繪製一個小標記
        if (faces[i].is_center) {
            // 在人臉框中心繪製一個小的實心矩形作為標記
            uint8_t center_x = (x1 + x2) / 2;
            uint8_t center_y = (y1 + y2) / 2;
            OLEDHandler_DrawRect(pstHandler, 
                               center_x - 2, center_y - 2,
                               center_x + 2, center_y + 2, 1);
        }
    }

    // 顯示FPS、人臉數量、匹配結果 (直接繪製到 frame buffer)
    OLEDHandler_DisplayInfo(pstHandler, face_count, fps, match_name, match_payload);

    // 將 frame buffer 刷新到 OLED
    int ret = OLEDHandler_FlushBuffer(pstHandler);

    return ret;
}
