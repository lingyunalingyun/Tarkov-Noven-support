#pragma once

#include "ui/TabBar.h"

namespace noven::ui {

// 坐标为客户区 DIP；maximum 是内容偏移上限，不是轨道像素长度。
// Coordinates are client-area DIPs; maximum is the content offset limit, not track length.
struct ScrollbarGeometry final {
    D2D1_RECT_F track{};
    D2D1_RECT_F thumb{};
    float maximum{};

    [[nodiscard]] float OffsetFromThumbTop(float top) const noexcept {
        const float travel = (track.bottom - track.top) - (thumb.bottom - thumb.top);
        return travel > 0.0F
            ? std::clamp((top - track.top) / travel, 0.0F, 1.0F) * maximum : 0.0F;
    }
};

struct ScrollbarPose final {
    std::optional<ScrollbarGeometry> bar;
    float opacity{};
};

// 轨道覆盖可视区高度；内容不足一屏时没有滚动条。绘制和命中共用此几何。
// The track spans the viewport height; fitting content has no scrollbar. Share this geometry with hit testing.
[[nodiscard]] inline std::optional<ScrollbarGeometry> MakeScrollbar(
    D2D1_RECT_F track, float contentHeight, float scroll) noexcept {
    const float viewport = track.bottom - track.top;
    const float maximum = (std::max)(0.0F, contentHeight - viewport);
    if (maximum <= 0.0F || viewport <= 0.0F) return std::nullopt;
    const float thumbHeight = (std::min)(viewport,
        (std::max)(28.0F, viewport * viewport / (viewport + maximum)));
    const float top = track.top + (viewport - thumbHeight)
        * std::clamp(scroll / maximum, 0.0F, 1.0F);
    return ScrollbarGeometry{track,
        D2D1::RectF(track.left, top, track.right, top + thumbHeight), maximum};
}

// 两侧都有滑块时弹性插值；仅一侧有滑块时跟随内容淡入/淡出。
// Morph elastically when both thumbs exist; otherwise fade with incoming/outgoing content.
// 过渡结果只用于显示；落定前不要用它反推目标列表的拖动偏移。
// Transitional geometry is visual only; do not use it to map dragging into the target list.
[[nodiscard]] inline ScrollbarPose SampleScrollbarTransition(
    const std::optional<ScrollbarGeometry>& outgoing,
    const std::optional<ScrollbarGeometry>& incoming, float progress) noexcept {
    if (progress >= 1.0F) return {incoming, incoming ? 1.0F : 0.0F};
    const auto pose = SampleTabTransition(progress);
    if (!incoming) return {outgoing, pose.outgoingOpacity};
    if (!outgoing) return {incoming, pose.incomingOpacity};
    auto bar = *incoming;
    const float t = pose.underlineProgress;
    const float oldHeight = outgoing->thumb.bottom - outgoing->thumb.top;
    const float newHeight = incoming->thumb.bottom - incoming->thumb.top;
    const float height = std::clamp(oldHeight + (newHeight - oldHeight) * t,
        (std::min)(28.0F, bar.track.bottom - bar.track.top),
        bar.track.bottom - bar.track.top);
    bar.thumb.top = std::clamp(outgoing->thumb.top
        + (incoming->thumb.top - outgoing->thumb.top) * t,
        bar.track.top, bar.track.bottom - height);
    bar.thumb.bottom = bar.thumb.top + height;
    return {bar, 1.0F};
}

inline void DrawScrollbar(const UiCanvas& canvas, const UiTheme& theme,
                          const ScrollbarPose& pose) {
    if (!pose.bar || pose.opacity <= 0.0F) return;
    const auto& bar = *pose.bar;
    const float originalOpacity = canvas.brush.GetOpacity();
    canvas.brush.SetOpacity(originalOpacity * pose.opacity);
    canvas.Round(D2D1::RectF(bar.track.left + 4, bar.track.top,
        bar.track.right - 4, bar.track.bottom), 2.0F, theme.surface);
    canvas.Round(D2D1::RectF(bar.thumb.left + 3, bar.thumb.top,
        bar.thumb.right - 3, bar.thumb.bottom), 3.0F, theme.secondaryText);
    canvas.brush.SetOpacity(originalOpacity);
}

} // namespace noven::ui
