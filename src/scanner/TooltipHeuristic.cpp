#include "scanner/TooltipHeuristic.h"

#include <algorithm>
#include <cctype>
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

float Saturation(const std::uint8_t* pixel) noexcept {
    const float blue = static_cast<float>(pixel[0]) / 255.0F;
    const float green = static_cast<float>(pixel[1]) / 255.0F;
    const float red = static_cast<float>(pixel[2]) / 255.0F;
    const float maximum = std::max({red, green, blue});
    const float minimum = std::min({red, green, blue});
    return maximum <= 0.001F ? 0.0F : (maximum - minimum) / maximum;
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

float TooltipBackgroundRatio(
    const capture::CapturedFrame& frame,
    const ocr::TextBox& bounds,
    long sample_step
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
    std::size_t matching = 0;
    std::size_t samples = 0;
    for (long y = top; y < bottom; y += std::max(1L, sample_step)) {
        for (long x = left; x < right; x += std::max(1L, sample_step)) {
            const std::size_t offset = static_cast<std::size_t>(y) * frame.stride
                + static_cast<std::size_t>(x) * 4;
            if (offset + 3 >= frame.bgra.size()) {
                continue;
            }
            const std::uint8_t* pixel = frame.bgra.data() + offset;
            ++samples;
            if (Luminance(pixel) <= 125.0F && Saturation(pixel) <= 0.40F) {
                ++matching;
            }
        }
    }
    return samples == 0 ? 0.0F
        : static_cast<float>(matching) / static_cast<float>(samples);
}

float TooltipTextContentConfidence(
    const capture::CapturedFrame& frame,
    const ocr::TextBox& bounds
) noexcept {
    const long left = std::max(0L, static_cast<long>(std::floor(bounds.x1)) + 6);
    const long top = std::max(0L, static_cast<long>(std::floor(bounds.y1)) + 6);
    const long right = std::min(
        static_cast<long>(frame.width),
        static_cast<long>(std::ceil(bounds.x2)) - 6
    );
    const long bottom = std::min(
        static_cast<long>(frame.height),
        static_cast<long>(std::ceil(bounds.y2)) - 6
    );
    if (right <= left || bottom <= top) {
        return 0.0F;
    }

    std::size_t bright = 0;
    std::size_t samples = 0;
    constexpr long kSampleStep = 2;
    for (long y = top; y < bottom; y += kSampleStep) {
        for (long x = left; x < right; x += kSampleStep) {
            const std::size_t offset = static_cast<std::size_t>(y) * frame.stride
                + static_cast<std::size_t>(x) * 4;
            if (offset + 3 >= frame.bgra.size()) {
                continue;
            }
            ++samples;
            if (Luminance(frame.bgra.data() + offset) >= 150.0F) {
                ++bright;
            }
        }
    }
    if (samples == 0) {
        return 0.0F;
    }
    return std::min(1.0F, static_cast<float>(bright) / samples * 8.0F);
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

enum class BorderSide {
    Left,
    Right,
    Top,
    Bottom,
};

bool HasBorderEvidence(
    const capture::CapturedFrame& frame,
    const ocr::TextBox& bounds,
    BorderSide side
) noexcept {
    constexpr long kBoundaryGuard = 4;
    constexpr long kSampleStep = 4;
    constexpr float kMinimumContrast = 18.0F;
    constexpr float kMinimumEvidenceRatio = 0.35F;
    const long left = static_cast<long>(std::floor(bounds.x1));
    const long top = static_cast<long>(std::floor(bounds.y1));
    const long right = static_cast<long>(std::ceil(bounds.x2)) - 1;
    const long bottom = static_cast<long>(std::ceil(bounds.y2)) - 1;
    const bool side_touches_frame =
        (side == BorderSide::Left && left < kBoundaryGuard)
        || (side == BorderSide::Right
            && right >= static_cast<long>(frame.width) - kBoundaryGuard)
        || (side == BorderSide::Top && top < kBoundaryGuard)
        || (side == BorderSide::Bottom
            && bottom >= static_cast<long>(frame.height) - kBoundaryGuard);
    if (right <= left || bottom <= top || side_touches_frame) {
        return false;
    }

    std::size_t evidence = 0;
    std::size_t samples = 0;
    const auto sample = [&](long inside_x, long inside_y,
                            long outside_x, long outside_y) {
        const float inside = PixelLuminance(frame, inside_x, inside_y);
        const float outside = PixelLuminance(frame, outside_x, outside_y);
        ++samples;
        if (inside <= 140.0F && outside - inside >= kMinimumContrast) {
            ++evidence;
        }
    };
    if (side == BorderSide::Left || side == BorderSide::Right) {
        const long inside_x = side == BorderSide::Left ? left + 2 : right - 2;
        const long outside_x = side == BorderSide::Left
            ? left - kBoundaryGuard
            : right + kBoundaryGuard;
        for (long y = top + 4; y <= bottom - 4; y += kSampleStep) {
            sample(inside_x, y, outside_x, y);
        }
    } else {
        const long inside_y = side == BorderSide::Top ? top + 2 : bottom - 2;
        const long outside_y = side == BorderSide::Top
            ? top - kBoundaryGuard
            : bottom + kBoundaryGuard;
        for (long x = left + 4; x <= right - 4; x += kSampleStep) {
            sample(x, inside_y, x, outside_y);
        }
    }
    return samples > 0
        && static_cast<float>(evidence) / static_cast<float>(samples)
            >= kMinimumEvidenceRatio;
}

} // namespace

TooltipPrimaryProfile DefaultTooltipPrimaryProfile() noexcept {
    return TooltipPrimaryProfile{};
}

capture::Size InventoryPrimaryTooltipSize(
    capture::Size maximum_size,
    const TooltipPrimaryProfile& profile
) noexcept {
    return capture::Size{
        std::max(1L, std::min(profile.initialSize.width, maximum_size.width)),
        std::max(1L, std::min(profile.initialSize.height, maximum_size.height)),
    };
}

bool IsBetterTooltipProbe(
    const TooltipBoxCandidate& candidate,
    TooltipPlacement placement,
    const TooltipBoxCandidate& current,
    TooltipPlacement current_placement
) noexcept {
    if (std::abs(candidate.geometryConfidence - current.geometryConfidence) > 0.15F) {
        return candidate.geometryConfidence > current.geometryConfidence;
    }
    if (candidate.FullBox() != current.FullBox()) {
        return candidate.FullBox();
    }
    const auto border_count = [](const TooltipBoxCandidate& box) {
        return static_cast<int>(box.hasLeftBorder)
            + static_cast<int>(box.hasRightBorder)
            + static_cast<int>(box.hasTopBorder)
            + static_cast<int>(box.hasBottomBorder);
    };
    if (border_count(candidate) != border_count(current)) {
        return border_count(candidate) > border_count(current);
    }
    if (candidate.textConfidence != current.textConfidence) {
        return candidate.textConfidence > current.textConfidence;
    }
    if (candidate.backgroundConfidence != current.backgroundConfidence) {
        return candidate.backgroundConfidence > current.backgroundConfidence;
    }
    if (std::abs(candidate.proximityToCursor - current.proximityToCursor) > 0.01F) {
        return candidate.proximityToCursor < current.proximityToCursor;
    }
    return static_cast<int>(placement) < static_cast<int>(current_placement);
}

std::optional<TooltipBoxCandidate> DetectTooltipBox(
    const capture::CapturedFrame& frame,
    AnchorPoint anchor,
    TooltipPlacement placement
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
                || (width > static_cast<float>(frame.width) * 0.95F
                    && height > static_cast<float>(frame.height) * 0.95F)) {
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
            const float proximity = RectangleDistance(bounds, anchor);
            if (proximity > 220.0F) {
                continue;
            }

            const bool clipped_left = bounds.x1 <= static_cast<float>(kSampleStep);
            const bool clipped_right = bounds.x2 >= static_cast<float>(frame.width - kSampleStep);
            const bool clipped_top = bounds.y1 <= static_cast<float>(kSampleStep);
            const bool clipped_bottom = bounds.y2 >= static_cast<float>(frame.height - kSampleStep);
            const bool has_left_border = !clipped_left
                && HasBorderEvidence(frame, bounds, BorderSide::Left);
            const bool has_right_border = !clipped_right
                && HasBorderEvidence(frame, bounds, BorderSide::Right);
            const bool has_top_border = !clipped_top
                && HasBorderEvidence(frame, bounds, BorderSide::Top);
            const bool has_bottom_border = !clipped_bottom
                && HasBorderEvidence(frame, bounds, BorderSide::Bottom);
            const float darkness = DarkRatio(frame, bounds);
            const float background = TooltipBackgroundRatio(frame, bounds, 6);
            const float text_confidence = TooltipTextContentConfidence(frame, bounds);
            const float contrast = EdgeContrast(frame, bounds);
            const float border_confidence = std::min(
                1.0F,
                darkness * 0.65F + contrast * 0.55F
            );
            const float edge_score = std::min(1.0F, contrast / 0.25F);
            const float border_score = static_cast<float>(has_left_border)
                + static_cast<float>(has_right_border)
                + static_cast<float>(has_top_border)
                + static_cast<float>(has_bottom_border);
            const float panel_confidence = background * 0.50F
                + edge_score * 0.20F + border_score * 0.20F
                + text_confidence * 0.10F;
            if (darkness < 0.35F || background < 0.35F
                || (contrast < 0.10F && !clipped_left
                && !clipped_right && !clipped_top && !clipped_bottom)) {
                continue;
            }

            const TooltipBoxCandidate candidate{
                bounds,
                border_confidence,
                background,
                text_confidence,
                panel_confidence,
                proximity,
                TooltipGeometryConfidence(
                    capture::Point{static_cast<long>(std::lround(anchor.x)),
                        static_cast<long>(std::lround(anchor.y))},
                    bounds, placement),
                has_left_border,
                has_right_border,
                has_top_border,
                has_bottom_border,
                clipped_left,
                clipped_right,
                clipped_top,
                clipped_bottom,
            };
            if (!best.has_value()
                || IsBetterTooltipProbe(
                    candidate,
                    placement,
                    *best,
                    placement
                )) {
                best = candidate;
            }
        }
    }
    return best;
}

