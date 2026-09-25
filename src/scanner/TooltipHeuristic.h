#pragma once

// 先定位深色低饱和度工具提示面板，再在其内部确定标题裁剪区。
// Locate a dark, low-saturation tooltip panel before deriving a title crop inside it.

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"
#include "scanner/SpatialCandidateSelector.h"
#include "scanner/TooltipPlacementModel.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace noven::scanner {

// 工具提示精确裁剪是主路径；自适应文字扩展仅作回退。
// Precise tooltip cropping is primary; adaptive text expansion is a fallback.
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
    // rect 为当前捕获 ROI 的局部坐标，不能直接当作虚拟桌面坐标。
    // rect is local to the captured ROI, not directly a virtual-desktop rectangle.
    ocr::TextBox rect;
    float borderConfidence{};
    float backgroundConfidence{};
    float textConfidence{};
    float panelConfidence{};
    float proximityToCursor{};
    float geometryConfidence{};
    // 仅真实观察到的边缘算作面板边界；ROI 截断边绝不证明面板完整。
    // Only observed edges count as panel borders; an ROI edge never proves completeness.
    bool hasLeftBorder{};
    bool hasRightBorder{};
    bool hasTopBorder{};
    bool hasBottomBorder{};
    bool clippedLeft{};
    bool clippedRight{};
    bool clippedTop{};
    bool clippedBottom{};

    // 四边均已观察到且未截断，才允许跳过最小边缘恢复。
    // Skip minimal edge recovery only when all four borders are observed and unclipped.
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

// 暗背景、边缘、内部文字及相对鼠标几何分别提供证据；优先确定区域再 OCR。
// Dark background, borders, contained text, and cursor geometry contribute separately;
// determine the region before OCR where possible.
[[nodiscard]] std::optional<TooltipBoxCandidate> DetectTooltipBox(
    const capture::CapturedFrame& frame,
    AnchorPoint anchor,
    TooltipPlacement placement = TooltipPlacement::DefaultRightUpper
);

// 只恢复缺失的面板边，并受最大尺寸和虚拟桌面边界约束。
// Recover only missing panel edges, bounded by maximum size and virtual desktop.
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

// 标题裁剪矩形始终限制在面板局部边界内。
// Keep the title crop within panel-local bounds.
[[nodiscard]] std::optional<ocr::TextBox> ClampTooltipTitleCrop(
    const ocr::TextBox& title,
    const ocr::TextBox& panel,
    float padding
) noexcept;

[[nodiscard]] const wchar_t* InventoryRecognitionPathName(
    InventoryRecognitionPath path
) noexcept;

} // namespace noven::scanner
