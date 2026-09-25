#pragma once

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