TooltipPrimaryDecision DecideTooltipPrimaryPath(
    const std::optional<TooltipBoxCandidate>& candidate,
    capture::Rect current_roi,
    capture::Rect virtual_screen,
    int recovery_attempt,
    const TooltipPrimaryProfile& profile
) noexcept {
    if (!candidate.has_value()
        || candidate->panelConfidence < profile.minimumPanelConfidence) {
        return TooltipPrimaryDecision{
            TooltipPrimaryAction::AdaptiveFallback,
            current_roi,
            candidate.has_value() ? L"low_panel_confidence" : L"no_panel",
        };
    }
    if (candidate->FullBox()) {
        return TooltipPrimaryDecision{
            TooltipPrimaryAction::PreciseCrop,
            current_roi,
            L"complete_panel",
        };
    }
    if (recovery_attempt >= profile.maximumRecoveryAttempts) {
        return TooltipPrimaryDecision{
            TooltipPrimaryAction::AdaptiveFallback,
            current_roi,
            L"panel_recovery_limit",
        };
    }
    const capture::Size recovery_size{
        std::min(
            profile.maximumRecoverySize.width,
            current_roi.Width() + profile.recoveryWidthStep
        ),
        std::min(
            profile.maximumRecoverySize.height,
            current_roi.Height() + profile.recoveryHeightStep
        ),
    };
    const capture::Rect expanded = ExpandTooltipRoi(
        current_roi,
        *candidate,
        recovery_size,
        virtual_screen
    );
    const bool changed = expanded.left != current_roi.left
        || expanded.top != current_roi.top
        || expanded.right != current_roi.right
        || expanded.bottom != current_roi.bottom;
    return TooltipPrimaryDecision{
        changed ? TooltipPrimaryAction::RecoverPanel
                : TooltipPrimaryAction::AdaptiveFallback,
        expanded,
        changed ? L"recover_missing_panel_edge" : L"panel_recovery_stalled",
    };
}

