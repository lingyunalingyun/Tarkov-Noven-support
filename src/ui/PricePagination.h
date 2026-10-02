#pragma once
#include "ui/UiCanvas.h"
#include "ui/Theme.h"
#include "ui/SearchBox.h"
#include "ui/localization/LocalizationService.h"
#include <algorithm>
#include <optional>

namespace noven::ui {
inline constexpr std::size_t PricePageSize = 30;
inline constexpr float PricePagerHeight = 44;
inline std::size_t PricePageCount(std::size_t total) noexcept {
    return (std::max)(std::size_t{1}, total / PricePageSize + (total % PricePageSize != 0));
}
// 页码按总页数饱和解析，避免超长输入溢出；空输入取消跳转。
// Saturate parsing at the page count to avoid overflow; empty input cancels a jump.
inline std::optional<std::size_t> PricePageFromInput(std::wstring_view input, std::size_t total) {
    if (input.empty()) return {};
    const auto pages = PricePageCount(total);
    std::size_t value = 0;
    for (const auto digit : input) {
        if (digit < L'0' || digit > L'9') return {};
        value = (std::min)(pages, value * 10 + static_cast<std::size_t>(digit - L'0'));
    }
    return (std::max)(std::size_t{1}, value) - 1;
}
// 常驻页头共用绘制/命中槽位，窄窗口也不依赖固定像素宽度。
// The sticky header shares draw/hit slots without fixed pixel widths in narrow windows.
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
    if (x >= PricePagerSlot(rect, 1).left && x < PricePagerSlot(rect, 1).right) return 0;
    return {};
}
inline void DrawPricePager(const UiCanvas& canvas, const UiTheme& theme,
                          D2D1_RECT_F rect, std::size_t page, std::size_t total,
                          const SearchBox* input = nullptr, bool caretVisible = false) {
    for (int index : {0, 2}) {
        const auto slot = PricePagerSlot(rect, index);
        const bool enabled = index == 0 ? page > 0 : page + 1 < PricePageCount(total);
        canvas.Round(slot, 6, theme.surface);
        canvas.CenteredText(Tr(index == 0 ? TextKey::PricesPreviousPage : TextKey::PricesNextPage),
            canvas.smallFormat, slot, enabled ? theme.primaryText : theme.secondaryText);
    }
    const auto center = PricePagerSlot(rect, 1);
    if (input && input->Focused()) {
        input->Draw(canvas, theme, center, Tr(TextKey::PricesJumpPage), caretVisible);
        return;
    }
    canvas.Round(center, 6, theme.surface);
    canvas.CenteredText(UiLocalization().Format(TextKey::PricesPage, {
        {L"page", std::to_wstring(page + 1)}, {L"pages", std::to_wstring(PricePageCount(total))},
        {L"total", std::to_wstring(total)}}), canvas.smallFormat, PricePagerSlot(rect, 1), theme.secondaryText);
}
}
