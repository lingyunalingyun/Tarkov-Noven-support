#pragma once

// 用 F2 时记录的游戏鼠标坐标预测工具提示框，而非截图查看器中的鼠标位置。
// Predict the tooltip from the in-game cursor recorded at F2, never a screenshot viewer cursor.

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"

namespace noven::scanner {

// 常态在鼠标右上；预计越过当前显示器右边界时，尝试向内偏移。
// Normally upper-right of the cursor; shift inward when predicted to overflow the monitor.
enum class TooltipPlacement {
    DefaultRightUpper,
    RightEdgeShiftedLeft,
};

struct TooltipPlacementProfile final {
    // 偏移和宽度只是预测值；只能用真实游戏 F2 时的鼠标/面板位置校准。
    // Offsets and widths are predictions; calibrate only from in-game F2
    // cursor/panel positions, not screenshot-viewer cursors.
    long offsetX{12};
    long offsetY{-93};
    long initialWidth{300};
    long initialHeight{80};
    long expectedPanelWidth{420};
    long shiftedRightOffset{40};
};

struct TooltipPlacementPrediction final {
    TooltipPlacement placement{TooltipPlacement::DefaultRightUpper};
    // 使用虚拟桌面屏幕坐标；可在副屏上为负值。
    // Uses virtual-desktop screen coordinates, which may be negative on secondary monitors.
    capture::Rect roi{};
};

[[nodiscard]] TooltipPlacementPrediction PredictTooltipPlacement(
    capture::Point cursor,
    capture::Rect monitor,
    const TooltipPlacementProfile& profile = {}
) noexcept;

// 鼠标点和面板矩形必须同属一个坐标系；面板检测时两者都为 ROI 局部坐标。
// Cursor and panel must share a coordinate space; during panel detection both
// are ROI-local. The score measures predicted placement and proximity.
[[nodiscard]] float TooltipGeometryConfidence(
    capture::Point cursor,
    const ocr::TextBox& panel,
    TooltipPlacement placement
) noexcept;

[[nodiscard]] const wchar_t* TooltipPlacementName(TooltipPlacement placement) noexcept;

} // namespace noven::scanner
