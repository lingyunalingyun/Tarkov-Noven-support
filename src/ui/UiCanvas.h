#pragma once

#pragma push_macro("DrawText")
#ifdef DrawText
#undef DrawText
#endif
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <string_view>

namespace noven::ui {

// 轻量绘制入口，供侧栏和页面复用同一画笔与文字格式。
// Small drawing surface shared by sidebar and page host.
struct UiCanvas final {
    ID2D1RenderTarget& target;
    ID2D1SolidColorBrush& brush;
    IDWriteTextFormat& title;
    IDWriteTextFormat& pageTitle;
    IDWriteTextFormat& label;
    IDWriteTextFormat& body;
    IDWriteTextFormat& smallFormat;

    void Fill(D2D1_RECT_F rect, D2D1_COLOR_F color) const {
        brush.SetColor(color);
        target.FillRectangle(rect, &brush);
    }

    void Round(D2D1_RECT_F rect, float radius, D2D1_COLOR_F color) const {
        brush.SetColor(color);
        target.FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), &brush);
    }

    void Circle(D2D1_POINT_2F center, float radius, D2D1_COLOR_F color) const {
        brush.SetColor(color);
        target.FillEllipse(D2D1::Ellipse(center, radius, radius), &brush);
    }

    void Text(std::wstring_view value, IDWriteTextFormat& format,
              D2D1_RECT_F rect, D2D1_COLOR_F color) const {
        brush.SetColor(color);
        target.DrawText(value.data(), static_cast<UINT32>(value.size()),
                         &format, rect, &brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
};

} // namespace noven::ui

#pragma pop_macro("DrawText")
