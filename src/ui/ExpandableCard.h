#pragma once
#include <algorithm>
#include <cstddef>

namespace noven::ui {
// 仅管理 UI 高度与定位；页面持有状态、驱动帧更新并负责内容生命周期。所有长度均为 DIP。
// UI-only height and anchoring state; the page owns it, ticks frames and manages content lifetime. Lengths are DIPs.
struct ExpandableCardState {
    float extent{};
    float extentFrom{};
    float expansionProgress{1};
    std::size_t rowIndex{};
    bool open{};

    void Retarget(bool expanded, std::size_t index) {
        open = expanded;
        rowIndex = index;
        extentFrom = extent;
        expansionProgress = 0;
    }

    void Advance(float seconds, float expandedHeight) {
        expansionProgress = (std::min)(1.0F, expansionProgress + seconds / 0.30F);
        const float target = open ? expandedHeight : 0;
        const float p = expansionProgress;
        const float eased = 1 - (1 - p) * (1 - p) * (1 - p);
        extent = expansionProgress >= 1 ? target : extentFrom + (target - extentFrom) * eased;
    }

    // 固定高度列表的额外滚动空间，与绘制、拖动和边界钳制共用；不会重排数据。
    // Extra scroll space for uniform-height rows; share with drawing, dragging and clamping, without reordering data.
    [[nodiscard]] float ScrollExtra(std::size_t count, float rowHeight,
        float viewportHeight, float expandedHeight) const {
        const float required = (std::max)(0.0F, ScrollTarget(rowHeight) + viewportHeight - count * rowHeight);
        const float weight = open ? 1.0F : std::clamp(extent / expandedHeight, 0.0F, 1.0F);
        return (std::max)(extent, required * weight);
    }

    [[nodiscard]] float ScrollTarget(float rowHeight) const { return static_cast<float>(rowIndex) * rowHeight; }
};
}
