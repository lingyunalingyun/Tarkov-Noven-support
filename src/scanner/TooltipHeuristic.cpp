#include "scanner/TooltipHeuristic.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>

namespace noven::scanner {

namespace {

float RectangleDistance(const ocr::TextBox& box, AnchorPoint anchor) noexcept {
    const float dx = anchor.x < box.x1
        ? box.x1 - anchor.x
        : (anchor.x > box.x2 ? anchor.x - box.x2 : 0.0F);
    const float dy = anchor.y < box.y1
        ? box.y1 - anchor.y
        : (anchor.y > box.y2 ? anchor.y - box.y2 : 0.0F);
    return std::hypot(dx, dy);
}

float Luminance(const std::uint8_t* pixel) noexcept {
    return 0.2126F * static_cast<float>(pixel[2])
        + 0.7152F * static_cast<float>(pixel[1])
        + 0.0722F * static_cast<float>(pixel[0]);
}

float DarkRatio(
    const capture::CapturedFrame& frame,
    const ocr::TextBox& bounds
) noexcept {
    if (frame.bgra.empty() || frame.width == 0 || frame.height == 0) {
        return 0.0F;
    }
    const long left = std::max(0L, static_cast<long>(std::floor(bounds.x1)));
    const long top = std::max(0L, static_cast<long>(std::floor(bounds.y1)));
    const long right = std::min(
        static_cast<long>(frame.width),
        static_cast<long>(std::ceil(bounds.x2))
    );
    const long bottom = std::min(
        static_cast<long>(frame.height),
        static_cast<long>(std::ceil(bounds.y2))
    );
    if (right <= left || bottom <= top) {
        return 0.0F;
    }

    std::size_t dark = 0;
    std::size_t samples = 0;
    constexpr long kSampleStep = 8;
    for (long y = top; y < bottom; y += kSampleStep) {
        for (long x = left; x < right; x += kSampleStep) {
            const std::size_t offset = static_cast<std::size_t>(y)
                * frame.stride
                + static_cast<std::size_t>(x) * 4;
            if (offset + 3 >= frame.bgra.size()) {
                continue;
            }
            ++samples;
            if (Luminance(frame.bgra.data() + offset) <= 105.0F) {
                ++dark;
            }
        }
    }
    return samples == 0 ? 0.0F : static_cast<float>(dark) / samples;
}

float PixelLuminance(
    const capture::CapturedFrame& frame,
    long x,
    long y
) noexcept {
    if (x < 0 || y < 0
        || x >= static_cast<long>(frame.width)
        || y >= static_cast<long>(frame.height)) {
        return 255.0F;
    }
    const std::size_t offset = static_cast<std::size_t>(y) * frame.stride
        + static_cast<std::size_t>(x) * 4;
    if (offset + 3 >= frame.bgra.size()) {
        return 255.0F;
    }
    return Luminance(frame.bgra.data() + offset);
}

float EdgeContrast(
    const capture::CapturedFrame& frame,
    const ocr::TextBox& bounds
) noexcept {
    const long left = std::max(0L, static_cast<long>(std::floor(bounds.x1)));
    const long top = std::max(0L, static_cast<long>(std::floor(bounds.y1)));
    const long right = std::min(
        static_cast<long>(frame.width) - 1,
        static_cast<long>(std::ceil(bounds.x2)) - 1
    );
    const long bottom = std::min(
        static_cast<long>(frame.height) - 1,
        static_cast<long>(std::ceil(bounds.y2)) - 1
    );
    if (right <= left || bottom <= top) {
        return 0.0F;
    }

    float total = 0.0F;
    std::size_t samples = 0;
    constexpr long kSampleStep = 4;
    const auto add_contrast = [&](long x, long y, long outside_x, long outside_y) {
        total += std::abs(
            PixelLuminance(frame, x, y) - PixelLuminance(frame, outside_x, outside_y)
        ) / 255.0F;
        ++samples;
    };
    for (long x = left; x <= right; x += kSampleStep) {
        add_contrast(x, top, x, top - 2);
        add_contrast(x, bottom, x, bottom + 2);
    }
    for (long y = top; y <= bottom; y += kSampleStep) {
        add_contrast(left, y, left - 2, y);
        add_contrast(right, y, right + 2, y);
    }
    return samples == 0 ? 0.0F : total / static_cast<float>(samples);
}

} // namespace

std::optional<TooltipBoxCandidate> DetectTooltipBox(
    const capture::CapturedFrame& frame,
    AnchorPoint anchor
) {
    if (frame.width == 0 || frame.height == 0 || frame.stride < frame.width * 4
        || frame.bgra.empty()) {
        return std::nullopt;
    }

    constexpr long kSampleStep = 4;
    const long grid_width = (static_cast<long>(frame.width) + kSampleStep - 1)
        / kSampleStep;
    const long grid_height = (static_cast<long>(frame.height) + kSampleStep - 1)
        / kSampleStep;
    std::vector<std::uint8_t> dark_mask(
        static_cast<std::size_t>(grid_width * grid_height),
        0
    );
    for (long grid_y = 0; grid_y < grid_height; ++grid_y) {
        for (long grid_x = 0; grid_x < grid_width; ++grid_x) {
            const long x = grid_x * kSampleStep;
            const long y = grid_y * kSampleStep;
            dark_mask[static_cast<std::size_t>(grid_y * grid_width + grid_x)] =
                PixelLuminance(frame, x, y) <= 105.0F ? 1 : 0;
        }
    }

    std::vector<std::uint8_t> visited(dark_mask.size(), 0);
    std::optional<TooltipBoxCandidate> best;
    for (long start_y = 0; start_y < grid_height; ++start_y) {
        for (long start_x = 0; start_x < grid_width; ++start_x) {
            const std::size_t start_index = static_cast<std::size_t>(
                start_y * grid_width + start_x
            );
            if (visited[start_index] == 1 || dark_mask[start_index] == 0) {
                continue;
            }
            visited[start_index] = 1;
            std::queue<std::pair<long, long>> pending;
            pending.push({start_x, start_y});
            long min_x = start_x;
            long max_x = start_x;
            long min_y = start_y;
            long max_y = start_y;
            std::size_t component_size = 0;
            while (!pending.empty()) {
                const auto [x, y] = pending.front();
                pending.pop();
                ++component_size;
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
                for (const auto& [dx, dy] : {
                    std::pair<long, long>{1, 0},
                    std::pair<long, long>{-1, 0},
                    std::pair<long, long>{0, 1},
                    std::pair<long, long>{0, -1},
                }) {
                    const long next_x = x + dx;
                    const long next_y = y + dy;
                    if (next_x < 0 || next_y < 0
                        || next_x >= grid_width || next_y >= grid_height) {
                        continue;
                    }
                    const std::size_t next_index = static_cast<std::size_t>(
                        next_y * grid_width + next_x
                    );
                    if (visited[next_index] == 0 && dark_mask[next_index] != 0) {
                        visited[next_index] = 1;
                        pending.push({next_x, next_y});
                    }
                }
            }

            const float width = static_cast<float>((max_x + 1) * kSampleStep - min_x * kSampleStep);
            const float height = static_cast<float>((max_y + 1) * kSampleStep - min_y * kSampleStep);
            if (component_size < 24 || width < 40.0F || height < 24.0F
                || width > static_cast<float>(frame.width) * 0.88F
                || height > static_cast<float>(frame.height) * 0.88F) {
                continue;
            }

            const ocr::TextBox bounds{
                static_cast<float>(min_x * kSampleStep),
                static_cast<float>(min_y * kSampleStep),
                std::min(static_cast<float>(frame.width),
                    static_cast<float>((max_x + 1) * kSampleStep)),
                std::min(static_cast<float>(frame.height),
                    static_cast<float>((max_y + 1) * kSampleStep)),
                1.0F,
            };
            const float center_x = (bounds.x1 + bounds.x2) / 2.0F;
            const float center_y = (bounds.y1 + bounds.y2) / 2.0F;
            const ScanDirection direction = ClassifyDirection(
                center_x - anchor.x,
                center_y - anchor.y
            );
            if (direction != ScanDirection::UpperRight
                && direction != ScanDirection::Right) {
                continue;
            }
            const float proximity = RectangleDistance(bounds, anchor);
            if (proximity > 48.0F) {
                continue;
            }

            const bool clipped_left = bounds.x1 <= static_cast<float>(kSampleStep);
            const bool clipped_right = bounds.x2 >= static_cast<float>(frame.width - kSampleStep);
            const bool clipped_top = bounds.y1 <= static_cast<float>(kSampleStep);
            const bool clipped_bottom = bounds.y2 >= static_cast<float>(frame.height - kSampleStep);
            const float darkness = DarkRatio(frame, bounds);
            const float contrast = EdgeContrast(frame, bounds);
            const float border_confidence = std::min(
                1.0F,
                darkness * 0.65F + contrast * 0.55F
            );
            if (darkness < 0.35F || (contrast < 0.10F && !clipped_left
                && !clipped_right && !clipped_top && !clipped_bottom)) {
                continue;
            }

            const TooltipBoxCandidate candidate{
                bounds,
                border_confidence,
                proximity,
                clipped_left,
                clipped_right,
                clipped_top,
                clipped_bottom,
            };
            if (!best.has_value()
                || candidate.borderConfidence > best->borderConfidence
                || (std::abs(candidate.borderConfidence - best->borderConfidence) < 0.001F
                    && candidate.proximityToCursor < best->proximityToCursor)) {
                best = candidate;
            }
        }
    }
    return best;
}

capture::Rect ExpandTooltipRoi(
    capture::Rect current,
    const TooltipBoxCandidate& candidate,
    capture::Size maximum_size,
    capture::Rect virtual_screen
) noexcept {
    const long maximum_width = std::max(1L, maximum_size.width);
    const long maximum_height = std::max(1L, maximum_size.height);
    long left = current.left;
    long top = current.top;
    long right = current.right;
    long bottom = current.bottom;
    if (candidate.clippedRight) {
        right = std::min(virtual_screen.right, current.left + maximum_width);
    }
    if (candidate.clippedLeft) {
        left = std::max(virtual_screen.left, current.right - maximum_width);
    }
    if (candidate.clippedTop) {
        top = std::max(virtual_screen.top, current.bottom - maximum_height);
    }
    if (candidate.clippedBottom) {
        bottom = std::min(virtual_screen.bottom, current.top + maximum_height);
    }
    return capture::Rect{left, top, right, bottom};
}

} // namespace noven::scanner
