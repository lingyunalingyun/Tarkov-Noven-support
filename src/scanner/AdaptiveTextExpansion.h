#pragma once

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"
#include "scanner/SpatialCandidateSelector.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace noven::scanner {

enum class ExpansionSide {
    None,
    Left,
    Right,
    Up,
    Down,
};

enum class LockedScanStage {
    HorizontalExpansion,
    VerticalExpansion,
    TextAssembly,
    CatalogMatch,
    Complete,
    Failed,
};

struct TooltipExpansionEvidence final {
    bool present{};
    bool complete{};
    bool hasLeftBorder{};
    bool hasRightBorder{};
    bool hasTopBorder{};
    bool hasBottomBorder{};
    bool clippedRight{};
    bool clippedTop{};
    bool clippedBottom{};
};

struct AdaptiveTextExpansionProfile final {
    float fixedMinimumGap{14.0F};
    float textHeightGapMultiplier{2.5F};
    float medianGapMultiplier{2.5F};
    float boundaryMarginMultiplier{1.25F};
    float maximumHeightRatio{2.0F};
    float alignmentToleranceMultiplier{0.8F};
    long minimumExpansionPixels{64};
    float expansionHeightMultiplier{4.0F};
    int maximumExpansionCount{6};
    long maximumWidth{600};
    long maximumHeight{320};
    std::size_t maximumRoiPixels{192000};
    std::size_t maximumTotalCapturedPixels{800000};
};

struct AdaptiveTextAnalysis final {
    std::vector<std::size_t> localBoxIndices;
    std::optional<ocr::TextBox> localBounds;
    float localMedianHeight{};
    float medianLocalGap{};
    float nearestOutsideGap{};
    float stopThreshold{};
    float safeMargin{};
    float leftTextMargin{};
    float rightTextMargin{};
    float topTextMargin{};
    float bottomTextMargin{};
    float rightmostTextX{};
    ExpansionSide expansionSide{ExpansionSide::None};
    bool shouldExpand{};
    bool needsExpandLeft{};
    bool needsExpandRight{};
    bool needsExpandTop{};
    bool needsExpandBottom{};
    bool noContinuationAfterExpansion{};
    bool widthCompletedBySafetyLimit{};
    bool heightCompletedBySafetyLimit{};
    bool trustedTooltipRightBorder{};
    float tooltipRightBorderX{};
    std::optional<capture::Rect> nextRoi;
    bool horizontalComplete{};
    bool verticalComplete{};
    bool stoppedByLargeGap{};
    bool boundedByTooltip{};
};

struct LockedScanDecision final {
    LockedScanStage nextStage{LockedScanStage::HorizontalExpansion};
    ExpansionSide expansionSide{ExpansionSide::None};
    bool shouldExpand{};
    const wchar_t* reason{L"none"};
};

struct LockedRoiStep final {
    capture::Rect roi;
    LockedScanStage stage{LockedScanStage::HorizontalExpansion};
};

[[nodiscard]] AdaptiveTextExpansionProfile DefaultAdaptiveTextExpansionProfile() noexcept;

[[nodiscard]] AdaptiveTextAnalysis AnalyzeTextContinuity(
    std::span<const ocr::TextBox> boxes,
    AnchorPoint anchor,
    capture::Size roi_size,
    ScanDirection direction,
    std::optional<ocr::TextBox> tooltip_bounds = std::nullopt,
    const AdaptiveTextExpansionProfile& profile =
        DefaultAdaptiveTextExpansionProfile()
);

[[nodiscard]] capture::Rect ExpandAdaptiveTextRoi(
    capture::Rect current_roi,
    ExpansionSide side,
    float local_text_height,
    capture::Rect virtual_screen,
    const AdaptiveTextExpansionProfile& profile =
        DefaultAdaptiveTextExpansionProfile()
) noexcept;

[[nodiscard]] LockedScanDecision DecideLockedScanStep(
    LockedScanStage stage,
    const AdaptiveTextAnalysis& analysis,
    TooltipExpansionEvidence tooltip = {}
) noexcept;

[[nodiscard]] float RectangleDistanceToPoint(
    const ocr::TextBox& box,
    AnchorPoint point
) noexcept;

[[nodiscard]] const wchar_t* ExpansionSideName(ExpansionSide side) noexcept;
[[nodiscard]] const wchar_t* LockedScanStageName(LockedScanStage stage) noexcept;

} // namespace noven::scanner