float TooltipTextBackgroundConfidence(
    const capture::CapturedFrame& frame,
    const ocr::TextBox& box
) noexcept {
    constexpr float kPadding = 4.0F;
    return TooltipBackgroundRatio(
        frame,
        ocr::TextBox{
            box.x1 - kPadding,
            box.y1 - kPadding,
            box.x2 + kPadding,
            box.y2 + kPadding,
            box.confidence,
        },
        3
    );
}

bool IsLikelyTooltipNameFragment(std::string_view text) {
    bool has_ascii_letter = false;
    bool has_non_ascii = false;
    bool has_price_marker = false;
    for (const unsigned char character : text) {
        has_ascii_letter = has_ascii_letter || std::isalpha(character) != 0;
        has_non_ascii = has_non_ascii || character >= 0x80;
        has_price_marker = has_price_marker || character == '$';
    }
    if (text.find("\xE2\x82\xBD") != std::string_view::npos
        || text.find("\xE2\x82\xAC") != std::string_view::npos) {
        has_price_marker = true;
    }
    if (has_price_marker) {
        return false;
    }
    if (has_ascii_letter || has_non_ascii) {
        return true;
    }
    return false;
}

std::vector<ocr::RecognizedText> FilterTooltipNameFragments(
    const capture::CapturedFrame& frame,
    std::span<const ocr::RecognizedText> texts,
    float minimum_background_confidence
) {
    std::vector<ocr::RecognizedText> result;
    result.reserve(texts.size());
    for (const ocr::RecognizedText& text : texts) {
        if (IsLikelyTooltipNameFragment(text.text)
            && TooltipTextBackgroundConfidence(frame, text.box)
                >= minimum_background_confidence) {
            result.push_back(text);
        }
    }
    return result;
}

