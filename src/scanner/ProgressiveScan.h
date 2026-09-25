#pragma once

// Inventory 回退的方向/深度推进；锁定后不得旋转到无关方向。
// Advance Inventory fallback direction/depth; once locked, do not rotate elsewhere.

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

// anchor 和返回矩形均使用虚拟桌面屏幕坐标，可跨负坐标显示器。
// Both anchor and returned rectangle use virtual-screen coordinates,
// including negative-coordinate monitors.
[[nodiscard]] capture::Rect CalculateDirectionalRoi(
    capture::Point anchor,
    ScanDirection direction,
    capture::Size size,
    capture::Rect virtual_screen,
    long anchor_gap = 8
) noexcept;

} // namespace noven::scanner
