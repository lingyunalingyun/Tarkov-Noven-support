#pragma once

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"

namespace noven::scanner {

enum class TooltipPlacement {
    DefaultRightUpper,
    RightEdgeShiftedLeft,
};

struct TooltipPlacementProfile final {
    // Provisional until cursor/panel offsets from real EFT F2 scans are available.
    long offsetX{12};
    long offsetY{-93};
    long initialWidth{300};
    long initialHeight{80};
    long expectedPanelWidth{420};
    long shiftedRightOffset{40};
};

struct TooltipPlacementPrediction final {
    TooltipPlacement placement{TooltipPlacement::DefaultRightUpper};
    capture::Rect roi{};
};

[[nodiscard]] TooltipPlacementPrediction PredictTooltipPlacement(
    capture::Point cursor,
    capture::Rect monitor,
    const TooltipPlacementProfile& profile = {}
) noexcept;

[[nodiscard]] float TooltipGeometryConfidence(
    capture::Point cursor,
    const ocr::TextBox& panel,
    TooltipPlacement placement
) noexcept;

[[nodiscard]] const wchar_t* TooltipPlacementName(TooltipPlacement placement) noexcept;

} // namespace noven::scanner
