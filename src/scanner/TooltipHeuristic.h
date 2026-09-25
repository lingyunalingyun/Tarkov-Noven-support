#pragma once

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"
#include "scanner/SpatialCandidateSelector.h"
#include "scanner/TooltipPlacementModel.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace noven::scanner {

enum class InventoryRecognitionPath {
    PrimaryTooltip,
    AdaptiveFallback,
};

enum class TooltipPrimaryAction {
    PreciseCrop,
    RecoverPanel,
    AdaptiveFallback,
};

struct TooltipPrimaryProfile final {
    capture::Size initialSize{300, 80};
    capture::Size maximumRecoverySize{600, 240};
    long recoveryWidthStep{160};
    long recoveryHeightStep{80};
    float minimumPanelConfidence{0.55F};
    int maximumRecoveryAttempts{2};
    long cropPadding{6};
};

struct TooltipBoxCandidate final {
    ocr::TextBox rect;
    float borderConfidence{};
    float backgroundConfidence{};
    float textConfidence{};
    float panelConfidence{};
    float proximityToCursor{};
    float geometryConfidence{};
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

struct TooltipPrimaryDecision final {
    TooltipPrimaryAction action{TooltipPrimaryAction::AdaptiveFallback};
    capture::Rect nextRoi;
    const wchar_t* reason{L"no_panel"};
};

[[nodiscard]] TooltipPrimaryProfile DefaultTooltipPrimaryProfile() noexcept;

[[nodiscard]] capture::Size InventoryPrimaryTooltipSize(
    capture::Size maximum_size,
    const TooltipPrimaryProfile& profile = DefaultTooltipPrimaryProfile()
) noexcept;

[[nodiscard]] bool IsBetterTooltipProbe(
    const TooltipBoxCandidate& candidate,
    TooltipPlacement placement,
    const TooltipBoxCandidate& current,
    TooltipPlacement current_placement
) noexcept;

[[nodiscard]] std::optional<TooltipBoxCandidate> DetectTooltipBox(
    const capture::CapturedFrame& frame,
    AnchorPoint anchor,
    TooltipPlacement placement = TooltipPlacement::DefaultRightUpper
);

[[nodiscard]] capture::Rect ExpandTooltipRoi(
    capture::Rect current,
    const TooltipBoxCandidate& candidate,
    capture::Size maximum_size,
    capture::Rect virtual_screen
) noexcept;

[[nodiscard]] TooltipPrimaryDecision DecideTooltipPrimaryPath(
    const std::optional<TooltipBoxCandidate>& candidate,
    capture::Rect current_roi,
    capture::Rect virtual_screen,
    int recovery_attempt,
    const TooltipPrimaryProfile& profile = DefaultTooltipPrimaryProfile()
) noexcept;

[[nodiscard]] float TooltipTextBackgroundConfidence(
    const capture::CapturedFrame& frame,
    const ocr::TextBox& box
) noexcept;

[[nodiscard]] bool IsLikelyTooltipNameFragment(std::string_view text);

[[nodiscard]] std::vector<ocr::RecognizedText> FilterTooltipNameFragments(
    const capture::CapturedFrame& frame,
    std::span<const ocr::RecognizedText> texts,
    float minimum_background_confidence = 0.30F
);

[[nodiscard]] const wchar_t* TooltipPrimaryActionName(
    TooltipPrimaryAction action
) noexcept;

[[nodiscard]] std::optional<ocr::TextBox> DeriveTooltipTitleBand(
    std::span<const ocr::TextBox> boxes,
    capture::Size frame_size
);

[[nodiscard]] std::optional<ocr::TextBox> ClampTooltipTitleCrop(
    const ocr::TextBox& title,
    const ocr::TextBox& panel,
    float padding
) noexcept;

[[nodiscard]] const wchar_t* InventoryRecognitionPathName(
    InventoryRecognitionPath path
) noexcept;

} // namespace noven::scanner
