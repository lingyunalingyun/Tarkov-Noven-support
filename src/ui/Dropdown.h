#pragma once

#include "ui/UiCanvas.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace noven::ui {

// 所有坐标均为 DIP；绘制和命中检测共用同一布局。
// All coordinates are DIPs; rendering and hit testing share this layout.
struct DropdownLayout final {
    D2D1_RECT_F header;
    [[nodiscard]] D2D1_RECT_F Option(std::size_t row) const noexcept {
        return D2D1::RectF(header.left + 4, header.bottom + 12 + row * 32,
            header.right - 4, header.bottom + 12 + (row + 1) * 32);
    }
    [[nodiscard]] D2D1_RECT_F Panel(std::size_t rows) const noexcept {
        return D2D1::RectF(header.left, header.bottom + 8,
            header.right, header.bottom + 16 + rows * 32);
    }
};

struct DropdownPose final { float opacity; float scale; };

[[nodiscard]] inline bool HitTestDropdownRect(D2D1_RECT_F rect,
    float x, float y, bool enabled = true) noexcept {
    return enabled && x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

// 关闭时从当前进度反向播放，不跳回完全展开状态。
// Closing reverses from the current progress without jumping to fully open.
[[nodiscard]] inline float AdvanceDropdownTransition(
    float progress, bool closing, float elapsedSeconds) noexcept {
    return std::clamp(progress + (closing ? -elapsedSeconds / 0.14F
        : elapsedSeconds / 0.24F), 0.0F, 1.0F);
}

// 调用方按时间推进进度；组件只定义已验收的淡入与轻弹性曲线。
// The caller advances time; the component defines the accepted fade and spring curves.
[[nodiscard]] inline DropdownPose SampleDropdownTransition(float progress) noexcept {
    const float p = std::clamp(progress, 0.0F, 1.0F);
    const float spring = p >= 1 ? 1 : 1 - std::exp(-6 * p) * std::cos(7 * p);
    return {p * p * (3 - 2 * p), 0.96F + 0.04F * spring};
}

// 文案由调用方本地化，组件不持有价格、扫描或服务状态。仅在 UI 线程绘制。
// Labels are localized by the caller; no price, scanner or service state is owned here.
// Draw only on the UI thread.
inline void DrawDropdownHeader(const UiCanvas& canvas, const UiTheme& theme,
    D2D1_RECT_F rect, std::wstring_view label, bool selected, bool hot) {
    canvas.Round(rect, 6.0F, selected ? theme.selected : hot ? theme.hover : theme.surface);
    canvas.Text(label, canvas.smallFormat,
        D2D1::RectF(rect.left + 10, rect.top, rect.right - 22, rect.bottom),
        selected ? theme.accent : theme.secondaryText);
    const float cx = rect.right - 13;
    const float cy = (rect.top + rect.bottom) / 2;
    const float direction = selected ? -1.0F : 1.0F;
    canvas.brush.SetColor(selected ? theme.accent : theme.secondaryText);
    canvas.target.DrawLine(D2D1::Point2F(cx - 3, cy - direction * 1.5F),
        D2D1::Point2F(cx, cy + direction * 1.5F), &canvas.brush, 1.4F);
    canvas.target.DrawLine(D2D1::Point2F(cx, cy + direction * 1.5F),
        D2D1::Point2F(cx + 3, cy - direction * 1.5F), &canvas.brush, 1.4F);

}

inline void DrawDropdownPanel(const UiCanvas& canvas, const UiTheme& theme,
    DropdownLayout layout, std::size_t rows) {
    const auto panel = layout.Panel(rows);
    canvas.Round(panel, 7.0F, theme.surface);
    canvas.brush.SetColor(theme.divider);
    canvas.target.DrawRoundedRectangle(D2D1::RoundedRect(panel, 7, 7), &canvas.brush, 1.0F);
}

inline void DrawDropdownOption(const UiCanvas& canvas, const UiTheme& theme,
    D2D1_RECT_F rect, std::wstring_view label, bool selected, bool enabled, bool hovered) {
    const bool hot = enabled && hovered;
    if (hot) canvas.Round(rect, 4.0F, theme.hover);
    auto color = selected ? theme.accent : theme.secondaryText;
    if (!enabled) color.a *= 0.4F;
    canvas.Text(label, canvas.smallFormat,
        D2D1::RectF(rect.left + 6, rect.top, rect.right - 18, rect.bottom), color);
    if (selected) {
        const float cx = rect.right - 9;
        const float cy = (rect.top + rect.bottom) / 2;
        canvas.brush.SetColor(theme.accent);
        canvas.target.DrawLine(D2D1::Point2F(cx - 4, cy),
            D2D1::Point2F(cx - 1, cy + 3), &canvas.brush, 1.5F);
        canvas.target.DrawLine(D2D1::Point2F(cx - 1, cy + 3),
            D2D1::Point2F(cx + 4, cy - 3), &canvas.brush, 1.5F);
    }

}

} // namespace noven::ui
