#pragma once
#include "ui/UiCanvas.h"
#include "ui/Theme.h"
#include "ui/localization/LocalizationService.h"
#include <algorithm>
#include <optional>

namespace noven::ui {
inline constexpr std::size_t PricePageSize = 30;
inline constexpr float PricePagerHeight = 44;
inline std::size_t PricePageCount(std::size_t total) noexcept {
    return (std::max)(std::size_t{1}, total / PricePageSize + (total % PricePageSize != 0));
}
// 页头和页尾共用绘制/命中槽位，窄窗口也不依赖固定像素宽度。
// Header and footer share draw/hit slots without fixed pixel widths in narrow windows.
inline D2D1_RECT_F PricePagerSlot(D2D1_RECT_F rect, int index) noexcept {
    const float slot = (rect.right - rect.left) / 3;
    return D2D1::RectF(rect.left + slot * index, rect.top,
        rect.left + slot * (index + 1), rect.bottom);
}
inline std::optional<int> HitPricePager(D2D1_RECT_F rect, float x, float y,
                                       std::size_t page, std::size_t total) noexcept {
    if (y < rect.top || y >= rect.bottom || x < rect.left || x >= rect.right) return {};
    if (x < PricePagerSlot(rect, 0).right && page > 0) return -1;
    if (x >= PricePagerSlot(rect, 2).left && page + 1 < PricePageCount(total)) return 1;
    return {};
}
inline void DrawPricePager(const UiCanvas& canvas, const UiTheme& theme,
                          D2D1_RECT_F rect, std::size_t page, std::size_t total) {
    for (int index : {0, 2}) {
        const auto slot = PricePagerSlot(rect, index);
        const bool enabled = index == 0 ? page > 0 : page + 1 < PricePageCount(total);
        canvas.Round(slot, 6, theme.surface);
        canvas.CenteredText(Tr(index == 0 ? TextKey::PricesPreviousPage : TextKey::PricesNextPage),
            canvas.smallFormat, slot, enabled ? theme.primaryText : theme.secondaryText);
    }
    canvas.CenteredText(UiLocalization().Format(TextKey::PricesPage, {
        {L"page", std::to_wstring(page + 1)}, {L"pages", std::to_wstring(PricePageCount(total))},
        {L"total", std::to_wstring(total)}}), canvas.smallFormat, PricePagerSlot(rect, 1), theme.secondaryText);
}
}
