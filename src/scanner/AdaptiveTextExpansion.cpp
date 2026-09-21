#include "scanner/AdaptiveTextExpansion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace noven::scanner {

namespace {

float Width(const ocr::TextBox& box) noexcept {
    return std::max(0.0F, box.x2 - box.x1);
}

float Height(const ocr::TextBox& box) noexcept {
    return std::max(0.0F, box.y2 - box.y1);
}

float HorizontalGap(const ocr::TextBox& left, const ocr::TextBox& right) noexcept {
    return std::max(0.0F, std::max(left.x1, right.x1) - std::min(left.x2, right.x2));
}

float VerticalGap(const ocr::TextBox& top, const ocr::TextBox& bottom) noexcept {
    return std::max(0.0F, std::max(top.y1, bottom.y1) - std::min(top.y2, bottom.y2));
}

float RectangleGap(const ocr::TextBox& left, const ocr::TextBox& right) noexcept {
    return std::hypot(HorizontalGap(left, right), VerticalGap(left, right));
}

float Median(std::vector<float> values) {
    if (values.empty()) {
        return 0.0F;
    }
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    if (values.size() % 2 != 0) {
        return *middle;
    }
    const float upper = *middle;
    const float lower = *std::max_element(values.begin(), middle);
    return (lower + upper) / 2.0F;
}

bool CenterInside(const ocr::TextBox& box, const ocr::TextBox& bounds) noexcept {
    const float center_x = (box.x1 + box.x2) / 2.0F;
    const float center_y = (box.y1 + box.y2) / 2.0F;
    return center_x >= bounds.x1 && center_x <= bounds.x2
        && center_y >= bounds.y1 && center_y <= bounds.y2;
}

bool CanExpandToward(ScanDirection direction, ExpansionSide side) noexcept {
    switch (direction) {
    case ScanDirection::UpperRight:
        return side == ExpansionSide::Right || side == ExpansionSide::Up;
    case ScanDirection::Up:
        return side == ExpansionSide::Left || side == ExpansionSide::Right
            || side == ExpansionSide::Up;
    case ScanDirection::UpperLeft:
        return side == ExpansionSide::Left || side == ExpansionSide::Up;
    case ScanDirection::Right:
        return side == ExpansionSide::Right || side == ExpansionSide::Up
            || side == ExpansionSide::Down;
    case ScanDirection::LowerRight:
        return side == ExpansionSide::Right || side == ExpansionSide::Down;
    case ScanDirection::Down:
        return side == ExpansionSide::Left || side == ExpansionSide::Right
            || side == ExpansionSide::Down;
    case ScanDirection::LowerLeft:
        return side == ExpansionSide::Left || side == ExpansionSide::Down;
    case ScanDirection::Left:
        return side == ExpansionSide::Left || side == ExpansionSide::Up
            || side == ExpansionSide::Down;
    }
    return false;
}

bool Continuous(
    const ocr::TextBox& left,
    const ocr::TextBox& right,
    const AdaptiveTextExpansionProfile& profile
) noexcept {
    const float left_height = Height(left);
    const float right_height = Height(right);
    if (left_height <= 0.0F || right_height <= 0.0F) {
        return false;
    }
    if (std::max(left_height, right_height) / std::min(left_height, right_height)
        > profile.maximumHeightRatio) {
        return false;
    }
    const float text_height = std::max(left_height, right_height);
    const float gap_limit = std::max(
        profile.fixedMinimumGap,
        text_height * profile.textHeightGapMultiplier
    );
    const float left_center_x = (left.x1 + left.x2) / 2.0F;
    const float left_center_y = (left.y1 + left.y2) / 2.0F;
    const float right_center_x = (right.x1 + right.x2) / 2.0F;
    const float right_center_y = (right.y1 + right.y2) / 2.0F;
    const bool same_line = std::abs(left_center_y - right_center_y)
        <= std::max(8.0F, text_height * profile.alignmentToleranceMultiplier)
        && HorizontalGap(left, right) <= gap_limit;
    const float horizontal_overlap = std::max(
        0.0F,
        std::min(left.x2, right.x2) - std::max(left.x1, right.x1)
    );
    const bool compact_stack = VerticalGap(left, right) <= gap_limit
        && (horizontal_overlap > 0.0F
            || std::abs(left_center_x - right_center_x) <= gap_limit);
    return same_line || compact_stack;
}

ocr::TextBox Bounds(
    std::span<const ocr::TextBox> boxes,
    std::span<const std::size_t> indices
) noexcept {
    ocr::TextBox bounds{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        1.0F,
    };
    for (const std::size_t index : indices) {
        bounds.x1 = std::min(bounds.x1, boxes[index].x1);
        bounds.y1 = std::min(bounds.y1, boxes[index].y1);
        bounds.x2 = std::max(bounds.x2, boxes[index].x2);
        bounds.y2 = std::max(bounds.y2, boxes[index].y2);
        bounds.confidence = std::min(bounds.confidence, boxes[index].confidence);
    }
    return bounds;
}

} // namespace

