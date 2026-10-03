#include "scanner/TooltipPlacementModel.h"

#include <algorithm>
#include <cmath>

namespace noven::scanner {

TooltipPlacementPrediction PredictTooltipPlacement(
    capture::Point cursor,
    capture::Rect monitor,
    const TooltipPlacementProfile& profile
) noexcept {
    // 只在预计正常右侧面板越过鼠标所在显示器时向内偏移；不常态探测左侧。
    // Shift inward only when the normal right-side panel would overflow the
    // cursor's monitor; do not routinely probe left.
    const bool shifted = cursor.x + profile.offsetX + profile.expectedPanelWidth
        > monitor.right;
    const long left = shifted
        ? cursor.x + profile.shiftedRightOffset - profile.initialWidth
        : cursor.x + profile.offsetX;
    const long top = cursor.y + profile.offsetY;
    return {
        shifted ? TooltipPlacement::RightEdgeShiftedLeft
                : TooltipPlacement::DefaultRightUpper,
        {
            std::clamp(left, monitor.left, monitor.right),
            std::clamp(top, monitor.top, monitor.bottom),
            std::clamp(left + profile.initialWidth, monitor.left, monitor.right),
            std::clamp(top + profile.initialHeight, monitor.top, monitor.bottom),
        },
    };
}

std::optional<TooltipPlacementPrediction> RightTooltipAlternative(
    capture::Point cursor, capture::Rect monitor, TooltipPlacement primary
) noexcept {
    if (primary != TooltipPlacement::RightEdgeShiftedLeft) return std::nullopt;
    TooltipPlacementProfile profile;
    profile.expectedPanelWidth = 0;
    auto prediction = PredictTooltipPlacement(cursor, monitor, profile);
    if (prediction.placement != TooltipPlacement::DefaultRightUpper
        || prediction.roi.right - prediction.roi.left < 40
        || prediction.roi.Empty()) return std::nullopt;
    return prediction;
}

float TooltipGeometryConfidence(
    capture::Point cursor,
    const ocr::TextBox& panel,
    TooltipPlacement placement
) noexcept {
    // 该分数仅评价面板相对鼠标的方位和距离，不代表文字识别质量。
    // This score measures panel placement/proximity, not OCR quality.
    if (panel.x2 <= panel.x1 || panel.y2 <= panel.y1) return 0.0F;
    const bool above = panel.y1 < static_cast<float>(cursor.y)
        && panel.y2 <= static_cast<float>(cursor.y + 24);
    const bool on_expected_side = placement == TooltipPlacement::DefaultRightUpper
        ? panel.x1 >= static_cast<float>(cursor.x + 4)
        : panel.x1 < static_cast<float>(cursor.x)
            && panel.x2 >= static_cast<float>(cursor.x - 80);
    const float horizontal_gap = placement == TooltipPlacement::DefaultRightUpper
        ? std::max(0.0F, panel.x1 - static_cast<float>(cursor.x))
        : std::max(0.0F, static_cast<float>(cursor.x) - panel.x2);
    const float vertical_gap = std::max(0.0F,
        static_cast<float>(cursor.y) - panel.y2);
    const float proximity = std::clamp(
        1.0F - std::hypot(horizontal_gap, vertical_gap) / 220.0F,
        0.0F, 1.0F);
    if (!on_expected_side) return 0.25F * proximity;
    return (on_expected_side ? 0.45F : 0.0F)
        + (above ? 0.30F : 0.0F) + 0.25F * proximity;
}

const wchar_t* TooltipPlacementName(TooltipPlacement placement) noexcept {
    // Unknown 仅是无效枚举值的防御性日志文本，不是第三种实际预测模式。
    // Unknown is defensive log text for an invalid enum value, not a third prediction mode.
    switch (placement) {
    case TooltipPlacement::DefaultRightUpper: return L"DefaultRightUpper";
    case TooltipPlacement::RightEdgeShiftedLeft: return L"RightEdgeShiftedLeft";
    }
    return L"Unknown";
}

} // namespace noven::scanner
