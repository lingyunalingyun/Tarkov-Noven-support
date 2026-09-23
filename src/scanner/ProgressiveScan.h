#pragma once

#include "capture/CaptureTypes.h"
#include "scanner/SpatialCandidateSelector.h"

#include <cstddef>
#include <optional>

namespace noven::scanner {

constexpr int kInventoryScanLevelCount = 3;
constexpr int kInventoryDirectionalSearchDepthCount = 3;

struct ProgressiveScanPosition final {
    int level{};
    std::size_t direction_index{};
    int search_depth{};
};

[[nodiscard]] bool ShouldInitializeDirectionalRoi(
    int tooltip_expansion_attempt,
    int adaptive_expansion_count
) noexcept;

[[nodiscard]] std::optional<ProgressiveScanPosition> NextInventoryScanPosition(
    ProgressiveScanPosition current,
    std::size_t direction_count,
    bool direction_locked = false
) noexcept;

[[nodiscard]] capture::Size InventoryScanSizeForLevel(
    capture::Size maximum_size,
    int level
) noexcept;

[[nodiscard]] capture::Size DirectionalScanSizeForDepth(
    capture::Size maximum_size,
    int search_depth
) noexcept;

[[nodiscard]] capture::Rect CalculateDirectionalRoi(
    capture::Point anchor,
    ScanDirection direction,
    capture::Size size,
    capture::Rect virtual_screen,
    long anchor_gap = 8
) noexcept;

} // namespace noven::scanner