const wchar_t* TooltipPrimaryActionName(TooltipPrimaryAction action) noexcept {
    switch (action) {
    case TooltipPrimaryAction::PreciseCrop: return L"precise_crop";
    case TooltipPrimaryAction::RecoverPanel: return L"recover_panel";
    case TooltipPrimaryAction::AdaptiveFallback: return L"adaptive_fallback";
    }
    return L"adaptive_fallback";
}

std::optional<ocr::TextBox> DeriveTooltipTitleBand(
    std::span<const ocr::TextBox> boxes,
    capture::Size frame_size
) {
    if (boxes.empty() || frame_size.width <= 0 || frame_size.height <= 0) {
        return std::nullopt;
    }
    std::size_t first = boxes.size();
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        if (boxes[index].x2 <= boxes[index].x1 || boxes[index].y2 <= boxes[index].y1) {
            continue;
        }
        if (first == boxes.size() || boxes[index].y1 < boxes[first].y1) {
            first = index;
        }
    }
    if (first == boxes.size()) {
        return std::nullopt;
    }

    const ocr::TextBox& top_box = boxes[first];
    const float top_height = top_box.y2 - top_box.y1;
    const float top_center = (top_box.y1 + top_box.y2) / 2.0F;
    float left = top_box.x1;
    float top = top_box.y1;
    float right = top_box.x2;
    float bottom = top_box.y2;
    for (std::size_t index = 0; index < boxes.size(); ++index) {
        if (index == first) {
            continue;
        }
        const ocr::TextBox& box = boxes[index];
        if (box.x2 <= box.x1 || box.y2 <= box.y1) {
            continue;
        }
        const float height = box.y2 - box.y1;
        const float center = (box.y1 + box.y2) / 2.0F;
        const bool same_title_line = std::abs(center - top_center)
            <= std::max(8.0F, std::max(height, top_height) * 0.70F);
        const float gap = box.y1 - bottom;
        const bool wrapped_title_line = gap >= -2.0F
            && gap <= std::max(8.0F, top_height * 1.25F)
            && std::abs(box.x1 - left) <= std::max(20.0F, top_height * 1.5F);
        if (!same_title_line && !wrapped_title_line) {
            continue;
        }
        left = std::min(left, box.x1);
        top = std::min(top, box.y1);
        right = std::max(right, box.x2);
        bottom = std::max(bottom, box.y2);
    }
    return ocr::TextBox{
        std::clamp(left, 0.0F, static_cast<float>(frame_size.width)),
        std::clamp(top, 0.0F, static_cast<float>(frame_size.height)),
        std::clamp(right, 0.0F, static_cast<float>(frame_size.width)),
        std::clamp(bottom, 0.0F, static_cast<float>(frame_size.height)),
        1.0F,
    };
}

std::optional<ocr::TextBox> ClampTooltipTitleCrop(
    const ocr::TextBox& title,
    const ocr::TextBox& panel,
    float padding
) noexcept {
    const ocr::TextBox crop{
        std::max(panel.x1, title.x1 - padding),
        std::max(panel.y1, title.y1 - padding),
        std::min(panel.x2, title.x2 + padding),
        std::min(panel.y2, title.y2 + padding),
        title.confidence,
    };
    if (crop.x2 <= crop.x1 || crop.y2 <= crop.y1) return std::nullopt;
    return crop;
}

const wchar_t* InventoryRecognitionPathName(
    InventoryRecognitionPath path
) noexcept {
    return path == InventoryRecognitionPath::PrimaryTooltip
        ? L"PRIMARY_TOOLTIP" : L"ADAPTIVE_FALLBACK";
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
