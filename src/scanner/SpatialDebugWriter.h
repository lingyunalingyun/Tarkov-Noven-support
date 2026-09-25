#pragma once

// 将 ROI 局部文字框、屏幕坐标中的扩展步骤与面板预测绘成离线调试图。
// Render ROI-local text boxes, screen-space expansion steps, and panel predictions
// into an offline debug image; none of this participates in matching.

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"
#include "scanner/AdaptiveTextExpansion.h"
#include "scanner/LocalTextGrouping.h"
#include "scanner/SpatialCandidateSelector.h"

#include <filesystem>
#include <optional>
#include <span>

namespace noven::scanner {

bool WriteSpatialAnnotatedBmp(
    const std::filesystem::path& path,
    const capture::CapturedFrame& frame,
    AnchorPoint anchor,
    const ScannerProfile& profile,
    std::span<const ocr::RecognizedText> recognized_texts,
    const ScanResult& result,
    std::wstring& error,
    std::optional<ocr::TextBox> tooltip_region = std::nullopt,
    std::optional<AdaptiveTextAnalysis> adaptive_analysis = std::nullopt,
    std::span<const LockedRoiStep> locked_roi_steps = {},
    capture::Point frame_origin = {},
    std::optional<OrderedTextAssembly> text_assembly = std::nullopt,
    std::optional<capture::Rect> predicted_tooltip = std::nullopt,
    std::optional<long> monitor_right = std::nullopt
);

} // namespace noven::scanner