AdaptiveTextExpansionProfile DefaultAdaptiveTextExpansionProfile() noexcept {
    return AdaptiveTextExpansionProfile{};
}

float RectangleDistanceToPoint(const ocr::TextBox& box, AnchorPoint point) noexcept {
    const float dx = std::max({box.x1 - point.x, 0.0F, point.x - box.x2});
    const float dy = std::max({box.y1 - point.y, 0.0F, point.y - box.y2});
    return std::hypot(dx, dy);
}

AdaptiveTextAnalysis AnalyzeTextContinuity(
    std::span<const ocr::TextBox> boxes,
    AnchorPoint anchor,
    capture::Size roi_size,
    ScanDirection direction,
    std::optional<ocr::TextBox> tooltip_bounds,
    const AdaptiveTextExpansionProfile& profile
) {
    AdaptiveTextAnalysis result;
    result.boundedByTooltip = tooltip_bounds.has_value();
    if (boxes.empty()) {
        return result;
    }

    std::vector<std::size_t> eligible;
    eligible.reserve(boxes.size());
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        if (!tooltip_bounds.has_value() || CenterInside(boxes[index], *tooltip_bounds)) {
            eligible.push_back(index);
        }
    }
    if (eligible.empty()) {
        return result;
    }

    const std::size_t seed = *std::min_element(
        eligible.begin(),
        eligible.end(),
        [&](std::size_t left, std::size_t right) {
            return RectangleDistanceToPoint(boxes[left], anchor)
                < RectangleDistanceToPoint(boxes[right], anchor);
        }
    );
    std::vector<bool> included(boxes.size(), false);
    included[seed] = true;
    result.localBoxIndices.push_back(seed);
    std::vector<float> local_gaps;
    for (std::size_t cursor = 0; cursor < result.localBoxIndices.size(); ++cursor) {
        const std::size_t current = result.localBoxIndices[cursor];
        for (const std::size_t candidate : eligible) {
            if (included[candidate]
                || !Continuous(boxes[current], boxes[candidate], profile)) {
                continue;
            }
            included[candidate] = true;
            result.localBoxIndices.push_back(candidate);
            local_gaps.push_back(RectangleGap(boxes[current], boxes[candidate]));
        }
    }

    result.localBounds = Bounds(boxes, result.localBoxIndices);
    std::vector<float> heights;
    heights.reserve(result.localBoxIndices.size());
    for (const std::size_t index : result.localBoxIndices) {
        heights.push_back(Height(boxes[index]));
    }
    result.localMedianHeight = Median(std::move(heights));
    result.medianLocalGap = Median(std::move(local_gaps));
    result.stopThreshold = std::max({
        profile.fixedMinimumGap,
        result.localMedianHeight * profile.textHeightGapMultiplier,
        result.medianLocalGap * profile.medianGapMultiplier,
    });

    result.nearestOutsideGap = std::numeric_limits<float>::infinity();
    for (const std::size_t candidate : eligible) {
        if (included[candidate]) {
            continue;
        }
        for (const std::size_t local : result.localBoxIndices) {
            result.nearestOutsideGap = std::min(
                result.nearestOutsideGap,
                RectangleGap(boxes[local], boxes[candidate])
            );
        }
    }
    result.stoppedByLargeGap = std::isfinite(result.nearestOutsideGap)
        && result.nearestOutsideGap > result.stopThreshold;

    if (tooltip_bounds.has_value()) {
        return result;
    }

    const ocr::TextBox& bounds = *result.localBounds;
    const float boundary_margin = std::max(
        profile.fixedMinimumGap,
        result.localMedianHeight * profile.boundaryMarginMultiplier
    );
    struct Edge final { ExpansionSide side; float distance; };
    const std::array edges{
        Edge{ExpansionSide::Left, bounds.x1},
        Edge{ExpansionSide::Right, static_cast<float>(roi_size.width) - bounds.x2},
        Edge{ExpansionSide::Up, bounds.y1},
        Edge{ExpansionSide::Down, static_cast<float>(roi_size.height) - bounds.y2},
    };
    const auto nearest_edge = std::min_element(
        edges.begin(),
        edges.end(),
        [&](const Edge& left, const Edge& right) {
            const float left_distance = CanExpandToward(direction, left.side)
                ? left.distance : std::numeric_limits<float>::infinity();
            const float right_distance = CanExpandToward(direction, right.side)
                ? right.distance : std::numeric_limits<float>::infinity();
            return left_distance < right_distance;
        }
    );
    if (nearest_edge != edges.end()
        && CanExpandToward(direction, nearest_edge->side)
        && nearest_edge->distance <= boundary_margin) {
        result.shouldExpand = true;
        result.expansionSide = nearest_edge->side;
    }
    return result;
}

