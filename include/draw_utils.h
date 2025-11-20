#ifndef DRAW_UTILS_H
#define DRAW_UTILS_H

#include "cvi_tdl.h"

constexpr cvtdl_service_brush_t CreateBrush(float r, float g, float b, uint32_t size = 2) {
    return cvtdl_service_brush_t{{r, g, b}, size};
}

constexpr cvtdl_service_brush_t BRUSH_GREEN   = CreateBrush(0.0f,   255.0f, 0.0f);
constexpr cvtdl_service_brush_t BRUSH_RED     = CreateBrush(255.0f, 0.0f,   0.0f);
constexpr cvtdl_service_brush_t BRUSH_BLUE    = CreateBrush(0.0f,   0.0f,   255.0f);
constexpr cvtdl_service_brush_t BRUSH_YELLOW  = CreateBrush(255.0f, 255.0f, 0.0f);
constexpr cvtdl_service_brush_t BRUSH_WHITE   = CreateBrush(255.0f, 255.0f, 255.0f);
constexpr cvtdl_service_brush_t BRUSH_CYAN    = CreateBrush(0.0f,   255.0f, 255.0f);
constexpr cvtdl_service_brush_t BRUSH_MAGENTA = CreateBrush(255.0f, 0.0f,   255.0f);
constexpr cvtdl_service_brush_t BRUSH_ORANGE  = CreateBrush(255.0f, 165.0f, 0.0f);

#endif // DRAW_UTILS_H
