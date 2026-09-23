#include "scanner/SpatialDebugWriter.h"

#include "scanner/DebugImageWriter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace noven::scanner {

namespace {

struct Color final {
    std::uint8_t blue;
    std::uint8_t green;
    std::uint8_t red;
};

void SetPixel(capture::CapturedFrame& frame, long x, long y, Color color) {
    if (x < 0 || y < 0 || x >= static_cast<long>(frame.width)
        || y >= static_cast<long>(frame.height)) {
        return;
    }
    const std::size_t offset = static_cast<std::size_t>(y) * frame.stride
        + static_cast<std::size_t>(x) * 4;
    frame.bgra[offset + 0] = color.blue;
    frame.bgra[offset + 1] = color.green;
    frame.bgra[offset + 2] = color.red;
    frame.bgra[offset + 3] = 255;
}

void DrawRect(capture::CapturedFrame& frame, const ocr::TextBox& box, Color color) {
    const long left = static_cast<long>(std::clamp(
        box.x1, 0.0F, static_cast<float>(frame.width > 0 ? frame.width - 1 : 0)));
    const long top = static_cast<long>(std::clamp(
        box.y1, 0.0F, static_cast<float>(frame.height > 0 ? frame.height - 1 : 0)));
    const long right = static_cast<long>(std::clamp(
        box.x2 - 1.0F, 0.0F, static_cast<float>(frame.width > 0 ? frame.width - 1 : 0)));
    const long bottom = static_cast<long>(std::clamp(
        box.y2 - 1.0F, 0.0F, static_cast<float>(frame.height > 0 ? frame.height - 1 : 0)));
    if (right < left || bottom < top) {
        return;
    }
    for (int thickness = 0; thickness < 2; ++thickness) {
        for (long x = left; x <= right; ++x) {
            SetPixel(frame, x, top + thickness, color);
            SetPixel(frame, x, bottom - thickness, color);
        }
        for (long y = top; y <= bottom; ++y) {
            SetPixel(frame, left + thickness, y, color);
            SetPixel(frame, right - thickness, y, color);
        }
    }
}

void DrawCircle(capture::CapturedFrame& frame, AnchorPoint center, float radius, Color color) {
    constexpr int kSamples = 720;
    for (int sample = 0; sample < kSamples; ++sample) {
        const float angle = static_cast<float>(sample) * 6.28318530718F
            / static_cast<float>(kSamples);
        SetPixel(
            frame,
            static_cast<long>(std::lround(center.x + std::cos(angle) * radius)),
            static_cast<long>(std::lround(center.y + std::sin(angle) * radius)),
            color
        );
    }
}

void DrawAnchor(capture::CapturedFrame& frame, AnchorPoint anchor) {
    const long x = static_cast<long>(std::lround(anchor.x));
    const long y = static_cast<long>(std::lround(anchor.y));
    for (long delta = -8; delta <= 8; ++delta) {
        SetPixel(frame, x + delta, y, Color{0, 255, 255});
        SetPixel(frame, x, y + delta, Color{0, 255, 255});
    }
}

void DrawLine(
    capture::CapturedFrame& frame,
    AnchorPoint start,
    AnchorPoint end,
    Color color
) {
    long x0 = static_cast<long>(std::lround(start.x));
    long y0 = static_cast<long>(std::lround(start.y));
    const long x1 = static_cast<long>(std::lround(end.x));
    const long y1 = static_cast<long>(std::lround(end.y));
    const long delta_x = std::abs(x1 - x0);
    const long step_x = x0 < x1 ? 1 : -1;
    const long delta_y = -std::abs(y1 - y0);
    const long step_y = y0 < y1 ? 1 : -1;
    long error = delta_x + delta_y;
    while (true) {
        SetPixel(frame, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const long doubled_error = 2 * error;
        if (doubled_error >= delta_y) {
            error += delta_y;
            x0 += step_x;
        }
        if (doubled_error <= delta_x) {
            error += delta_x;
            y0 += step_y;
        }
    }
}

void DrawCenter(capture::CapturedFrame& frame, AnchorPoint center, Color color) {
    const long x = static_cast<long>(std::lround(center.x));
    const long y = static_cast<long>(std::lround(center.y));
    for (long delta = -3; delta <= 3; ++delta) {
        SetPixel(frame, x + delta, y, color);
        SetPixel(frame, x, y + delta, color);
    }
}

float DirectionCenterAngle(ScanDirection direction) noexcept {
    switch (direction) {
    case ScanDirection::UpperRight: return -45.0F;
    case ScanDirection::Right: return 0.0F;
    case ScanDirection::LowerRight: return 45.0F;
    case ScanDirection::Down: return 90.0F;
    case ScanDirection::LowerLeft: return 135.0F;
    case ScanDirection::Left: return 180.0F;
    case ScanDirection::UpperLeft: return -135.0F;
    case ScanDirection::Up: return -90.0F;
    }
    return 0.0F;
}

void DrawSectorGuides(
    capture::CapturedFrame& frame,
    AnchorPoint anchor,
    const ScannerProfile& profile,
    const ScanResult& result
) {
    const float radius = profile.max_search_radius;
    for (int boundary = 0; boundary < 8; ++boundary) {
        const float angle = (-22.5F + static_cast<float>(boundary) * 45.0F)
            * 3.14159265359F / 180.0F;
        DrawLine(
            frame,
            anchor,
            AnchorPoint{
                anchor.x + std::cos(angle) * radius,
                anchor.y + std::sin(angle) * radius,
            },
            Color{64, 64, 128}
        );
    }
    if (result.selected.has_value()) {
        const float center_angle = DirectionCenterAngle(result.selected->direction);
        const float inner_angle = (center_angle - profile.sector_half_angle_degrees)
            * 3.14159265359F / 180.0F;
        const float outer_angle = (center_angle + profile.sector_half_angle_degrees)
            * 3.14159265359F / 180.0F;
        const float selected_radius = std::min(
            profile.max_search_radius,
            static_cast<float>(result.selected->ringIndex + 1) * profile.ring_step
        );
        for (const float angle : {inner_angle, outer_angle}) {
            DrawLine(
                frame,
                anchor,
                AnchorPoint{
                    anchor.x + std::cos(angle) * selected_radius,
                    anchor.y + std::sin(angle) * selected_radius,
                },
                Color{0, 180, 255}
            );
        }
    }
}

bool SameCandidate(const ScanCandidate& left, const ScanCandidate& right) {
    return left.match.item == right.match.item
        && left.recognized.text == right.recognized.text
        && left.recognized.box.x1 == right.recognized.box.x1
        && left.recognized.box.y1 == right.recognized.box.y1
        && left.recognized.box.x2 == right.recognized.box.x2
        && left.recognized.box.y2 == right.recognized.box.y2;
}

std::array<std::array<int, 3>, 5> DigitGlyph(std::size_t digit) noexcept {
    switch (digit) {
    case 1:
        return {{{0, 1, 0}, {1, 1, 0}, {0, 1, 0}, {0, 1, 0}, {1, 1, 1}}};
    case 2:
        return {{{1, 1, 0}, {0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {1, 1, 1}}};
    case 3:
        return {{{1, 1, 0}, {0, 0, 1}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}}};
    case 4:
        return {{{1, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {0, 0, 1}}};
    case 5:
        return {{{1, 1, 1}, {1, 0, 0}, {1, 1, 0}, {0, 0, 1}, {1, 1, 0}}};
    case 6:
        return {{{0, 1, 1}, {1, 0, 0}, {1, 1, 0}, {1, 0, 1}, {0, 1, 0}}};
    case 7:
        return {{{1, 1, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}}};
    case 8:
        return {{{0, 1, 0}, {1, 0, 1}, {0, 1, 0}, {1, 0, 1}, {0, 1, 0}}};
    case 9:
        return {{{0, 1, 0}, {1, 0, 1}, {0, 1, 1}, {0, 0, 1}, {1, 1, 0}}};
    default:
        return {};
    }
}

} // namespace

bool WriteSpatialAnnotatedBmp(
    const std::filesystem::path& path,
    const capture::CapturedFrame& frame,
    AnchorPoint anchor,
    const ScannerProfile& profile,
    std::span<const ocr::RecognizedText> recognized_texts,
    const ScanResult& result,
    std::wstring& error,
    std::optional<ocr::TextBox> tooltip_region,
    std::optional<AdaptiveTextAnalysis> adaptive_analysis,
    std::span<const LockedRoiStep> locked_roi_steps,
    capture::Point frame_origin,
    std::optional<OrderedTextAssembly> text_assembly
) {
    capture::CapturedFrame annotated = frame;

    DrawRect(
        annotated,
        ocr::TextBox{
            0.0F,
            0.0F,
            static_cast<float>(frame.width),
            static_cast<float>(frame.height),
            1.0F,
        },
        Color{255, 255, 255}
    );

    for (const LockedRoiStep& step : locked_roi_steps) {
        const Color color = step.stage == LockedScanStage::HorizontalExpansion
            ? Color{255, 128, 0}
            : Color{0, 200, 0};
        DrawRect(
            annotated,
            ocr::TextBox{
                static_cast<float>(step.roi.left - frame_origin.x),
                static_cast<float>(step.roi.top - frame_origin.y),
                static_cast<float>(step.roi.right - frame_origin.x),
                static_cast<float>(step.roi.bottom - frame_origin.y),
                1.0F,
            },
            color
        );
    }

    if (tooltip_region.has_value()) {
        DrawRect(annotated, *tooltip_region, Color{255, 0, 255});
    }
    if (adaptive_analysis.has_value()
        && adaptive_analysis->localBounds.has_value()) {
        const float continuation_left = std::max(
            0.0F,
            static_cast<float>(frame.width) - adaptive_analysis->safeMargin
        );
        DrawRect(
            annotated,
            ocr::TextBox{
                continuation_left,
                1.0F,
                static_cast<float>(frame.width) - 1.0F,
                static_cast<float>(frame.height) - 1.0F,
                1.0F,
            },
            adaptive_analysis->horizontalComplete
                ? Color{0, 180, 0}
                : Color{0, 0, 255}
        );
        DrawLine(
            annotated,
            AnchorPoint{adaptive_analysis->rightmostTextX, 0.0F},
            AnchorPoint{
                adaptive_analysis->rightmostTextX,
                static_cast<float>(frame.height) - 1.0F,
            },
            Color{0, 0, 255}
        );
        if (adaptive_analysis->trustedTooltipRightBorder) {
            DrawLine(
                annotated,
                AnchorPoint{adaptive_analysis->tooltipRightBorderX, 0.0F},
                AnchorPoint{
                    adaptive_analysis->tooltipRightBorderX,
                    static_cast<float>(frame.height) - 1.0F,
                },
                Color{255, 0, 255}
            );
        }
        if (adaptive_analysis->nextRoi.has_value()) {
            const capture::Rect& next = *adaptive_analysis->nextRoi;
            DrawRect(
                annotated,
                ocr::TextBox{
                    static_cast<float>(next.left - frame_origin.x),
                    static_cast<float>(next.top - frame_origin.y),
                    static_cast<float>(next.right - frame_origin.x),
                    static_cast<float>(next.bottom - frame_origin.y),
                    1.0F,
                },
                Color{255, 0, 0}
            );
        }
        DrawRect(annotated, *adaptive_analysis->localBounds, Color{255, 255, 0});
        if (adaptive_analysis->stoppedByLargeGap) {
            const ocr::TextBox& block = *adaptive_analysis->localBounds;
            const float threshold = adaptive_analysis->stopThreshold;
            DrawRect(
                annotated,
                ocr::TextBox{
                    block.x1 - threshold,
                    block.y1 - threshold,
                    block.x2 + threshold,
                    block.y2 + threshold,
                    1.0F,
                },
                Color{0, 165, 255}
            );
        }
    }
    for (const ocr::RecognizedText& recognized : recognized_texts) {
        DrawRect(annotated, recognized.box, Color{0, 255, 255});
    }
    if (text_assembly.has_value()) {
        constexpr std::array<Color, 4> line_colors{
            Color{255, 255, 0}, Color{255, 0, 255},
            Color{0, 255, 0}, Color{0, 165, 255},
        };
        for (std::size_t index = 0; index < text_assembly->lines.size(); ++index) {
            const OrderedTextLine& line = text_assembly->lines[index];
            const Color color = line_colors[index % line_colors.size()];
            DrawRect(annotated, line.combinedBox, color);
            if (index < 9) {
                const auto glyph = DigitGlyph(index + 1);
                const long label_x = static_cast<long>(std::lround(line.combinedBox.x1)) + 2;
                const long label_y = static_cast<long>(std::lround(line.combinedBox.y1)) + 2;
                for (long row = 0; row < 5; ++row) {
                    for (long column = 0; column < 3; ++column) {
                        if (glyph[static_cast<std::size_t>(row)]
                                [static_cast<std::size_t>(column)] != 0) {
                            SetPixel(
                                annotated,
                                label_x + column,
                                label_y + row,
                                color
                            );
                        }
                    }
                }
            }
        }
    }
    for (float radius = profile.ring_step;
         radius <= profile.max_search_radius;
         radius += profile.ring_step) {
        DrawCircle(annotated, anchor, radius, Color{96, 96, 96});
    }
    DrawSectorGuides(annotated, anchor, profile, result);
    for (std::size_t index = 0; index < result.considered.size(); ++index) {
        const ScanCandidate& candidate = result.considered[index];
        const AnchorPoint center{
            (candidate.recognized.box.x1 + candidate.recognized.box.x2) / 2.0F,
            (candidate.recognized.box.y1 + candidate.recognized.box.y2) / 2.0F,
        };
        const bool selected = result.selected.has_value()
            && SameCandidate(candidate, *result.selected);
        const Color candidate_color = selected
            ? Color{255, 0, 0}
            : (candidate.participatingInSearch
                ? Color{0, 255, 0}
                : (candidate.match.ambiguous
                    ? Color{0, 165, 255}
                    : Color{96, 96, 96}));
        DrawLine(annotated, anchor, center, candidate_color);
        DrawCenter(annotated, center, candidate_color);
        DrawRect(annotated, candidate.recognized.box, candidate_color);
        if ((index + 1) <= 9) {
            const long label_x = static_cast<long>(std::lround(center.x)) + 5;
            const long label_y = static_cast<long>(std::lround(center.y)) - 7;
            const auto glyph = DigitGlyph(index + 1);
            for (long row = 0; row < 5; ++row) {
                for (long column = 0; column < 3; ++column) {
                    if (glyph[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] != 0) {
                        SetPixel(annotated, label_x + column, label_y + row, candidate_color);
                    }
                }
            }
        }
    }
    DrawAnchor(annotated, anchor);
    return WriteDebugBmp(path, annotated, error);
}

} // namespace noven::scanner
