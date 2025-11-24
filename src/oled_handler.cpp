#include "oled_handler.h"
#include <iostream>
#include <cstring>
#include <cstdio>
#include <algorithm>

extern "C" {
#include "ssd1306.h"
#include "linux_i2c.h"
}

// 內部輔助函數：繪製線段 (Bresenham's line algorithm)
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

int OLEDHandler_Init(OLEDHandler_t *pstHandler, uint8_t i2c_dev) {
    if (!pstHandler) {
        std::cerr << "OLEDHandler_Init: Invalid handler pointer" << std::endl;
        return -1;
    }

    std::memset(pstHandler, 0, sizeof(OLEDHandler_t));
    pstHandler->i2c_dev = i2c_dev;

    // 初始化 SSD1306
    uint8_t ret = ssd1306_init(i2c_dev);
    if (ret != 0) {
        std::cerr << "Failed to initialize SSD1306, ret=" << (int)ret << std::endl;
        return -1;
    }

    // 配置 OLED (128x64)
    ret = ssd1306_oled_default_config(SSD1306_128_64_LINES, SSD1306_128_64_COLUMNS);
    if (ret != 0) {
        std::cerr << "Failed to configure SSD1306, ret=" << (int)ret << std::endl;
        ssd1306_end();
        return -1;
    }

    // 打開顯示
    ssd1306_oled_onoff(1);
    
    // 清空螢幕
    OLEDHandler_ClearScreen(pstHandler);

    pstHandler->initialized = 1;
    
    std::cout << "OLED Handler initialized successfully (128x64)" << std::endl;
    std::cout << "I2C device: " << (int)i2c_dev << std::endl;
    
    return 0;
}

void OLEDHandler_Cleanup(OLEDHandler_t *pstHandler) {
    if (pstHandler && pstHandler->initialized) {
        OLEDHandler_ClearScreen(pstHandler);
        ssd1306_oled_onoff(0);
        ssd1306_end();
        pstHandler->initialized = 0;
        std::cout << "OLED Handler cleaned up" << std::endl;
    }
}

int OLEDHandler_ClearScreen(OLEDHandler_t *pstHandler) {
    if (!pstHandler || !pstHandler->initialized) {
        return -1;
    }

    // 清空 frame buffer
    std::memset(pstHandler->frame_buffer, 0, sizeof(pstHandler->frame_buffer));
    
    // 使用 ssd1306 API 清空螢幕
    uint8_t ret = ssd1306_oled_clear_screen();
    
    return (ret == 0) ? 0 : -1;
}

void OLEDHandler_ConvertCoordinate(float src_x, float src_y, 
                                   uint8_t *dst_x, uint8_t *dst_y) {
    // 將 1920x1080 坐標映射到 128x64
    float scale_x = (float)OLED_WIDTH / (float)FRAME_WIDTH;
    float scale_y = (float)OLED_HEIGHT / (float)FRAME_HEIGHT;
    
    int x = (int)(src_x * scale_x);
    int y = (int)(src_y * scale_y);
    
    // 邊界檢查
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
    if (!pstHandler || x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }

    // OLED 是垂直排列的，8 個像素為一個 byte
    uint16_t index = x + (y / 8) * OLED_WIDTH;
    uint8_t bit = y % 8;

    if (color) {
        pstHandler->frame_buffer[index] |= (1 << bit);
    } else {
        pstHandler->frame_buffer[index] &= ~(1 << bit);
    }
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
    // 水平線
    draw_line(pstHandler, center_x - size, center_y, center_x + size, center_y);
    // 垂直線
    draw_line(pstHandler, center_x, center_y - size, center_x, center_y + size);

    // 在中心畫一個小圓 (用正方形代替)
    OLEDHandler_DrawRect(pstHandler, center_x - 1, center_y - 1, 
                        center_x + 1, center_y + 1, 0);
}

int OLEDHandler_FlushBuffer(OLEDHandler_t *pstHandler) {
    if (!pstHandler || !pstHandler->initialized) {
        return -1;
    }

    // 設置為水平模式
    ssd1306_oled_set_mem_mode(SSD1306_HORI_MODE);
    
    // 設置列地址範圍 (0-127)
    ssd1306_oled_set_col(0, OLED_WIDTH - 1);
    
    // 設置頁地址範圍 (0-7, 每頁 8 行)
    ssd1306_oled_set_page(0, (OLED_HEIGHT / 8) - 1);

    // 寫入數據到 OLED
    // 分批寫入數據 (SSD1306 控制字節 + 數據)
    const uint16_t chunk_size = 128;
    uint16_t total_bytes = OLED_WIDTH * OLED_HEIGHT / 8;
    
    for (uint16_t i = 0; i < total_bytes; i += chunk_size) {
        uint16_t bytes_to_write = std::min((uint16_t)chunk_size, (uint16_t)(total_bytes - i));
        
        // 準備數據包：控制字節 + 數據
        uint8_t packet[chunk_size + 1];
        packet[0] = SSD1306_DATA_CONTROL_BYTE;
        std::memcpy(packet + 1, pstHandler->frame_buffer + i, bytes_to_write);
        
        uint8_t ret = _i2c_write(packet, bytes_to_write + 1);
        if (ret != 0) {
            std::cerr << "Failed to write to OLED at offset " << i << std::endl;
            return -1;
        }
    }

    return 0;
}

int OLEDHandler_DisplayInfo(OLEDHandler_t *pstHandler,
                            uint32_t face_count,
                            float fps) {
    if (!pstHandler || !pstHandler->initialized) {
        return -1;
    }

    // 準備信息字串
    char info_line1[32];
    char info_line2[32];
    
    snprintf(info_line1, sizeof(info_line1), "Faces:%u", face_count);
    snprintf(info_line2, sizeof(info_line2), "FPS:%.1f", fps);

    // 在螢幕頂部顯示信息
    ssd1306_oled_set_XY(0, 0);
    ssd1306_oled_write_string(SSD1306_FONT_SMALL, info_line1);
    
    ssd1306_oled_set_XY(0, 1);
    ssd1306_oled_write_string(SSD1306_FONT_SMALL, info_line2);

    return 0;
}

int OLEDHandler_UpdateDisplay(OLEDHandler_t *pstHandler, 
                              const OLEDFaceBox_t *faces, 
                              uint32_t face_count,
                              float fps) {
    if (!pstHandler || !pstHandler->initialized) {
        return -1;
    }

    // 清空 frame buffer
    std::memset(pstHandler->frame_buffer, 0, sizeof(pstHandler->frame_buffer));

    // 繪製十字準星 (畫面中心)
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

    // 將 frame buffer 刷新到 OLED
    int ret = OLEDHandler_FlushBuffer(pstHandler);
    if (ret != 0) {
        return ret;
    }

    // 顯示文字信息 (FPS 和人臉數量)
    ret = OLEDHandler_DisplayInfo(pstHandler, face_count, fps);

    return ret;
}
