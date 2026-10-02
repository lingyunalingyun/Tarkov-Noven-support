#pragma once

#pragma push_macro("DrawText")
#ifdef DrawText
#undef DrawText
#endif
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <string_view>
#include <wrl/client.h>

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
    IDWriteFactory* textFactory{};
    // 借用当前输入页的窗口；旧页过渡和离屏绘制必须留空，避免重定位输入法。
    // Borrow the active input window; outgoing/offscreen draws leave it null to avoid moving the IME.
    HWND inputWindow{};

    // 只缩小超出可用宽度的本地化页名，不改变标题行高度。
    // Shrink only localized page names that exceed available width, preserving the title-row height.
    void FittedTitle(std::wstring_view value, D2D1_RECT_F rect, D2D1_COLOR_F color) const {
        if (!textFactory) { Text(value, title, rect, color); return; }
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(textFactory->CreateTextLayout(value.data(), static_cast<UINT32>(value.size()),
                &title, 10000, rect.bottom - rect.top, &layout))) {
            Text(value, title, rect, color); return;
        }
        DWRITE_TEXT_METRICS metrics{};
        if (SUCCEEDED(layout->GetMetrics(&metrics)) && metrics.width > rect.right - rect.left)
            layout->SetFontSize(title.GetFontSize() * (rect.right - rect.left) / metrics.width,
                DWRITE_TEXT_RANGE{0, static_cast<UINT32>(value.size())});
        layout->SetMaxWidth(rect.right - rect.left);
        brush.SetColor(color);
        target.DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), &brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

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

    // 临时居中后恢复共享格式；控件不得把对齐状态泄漏到后续绘制。
    // Center temporarily and restore the borrowed format; controls must not leak alignment state.
    void CenteredText(std::wstring_view value, IDWriteTextFormat& format,
                      D2D1_RECT_F rect, D2D1_COLOR_F color) const {
        const auto horizontal=format.GetTextAlignment();
        const auto vertical=format.GetParagraphAlignment();
        format.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format.SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        Text(value,format,rect,color);
        format.SetTextAlignment(horizontal);
        format.SetParagraphAlignment(vertical);
    }
};

} // namespace noven::ui

#pragma pop_macro("DrawText")
