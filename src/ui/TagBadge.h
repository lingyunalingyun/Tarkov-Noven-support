#pragma once
#include "ui/UiCanvas.h"
#include "ui/Theme.h"
#include <algorithm>

namespace noven::ui {
// 文本和资源仅在绘制期间借用；标签预留宽度，长标题省略，不溢出卡片。
// Borrow text/resources only during drawing; reserve badge width and ellipsize long titles within the card.
inline void DrawTaggedTitle(const UiCanvas& canvas, const UiTheme& theme,
    D2D1_RECT_F bounds, std::wstring_view title, std::wstring_view tag) {
    if (tag.empty() || !canvas.textFactory) {
        canvas.Text(title, canvas.label, bounds, theme.primaryText);
        return;
    }
    Microsoft::WRL::ComPtr<IDWriteTextLayout> badge, name;
    const float width = bounds.right - bounds.left;
    if (width <= 0 || FAILED(canvas.textFactory->CreateTextLayout(tag.data(),
        static_cast<UINT32>(tag.size()), &canvas.smallFormat, width, 24, &badge))) return;
    badge->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics{};
    badge->GetMetrics(&metrics);
    const float badgeWidth = metrics.width + 12;
    const float nameWidth = width - badgeWidth - 8;
    if (nameWidth < 40) { canvas.Text(title, canvas.label, bounds, theme.primaryText); return; }
    if (FAILED(canvas.textFactory->CreateTextLayout(title.data(), static_cast<UINT32>(title.size()),
        &canvas.label, nameWidth, bounds.bottom - bounds.top, &name))) return;
    name->SetWordWrapping(canvas.label.GetWordWrapping());
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(canvas.textFactory->CreateEllipsisTrimmingSign(&canvas.label, &ellipsis)))
        name->SetTrimming(&trimming, ellipsis.Get());
    name->GetMetrics(&metrics);
    canvas.brush.SetColor(theme.primaryText);
    canvas.target.DrawTextLayout(D2D1::Point2F(bounds.left, bounds.top), name.Get(),
        &canvas.brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    const float left = bounds.left + (std::min)(metrics.width, nameWidth) + 8;
    const auto box = D2D1::RectF(left, bounds.top + 2, left + badgeWidth, bounds.top + 23);
    canvas.Round(box, 3, theme.background);
    canvas.Text(tag, canvas.smallFormat,
        D2D1::RectF(box.left + 6, box.top, box.right - 6, box.bottom), theme.secondaryText);
}
}
