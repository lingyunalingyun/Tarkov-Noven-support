#pragma once

#include "capture/CaptureTypes.h"
#include "ocr/OcrTypes.h"
#include "scanner/AdaptiveTextExpansion.h"
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
    std::optional<AdaptiveTextAnalysis> adaptive_analysis = std::nullopt
);

} // namespace noven::scanner
