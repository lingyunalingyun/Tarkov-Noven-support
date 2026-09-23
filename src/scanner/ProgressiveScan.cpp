#include "scanner/ProgressiveScan.h"

#include <algorithm>
#include <array>

namespace noven::scanner {

namespace {

long ClampPositive(long value, long maximum) noexcept {
    return std::max(1L, std::min(value, std::max(1L, maximum)));
}

} // namespace

bool ShouldInitializeDirectionalRoi(
    int tooltip_expansion_attempt,
    int adaptive_expansion_count
) noexcept {
    return tooltip_expansion_attempt == 0 && adaptive_expansion_count == 0;
}

std::optional<ProgressiveScanPosition> NextInventoryScanPosition(
    ProgressiveScanPosition current,
    std::size_t direction_count,
    bool direction_locked
) noexcept {
    if (direction_count == 0) {
        return std::nullopt;
    }
    ++current.search_depth;
    if (current.search_depth >= kInventoryDirectionalSearchDepthCount) {
        current.search_depth = 0;
        if (direction_locked) {
            ++current.level;
        } else {
            ++current.direction_index;
            if (current.direction_index >= direction_count) {
                current.direction_index = 0;
                ++current.level;
            }
        }
    }
    if (current.level >= kInventoryScanLevelCount) {
        return std::nullopt;
    }
    return current;
}

capture::Size DirectionalScanSizeForDepth(
    capture::Size maximum_size,
    int search_depth
) noexcept {
    const capture::Size clamped_maximum = {
        std::max(1L, maximum_size.width),
        std::max(1L, maximum_size.height),
    };
    constexpr std::array<float, kInventoryDirectionalSearchDepthCount> scales{
        0.40F,
        0.70F,
        1.00F,
    };
    const float scale = scales[std::clamp(
        search_depth,
        0,
        kInventoryDirectionalSearchDepthCount - 1
    )];
    return {
        std::max(1L, std::min(
            clamped_maximum.width,
            static_cast<long>(clamped_maximum.width * scale)
        )),
        std::max(1L, std::min(
            clamped_maximum.height,
            static_cast<long>(clamped_maximum.height * scale)
        )),
    };
}

capture::Size InventoryScanSizeForLevel(
    capture::Size maximum_size,
    int level
) noexcept {
    const long maximum_width = std::max(1L, maximum_size.width);
    const long maximum_height = std::max(1L, maximum_size.height);
    switch (std::clamp(level, 0, kInventoryScanLevelCount - 1)) {
    case 0:
        return {
            ClampPositive(280, maximum_width),
            ClampPositive(140, maximum_height),
        };
    case 1:
        return {
            ClampPositive(420, maximum_width),
            ClampPositive(220, maximum_height),
        };
    case 2:
    default:
        return {
            ClampPositive(600, maximum_width),
            ClampPositive(320, maximum_height),
        };
    }
}

capture::Rect CalculateDirectionalRoi(
    capture::Point anchor,
    ScanDirection direction,
    capture::Size size,
    capture::Rect virtual_screen,
    long anchor_gap
) noexcept {
    const long width = std::max(1L, size.width);
    const long height = std::max(1L, size.height);
    const long gap = std::max(0L, anchor_gap);

    long left = anchor.x - width / 2;
    long top = anchor.y - height / 2;
    switch (direction) {
    case ScanDirection::UpperRight:
        left = anchor.x + gap;
        top = anchor.y - height + gap / 2;
        break;
    case ScanDirection::Right:
        left = anchor.x + gap;
        top = anchor.y - height / 2;
        break;
    case ScanDirection::LowerRight:
        left = anchor.x + gap;
        top = anchor.y - gap / 2;
        break;
    case ScanDirection::Down:
        left = anchor.x - width / 2;
        top = anchor.y + gap;
        break;
    case ScanDirection::LowerLeft:
        left = anchor.x - width - gap;
        top = anchor.y - gap / 2;
        break;
    case ScanDirection::Left:
        left = anchor.x - width - gap;
        top = anchor.y - height / 2;
        break;
    case ScanDirection::UpperLeft:
        left = anchor.x - width - gap;
        top = anchor.y - height + gap / 2;
        break;
    case ScanDirection::Up:
        left = anchor.x - width / 2;
        top = anchor.y - height - gap;
        break;
    }

    return capture::Rect{
        std::max(left, virtual_screen.left),
        std::max(top, virtual_screen.top),
        std::min(left + width, virtual_screen.right),
        std::min(top + height, virtual_screen.bottom),
    };
}

} // namespace noven::scanner
