#pragma once

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"
#include "scanner/SpatialCandidateSelector.h"

#include <optional>

namespace noven::scanner {

struct TooltipBoxCandidate final {
    ocr::TextBox rect;
    float borderConfidence{};
    float proximityToCursor{};
    bool hasLeftBorder{};
    bool hasRightBorder{};
    bool hasTopBorder{};
    bool hasBottomBorder{};
    bool clippedLeft{};
    bool clippedRight{};
    bool clippedTop{};
    bool clippedBottom{};

    [[nodiscard]] bool FullBox() const noexcept {
        return hasLeftBorder && hasRightBorder && hasTopBorder && hasBottomBorder
            && !clippedLeft && !clippedRight && !clippedTop && !clippedBottom;
    }
};

[[nodiscard]] std::optional<TooltipBoxCandidate> DetectTooltipBox(
    const capture::CapturedFrame& frame,
    AnchorPoint anchor
);

[[nodiscard]] capture::Rect ExpandTooltipRoi(
    capture::Rect current,
    const TooltipBoxCandidate& candidate,
    capture::Size maximum_size,
    capture::Rect virtual_screen
) noexcept;

} // namespace noven::scanner