capture::Rect ExpandAdaptiveTextRoi(
    capture::Rect current_roi,
    ExpansionSide side,
    float local_text_height,
    capture::Rect virtual_screen,
    const AdaptiveTextExpansionProfile& profile
) noexcept {
    if (current_roi.Empty() || side == ExpansionSide::None) {
        return current_roi;
    }
    const long expansion = std::max(
        profile.minimumExpansionPixels,
        static_cast<long>(std::ceil(
            std::max(1.0F, local_text_height) * profile.expansionHeightMultiplier
        ))
    );
    capture::Rect result = current_roi;
    switch (side) {
    case ExpansionSide::Left:
        result.left = std::max(
            {virtual_screen.left, current_roi.left - expansion,
             current_roi.right - profile.maximumWidth}
        );
        break;
    case ExpansionSide::Right:
        result.right = std::min(
            {virtual_screen.right, current_roi.right + expansion,
             current_roi.left + profile.maximumWidth}
        );
        break;
    case ExpansionSide::Up:
        result.top = std::max(
            {virtual_screen.top, current_roi.top - expansion,
             current_roi.bottom - profile.maximumHeight}
        );
        break;
    case ExpansionSide::Down:
        result.bottom = std::min(
            {virtual_screen.bottom, current_roi.bottom + expansion,
             current_roi.top + profile.maximumHeight}
        );
        break;
    case ExpansionSide::None:
        break;
    }
    if (static_cast<std::size_t>(result.Width())
            * static_cast<std::size_t>(result.Height())
        > profile.maximumRoiPixels) {
        return current_roi;
    }
    return result;
}

const wchar_t* ExpansionSideName(ExpansionSide side) noexcept {
    switch (side) {
    case ExpansionSide::Left: return L"left";
    case ExpansionSide::Right: return L"right";
    case ExpansionSide::Up: return L"up";
    case ExpansionSide::Down: return L"down";
    case ExpansionSide::None: return L"none";
    }
    return L"none";
}

} // namespace noven::scanner
