#pragma once

#include "ui/UiCanvas.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string_view>

namespace noven::ui {

// 下划线标签模板：所有尺寸为 DIP，文字与下划线共用标签槽位中心。
// Underline-tab template: all dimensions are DIPs; label and underline share
// the same slot center. Selection has no filled background.
struct TabBarLayout final {
    float left{};
    float top{};
    float bottom{};
    float itemWidth{};
    float underlineHalfWidth{};
};

template <typename Id>
struct TabBarItem final {
    Id id;
    std::wstring_view label;
};

struct TabTransitionPose final {
    float textProgress{};
    float underlineProgress{};
    float outgoingOpacity{};
    float incomingOpacity{};
    float outgoingScale{};
    float incomingScale{};
};

// 文字先变色，底线弹性滑动；内容先缩小淡出，再放大淡入。
// Recolor first, spring the underline, then shrink/fade out and grow/fade in.
// 调用方按时间推进 progress，并将透明度/缩放应用于自己的内容区域。
// The caller advances progress by elapsed time and applies opacity/scale to its content.
[[nodiscard]] inline TabTransitionPose SampleTabTransition(float progress) noexcept {
    const auto smooth = [](float value) {
        const float t = std::clamp(value, 0.0F, 1.0F);
        return t * t * (3.0F - 2.0F * t);
    };
    const float p = std::clamp(progress, 0.0F, 1.0F);
    const float lineTime = std::clamp((p - 0.08F) / 0.72F, 0.0F, 1.0F);
    const float line = lineTime >= 1.0F ? 1.0F
        : 1.0F - std::exp(-6.0F * lineTime) * std::cos(7.0F * lineTime);
    const float fadeOut = smooth(p / 0.42F);
    const float fadeIn = smooth((p - 0.42F) / 0.58F);
    return {smooth(p / 0.25F), line, 1.0F - fadeOut, fadeIn,
            1.0F - 0.035F * fadeOut, 0.965F + 0.035F * fadeIn};
}

template <typename Id, std::size_t Count>
[[nodiscard]] std::optional<Id> HitTestTabBar(
    const std::array<TabBarItem<Id>, Count>& items,
    TabBarLayout layout, float x, float y) noexcept {
    if (y < layout.top || y >= layout.bottom || x < layout.left
        || x >= layout.left + layout.itemWidth * static_cast<float>(Count))
        return std::nullopt;
    return items[static_cast<std::size_t>((x - layout.left) / layout.itemWidth)].id;
}

template <typename Id, std::size_t Count>
void DrawTabBar(const UiCanvas& canvas, const UiTheme& theme,
    IDWriteTextFormat& font, const std::array<TabBarItem<Id>, Count>& items,
    TabBarLayout layout, Id selected, std::optional<Id> hovered, Id outgoing,
    float progress, float underlineIndex) {
    const auto pose = SampleTabTransition(progress);
    const auto previousAlignment = font.GetTextAlignment();
    font.SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    for (std::size_t index = 0; index < Count; ++index) {
        const auto& item = items[index];
        const float left = layout.left + static_cast<float>(index) * layout.itemWidth;
        const float selectedWeight = progress < 1.0F
            ? (item.id == selected ? pose.textProgress
                : (item.id == outgoing ? 1.0F - pose.textProgress : 0.0F))
            : (item.id == selected ? 1.0F : 0.0F);
        const auto from = hovered == item.id ? theme.accent : theme.secondaryText;
        const auto to = theme.primaryText;
        const auto color = D2D1::ColorF(
            from.r + (to.r - from.r) * selectedWeight,
            from.g + (to.g - from.g) * selectedWeight,
            from.b + (to.b - from.b) * selectedWeight,
            from.a + (to.a - from.a) * selectedWeight);
        canvas.Text(item.label, font,
            D2D1::RectF(left, layout.top, left + layout.itemWidth, layout.bottom - 5.0F),
            color);
    }
    font.SetTextAlignment(previousAlignment);
    const float center = layout.left + layout.itemWidth * (underlineIndex + 0.5F);
    canvas.brush.SetColor(theme.accent);
    canvas.target.DrawLine(
        D2D1::Point2F(center - layout.underlineHalfWidth, layout.bottom - 2.0F),
        D2D1::Point2F(center + layout.underlineHalfWidth, layout.bottom - 2.0F),
        &canvas.brush, 2.0F);
}

} // namespace noven::ui
