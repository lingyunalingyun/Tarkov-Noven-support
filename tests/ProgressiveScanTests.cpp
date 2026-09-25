#include "scanner/ProgressiveScan.h"
#include "scanner/AdaptiveTextExpansion.h"
#include "scanner/TooltipHeuristic.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    const noven::capture::Rect virtual_screen{-1920, -200, 3840, 1960};
    const noven::capture::Point anchor{0, 0};
    const auto profile = noven::scanner::InventoryProfile();
    const std::array expected_directions{
        noven::scanner::ScanDirection::UpperRight,
        noven::scanner::ScanDirection::Up,
        noven::scanner::ScanDirection::UpperLeft,
    };

    Require(
        noven::scanner::InventoryScanSizeForLevel({800, 600}, 0).width == 280
            && noven::scanner::InventoryScanSizeForLevel({800, 600}, 0).height == 140,
        "level 0 uses a small directional ROI"
    );
    Require(
        noven::scanner::InventoryScanSizeForLevel({800, 600}, 2).width == 600
            && noven::scanner::InventoryScanSizeForLevel({800, 600}, 2).height == 320,
        "level 2 remains below the legacy 800x600 first capture"
    );
    const auto primary_size = noven::scanner::InventoryPrimaryTooltipSize({800, 600});
    Require(primary_size.width == 300 && primary_size.height == 80,
        "Inventory primary path starts with a compact 300x80 tooltip ROI");
    const auto normal_placement = noven::scanner::PredictTooltipPlacement(
        {500, 500}, {0, 0, 1200, 900});
    Require(normal_placement.placement
            == noven::scanner::TooltipPlacement::DefaultRightUpper
            && normal_placement.roi.left > 500
            && normal_placement.roi.top < 500
            && normal_placement.roi.bottom < 500,
        "center-screen search is one right-upper corridor, not three probes");
    const auto edge_placement = noven::scanner::PredictTooltipPlacement(
        {1150, 500}, {0, 0, 1200, 900});
    Require(edge_placement.placement
            == noven::scanner::TooltipPlacement::RightEdgeShiftedLeft
            && edge_placement.roi.left < 1150
            && edge_placement.roi.right > 1150,
        "right monitor edge shifts search inward across cursor X");
    const auto secondary_monitor = noven::scanner::PredictTooltipPlacement(
        {-300, 500}, {-1920, 0, 0, 1080});
    Require(secondary_monitor.placement
            == noven::scanner::TooltipPlacement::RightEdgeShiftedLeft,
        "placement uses cursor monitor bounds with negative coordinates");
    const auto credible_geometry = noven::scanner::TooltipGeometryConfidence(
        {500, 500}, {525, 412, 750, 480, 1.0F},
        noven::scanner::TooltipPlacement::DefaultRightUpper);
    const auto wrong_geometry = noven::scanner::TooltipGeometryConfidence(
        {500, 500}, {220, 412, 440, 480, 1.0F},
        noven::scanner::TooltipPlacement::DefaultRightUpper);
    Require(credible_geometry > 0.55F && wrong_geometry < 0.45F,
        "right-upper panel is credible and far above-left panel is rejected");
    const noven::scanner::TooltipBoxCandidate weak_right{
        {180, 20, 290, 70, 0.9F}, 0.6F, 0.70F, 0.1F, 0.62F, 15.0F, 0.95F,
        true, false, true, true, false, true, false, false,
    };
    const noven::scanner::TooltipBoxCandidate complete_above_center{
        {20, 10, 180, 75, 0.9F}, 0.7F, 0.90F, 0.6F, 0.92F, 20.0F, 0.10F,
        true, true, true, true, false, false, false, false,
    };
    Require(!noven::scanner::IsBetterTooltipProbe(
                complete_above_center,
                noven::scanner::TooltipPlacement::DefaultRightUpper,
                weak_right,
                noven::scanner::TooltipPlacement::DefaultRightUpper
            ),
        "far wrong-side panel cannot outrank a panel in the predicted corridor");
    Require(!noven::capture::FrameSafeAfterOverlayHide(
                true, noven::capture::CaptureSource::DxgiCachedFrame)
            && noven::capture::FrameSafeAfterOverlayHide(
                true, noven::capture::CaptureSource::DxgiNewFrame),
        "previous overlay cannot be read from a cached pre-hide frame");
    const std::array title_boxes{
        noven::ocr::TextBox{10.0F, 10.0F, 50.0F, 24.0F, 0.9F},
        noven::ocr::TextBox{54.0F, 10.0F, 88.0F, 24.0F, 0.9F},
        noven::ocr::TextBox{10.0F, 27.0F, 45.0F, 40.0F, 0.9F},
        noven::ocr::TextBox{10.0F, 65.0F, 70.0F, 78.0F, 0.9F},
    };
    const auto title_band = noven::scanner::DeriveTooltipTitleBand(
        title_boxes,
        {100, 100}
    );
    Require(title_band.has_value()
            && title_band->x1 == 10.0F && title_band->y1 == 10.0F
            && title_band->x2 == 88.0F && title_band->y2 == 40.0F,
        "title band includes compact wrapped name rows but excludes separated details");
    const auto confined_title = noven::scanner::ClampTooltipTitleCrop(
        {10.0F, 10.0F, 92.0F, 30.0F, 1.0F},
        {12.0F, 8.0F, 90.0F, 45.0F, 1.0F}, 4.0F);
    Require(confined_title.has_value() && confined_title->x1 == 12.0F
            && confined_title->x2 == 90.0F,
        "title OCR crop stays inside the detected tooltip panel");

    const auto upper_right = noven::scanner::CalculateDirectionalRoi(
        anchor,
        noven::scanner::ScanDirection::UpperRight,
        {280, 140},
        virtual_screen
    );
    Require(upper_right.left >= anchor.x, "upper-right ROI starts to the right of anchor");
    Require(upper_right.bottom > anchor.y, "upper-right ROI covers the tooltip boundary");
    Require(upper_right.top < anchor.y, "upper-right ROI extends above anchor");

    const auto left_edge = noven::scanner::CalculateDirectionalRoi(
        {-1910, 0},
        noven::scanner::ScanDirection::Left,
        {280, 140},
        virtual_screen
    );
    Require(left_edge.left == virtual_screen.left, "directional ROI clips at negative monitor edge");
    Require(left_edge.right > left_edge.left, "clipped directional ROI remains usable");

    Require(
        noven::scanner::DirectionalScanSizeForDepth({280, 140}, 0).width < 280
            && noven::scanner::DirectionalScanSizeForDepth({280, 140}, 2).width == 280,
        "directional search grows from a near slice to its level maximum"
    );
    Require(noven::scanner::ShouldInitializeDirectionalRoi(0, 0),
        "the first directional capture initializes its ROI");
    Require(!noven::scanner::ShouldInitializeDirectionalRoi(0, 1),
        "an adaptively expanded ROI survives into the next capture iteration");

    auto position = noven::scanner::ProgressiveScanPosition{0, 0, 0};
    for (int expected_level = 0; expected_level < 3; ++expected_level) {
        for (std::size_t expected_direction = 0; expected_direction < 3; ++expected_direction) {
            for (int expected_depth = 0; expected_depth < 3; ++expected_depth) {
                Require(position.level == expected_level, "scan level advances only after a full cycle");
                Require(position.search_depth == expected_depth,
                    "a direction expands near-to-far before the next direction");
                Require(
                    profile.direction_priority[position.direction_index]
                        == expected_directions[expected_direction],
                    "direction order is preserved"
                );
                const auto next = noven::scanner::NextInventoryScanPosition(position, 3);
                if (expected_level == 2 && expected_direction == 2 && expected_depth == 2) {
                    Require(!next.has_value(), "all three levels terminate after UpperLeft");
                } else {
                    Require(next.has_value(), "scan sequence has a next position");
                    position = *next;
                }
            }
        }
    }

    auto locked_position = noven::scanner::ProgressiveScanPosition{0, 0, 0};
    for (int expected_level = 0; expected_level < 3; ++expected_level) {
        for (int expected_depth = 0; expected_depth < 3; ++expected_depth) {
            Require(locked_position.level == expected_level,
                "direction lock expands the current direction through each level");
            Require(locked_position.direction_index == 0,
                "direction lock never rotates away from UpperRight");
            Require(locked_position.search_depth == expected_depth,
                "direction lock preserves near-to-far expansion");
            const auto next = noven::scanner::NextInventoryScanPosition(
                locked_position,
                3,
                true
            );
            if (expected_level == 2 && expected_depth == 2) {
                Require(!next.has_value(), "a locked direction ends without rotating");
            } else {
                Require(next.has_value(), "a locked direction has another local expansion");
                locked_position = *next;
            }
        }
    }

    const auto local_anchor = noven::capture::Point{
        anchor.x - upper_right.left,
        anchor.y - upper_right.top,
    };
    Require(local_anchor.x == anchor.x - upper_right.left,
        "global anchor converts to directional ROI coordinates");
    Require(local_anchor.y == anchor.y - upper_right.top,
        "global anchor keeps its vertical ROI coordinates");

    {
        const std::array boxes{
            noven::ocr::TextBox{118.0F, 72.0F, 166.0F, 92.0F, 0.9F},
            noven::ocr::TextBox{170.0F, 72.0F, 219.0F, 92.0F, 0.9F},
        };
        const auto analysis = noven::scanner::AnalyzeTextContinuity(
            boxes,
            {100.0F, 100.0F},
            {220, 120},
            noven::scanner::ScanDirection::UpperRight
        );
        Require(analysis.localBoxIndices.size() == 2,
            "continuous same-line text stays in one local block");
        Require(analysis.shouldExpand
                && analysis.expansionSide == noven::scanner::ExpansionSide::Right,
            "continuous text clipped at the right edge expands right");
        const auto expanded = noven::scanner::ExpandAdaptiveTextRoi(
            {100, 100, 320, 220},
            analysis.expansionSide,
            analysis.localMedianHeight,
            {-1000, -1000, 1000, 1000}
        );
        Require(expanded.left == 100 && expanded.right > 320
                && expanded.top == 100 && expanded.bottom == 220,
            "adaptive expansion changes only the clipped side");

        auto both_clipped = analysis;
        both_clipped.horizontalComplete = false;
        both_clipped.verticalComplete = false;
        both_clipped.topTextMargin = 0.0F;
        const auto horizontal_decision = noven::scanner::DecideLockedScanStep(
            noven::scanner::LockedScanStage::HorizontalExpansion,
            both_clipped
        );
        Require(horizontal_decision.shouldExpand,
            "width completion requests expansion before height expansion");
        Require(horizontal_decision.expansionSide
                == noven::scanner::ExpansionSide::Right,
            "width completion expands right before height expansion");

        const auto partial_tooltip_decision =
            noven::scanner::DecideLockedScanStep(
                noven::scanner::LockedScanStage::HorizontalExpansion,
                both_clipped,
                noven::scanner::TooltipExpansionEvidence{
                    .present = true,
                }
            );
        Require(partial_tooltip_decision.shouldExpand
                && partial_tooltip_decision.expansionSide
                    == noven::scanner::ExpansionSide::Right,
            "right-edge text still expands when partial tooltip geometry exists");
        Require(partial_tooltip_decision.nextStage
                == noven::scanner::LockedScanStage::HorizontalExpansion,
            "text assembly cannot run before width completion");

        const auto partial_without_right_border =
            noven::scanner::DecideLockedScanStep(
                noven::scanner::LockedScanStage::HorizontalExpansion,
                both_clipped,
                noven::scanner::TooltipExpansionEvidence{
                    .present = true,
                    .hasLeftBorder = true,
                    .hasTopBorder = true,
                    .hasBottomBorder = true,
                }
            );
        Require(partial_without_right_border.shouldExpand
                && partial_without_right_border.expansionSide
                    == noven::scanner::ExpansionSide::Right,
            "partial tooltip without a right border does not complete width");

        both_clipped.horizontalComplete = true;
        const auto vertical_decision = noven::scanner::DecideLockedScanStep(
            noven::scanner::LockedScanStage::HorizontalExpansion,
            both_clipped
        );
        Require(vertical_decision.shouldExpand
                && vertical_decision.nextStage
                    == noven::scanner::LockedScanStage::VerticalExpansion
                && vertical_decision.expansionSide
                    == noven::scanner::ExpansionSide::Up,
            "vertical expansion starts only after width is complete");
    }

    {
        const std::array boxes{
            noven::ocr::TextBox{80.0F, 42.0F, 150.0F, 62.0F, 0.9F},
            noven::ocr::TextBox{84.0F, 18.0F, 154.0F, 38.0F, 0.9F},
            noven::ocr::TextBox{86.0F, 0.0F, 156.0F, 14.0F, 0.9F},
        };
        const auto analysis = noven::scanner::AnalyzeTextContinuity(
            boxes,
            {70.0F, 80.0F},
            {220, 120},
            noven::scanner::ScanDirection::UpperRight
        );
        Require(analysis.localBoxIndices.size() == 3,
            "compact multi-line text stays continuous");
        Require(analysis.shouldExpand
                && analysis.expansionSide == noven::scanner::ExpansionSide::Up,
            "stacked text clipped at the top expands upward");
    }

    {
        const std::array boxes{
            noven::ocr::TextBox{30.0F, 60.0F, 80.0F, 80.0F, 0.9F},
            noven::ocr::TextBox{190.0F, 60.0F, 235.0F, 80.0F, 0.99F},
        };
        const auto analysis = noven::scanner::AnalyzeTextContinuity(
            boxes,
            {20.0F, 85.0F},
            {240, 120},
            noven::scanner::ScanDirection::UpperRight
        );
        Require(analysis.localBoxIndices.size() == 1
                && analysis.localBoxIndices.front() == 0,
            "unrelated text beyond a large horizontal gap is excluded");
        Require(analysis.stoppedByLargeGap && !analysis.shouldExpand,
            "large horizontal gap stops adaptive expansion");
    }

    {
        const std::array boxes{
            noven::ocr::TextBox{0.0F, 70.0F, 42.0F, 92.0F, 0.9F},
        };
        const auto analysis = noven::scanner::AnalyzeTextContinuity(
            boxes,
            {-8.0F, 100.0F},
            {220, 120},
            noven::scanner::ScanDirection::UpperRight
        );
        Require(!analysis.shouldExpand,
            "upper-right scan does not expand back toward the mouse-facing edge");
    }

    {
        const std::array boxes{
            noven::ocr::TextBox{70.0F, 60.0F, 130.0F, 80.0F, 0.9F},
            noven::ocr::TextBox{70.0F, 170.0F, 130.0F, 190.0F, 0.9F},
        };
        const auto analysis = noven::scanner::AnalyzeTextContinuity(
            boxes,
            {60.0F, 90.0F},
            {220, 220},
            noven::scanner::ScanDirection::UpperRight
        );
        Require(analysis.localBoxIndices.size() == 1
                && analysis.stoppedByLargeGap,
            "large vertical gap stops and excludes the distant row");
    }

    {
        const std::array boxes{
            noven::ocr::TextBox{8.0F, 30.0F, 80.0F, 52.0F, 0.9F},
            noven::ocr::TextBox{130.0F, 30.0F, 205.0F, 52.0F, 0.9F},
        };
        const noven::ocr::TextBox tooltip{0.0F, 20.0F, 100.0F, 70.0F, 1.0F};
        const auto analysis = noven::scanner::AnalyzeTextContinuity(
            boxes,
            {0.0F, 60.0F},
            {220, 120},
            noven::scanner::ScanDirection::UpperRight,
            tooltip
        );
        Require(analysis.boundedByTooltip && !analysis.shouldExpand,
            "tooltip bounds stop unnecessary adaptive expansion");
        Require(analysis.localBoxIndices.size() == 1
                && analysis.localBoxIndices.front() == 0,
            "text outside the tooltip does not enter its local block");
        const auto tooltip_decision = noven::scanner::DecideLockedScanStep(
            noven::scanner::LockedScanStage::HorizontalExpansion,
            analysis,
            noven::scanner::TooltipExpansionEvidence{
                .present = true,
                .complete = true,
                .hasLeftBorder = true,
                .hasRightBorder = true,
                .hasTopBorder = true,
                .hasBottomBorder = true,
            }
        );
        Require(!tooltip_decision.shouldExpand
                && tooltip_decision.nextStage
                    == noven::scanner::LockedScanStage::TextAssembly,
            "a complete tooltip skips horizontal and vertical expansion");
    }

    {
        const std::array detector_only_boxes{
            noven::ocr::TextBox{150.0F, 40.0F, 219.5F, 62.0F, 0.35F},
        };
        const auto detector_only_analysis = noven::scanner::AnalyzeTextContinuity(
            detector_only_boxes,
            {100.0F, 80.0F},
            {220, 120},
            noven::scanner::ScanDirection::UpperRight
        );
        Require(detector_only_analysis.needsExpandRight
                && !detector_only_analysis.horizontalComplete,
            "a detector box touching the right edge forces expansion even without recognition");
        const auto detector_only_decision = noven::scanner::DecideLockedScanStep(
            noven::scanner::LockedScanStage::HorizontalExpansion,
            detector_only_analysis
        );
        Require(detector_only_decision.shouldExpand
                && detector_only_decision.nextStage
                    == noven::scanner::LockedScanStage::HorizontalExpansion,
            "catalog work cannot begin on a detector-clipped text pass");
    }

    {
        noven::scanner::AdaptiveTextAnalysis width_complete;
        width_complete.horizontalComplete = true;
        width_complete.verticalComplete = true;
        width_complete.rightTextMargin = 60.0F;
        width_complete.safeMargin = 25.0F;
        const auto decision = noven::scanner::DecideLockedScanStep(
            noven::scanner::LockedScanStage::HorizontalExpansion,
            width_complete
        );
        Require(!decision.shouldExpand
                && decision.nextStage
                    == noven::scanner::LockedScanStage::TextAssembly,
            "safe empty margin after the rightmost text completes width");

        width_complete.noContinuationAfterExpansion = true;
        const auto no_continuation = noven::scanner::DecideLockedScanStep(
            noven::scanner::LockedScanStage::HorizontalExpansion,
            width_complete
        );
        Require(no_continuation.nextStage
                == noven::scanner::LockedScanStage::TextAssembly
                && !no_continuation.shouldExpand,
            "an expanded pass with no continuation reaches final assembly");
    }

    Require(
        noven::scanner::RectangleDistanceToPoint(
            {20.0F, 20.0F, 200.0F, 100.0F, 1.0F},
            {10.0F, 90.0F}
        )
            < noven::scanner::RectangleDistanceToPoint(
                {40.0F, 60.0F, 60.0F, 80.0F, 1.0F},
                {10.0F, 90.0F}
            ),
        "rectangle-edge distance, not center distance, defines proximity"
    );

    noven::capture::CapturedFrame tooltip_frame;
    tooltip_frame.width = 300;
    tooltip_frame.height = 200;
    tooltip_frame.stride = tooltip_frame.width * 4;
    tooltip_frame.bgra.assign(
        static_cast<std::size_t>(tooltip_frame.stride) * tooltip_frame.height,
        220
    );
    for (long y = 30; y < 120; ++y) {
        for (long x = 100; x < 260; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y)
                * tooltip_frame.stride + static_cast<std::size_t>(x) * 4;
            tooltip_frame.bgra[offset] = 40;
            tooltip_frame.bgra[offset + 1] = 40;
            tooltip_frame.bgra[offset + 2] = 40;
            tooltip_frame.bgra[offset + 3] = 255;
        }
    }
    const auto tooltip = noven::scanner::DetectTooltipBox(
        tooltip_frame,
        noven::scanner::AnchorPoint{90.0F, 100.0F}
    );
    Require(tooltip.has_value(), "dark near-upper-right rectangle is detected as a tooltip candidate");
    Require(tooltip->FullBox(), "fully visible tooltip is not marked as clipped");
    Require(tooltip->hasLeftBorder && tooltip->hasRightBorder
            && tooltip->hasTopBorder && tooltip->hasBottomBorder,
        "a complete tooltip requires observed evidence for all four borders");
    Require(
        tooltip->rect.x1 <= 104.0F && tooltip->rect.x2 >= 256.0F
            && tooltip->rect.y1 <= 36.0F && tooltip->rect.y2 >= 116.0F,
        "detected tooltip bounds contain the dark rectangle"
    );
    Require(tooltip->panelConfidence >= 0.55F,
        "dark low-saturation panel with visible edges is confident");
    const auto complete_decision = noven::scanner::DecideTooltipPrimaryPath(
        tooltip,
        {0, 0, 300, 200},
        {0, 0, 1000, 1000},
        0
    );
    Require(complete_decision.action
            == noven::scanner::TooltipPrimaryAction::PreciseCrop,
        "confident complete tooltip uses the primary precise-crop path");

    noven::capture::CapturedFrame clipped_right_frame;
    clipped_right_frame.width = 180;
    clipped_right_frame.height = 140;
    clipped_right_frame.stride = clipped_right_frame.width * 4;
    clipped_right_frame.bgra.assign(
        static_cast<std::size_t>(clipped_right_frame.stride)
            * clipped_right_frame.height,
        220
    );
    for (long y = 30; y < 120; ++y) {
        for (long x = 100; x < 180; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y)
                * clipped_right_frame.stride + static_cast<std::size_t>(x) * 4;
            clipped_right_frame.bgra[offset] = 40;
            clipped_right_frame.bgra[offset + 1] = 40;
            clipped_right_frame.bgra[offset + 2] = 40;
            clipped_right_frame.bgra[offset + 3] = 255;
        }
    }
    const auto clipped_right = noven::scanner::DetectTooltipBox(
        clipped_right_frame,
        noven::scanner::AnchorPoint{90.0F, 100.0F}
    );
    Require(clipped_right.has_value() && clipped_right->clippedRight,
        "tooltip touching the right edge is reported as clipped");
    Require(!clipped_right->hasRightBorder && !clipped_right->FullBox(),
        "the ROI boundary is not mistaken for a tooltip right border");
    const auto expanded_right = noven::scanner::ExpandTooltipRoi(
        noven::capture::Rect{90, 0, 180, 140},
        *clipped_right,
        {280, 140},
        noven::capture::Rect{0, 0, 1000, 1000}
    );
    Require(expanded_right.right > 180,
        "a right-clipped tooltip expands to the right before fallback");
    const auto partial_decision = noven::scanner::DecideTooltipPrimaryPath(
        clipped_right,
        {90, 0, 270, 140},
        {0, 0, 1000, 1000},
        0
    );
    Require(partial_decision.action
            == noven::scanner::TooltipPrimaryAction::RecoverPanel,
        "partial tooltip recovers its missing edge before adaptive fallback");
    Require(partial_decision.nextRoi.left == 90
            && partial_decision.nextRoi.top == 0
            && partial_decision.nextRoi.bottom == 140
            && partial_decision.nextRoi.right > 270,
        "right-clipped tooltip expands only its missing right edge");

    const auto no_panel_decision = noven::scanner::DecideTooltipPrimaryPath(
        std::nullopt,
        {90, 0, 390, 80},
        {0, 0, 1000, 1000},
        0
    );
    Require(no_panel_decision.action
            == noven::scanner::TooltipPrimaryAction::AdaptiveFallback,
        "missing tooltip panel invokes AdaptiveTextExpansion fallback");

    noven::capture::CapturedFrame clipped_top_frame;
    clipped_top_frame.width = 220;
    clipped_top_frame.height = 140;
    clipped_top_frame.stride = clipped_top_frame.width * 4;
    clipped_top_frame.bgra.assign(
        static_cast<std::size_t>(clipped_top_frame.stride)
            * clipped_top_frame.height,
        220
    );
    for (long y = 0; y < 84; ++y) {
        for (long x = 100; x < 180; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y)
                * clipped_top_frame.stride + static_cast<std::size_t>(x) * 4;
            clipped_top_frame.bgra[offset] = 40;
            clipped_top_frame.bgra[offset + 1] = 40;
            clipped_top_frame.bgra[offset + 2] = 40;
            clipped_top_frame.bgra[offset + 3] = 255;
        }
    }
    const auto clipped_top = noven::scanner::DetectTooltipBox(
        clipped_top_frame,
        noven::scanner::AnchorPoint{90.0F, 100.0F}
    );
    Require(clipped_top.has_value() && clipped_top->clippedTop,
        "tooltip touching the top edge is reported as clipped");
    const auto expanded_top = noven::scanner::ExpandTooltipRoi(
        noven::capture::Rect{90, -40, 220, 100},
        *clipped_top,
        {280, 200},
        noven::capture::Rect{-1000, -1000, 1000, 1000}
    );
    Require(expanded_top.top < -40,
        "a top-clipped tooltip expands upward before fallback");

    noven::capture::CapturedFrame text_background_frame;
    text_background_frame.width = 200;
    text_background_frame.height = 80;
    text_background_frame.stride = text_background_frame.width * 4;
    text_background_frame.bgra.assign(
        static_cast<std::size_t>(text_background_frame.stride)
            * text_background_frame.height,
        220
    );
    for (long y = 5; y < 75; ++y) {
        for (long x = 5; x < 105; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y)
                * text_background_frame.stride + static_cast<std::size_t>(x) * 4;
            text_background_frame.bgra[offset] = 35;
            text_background_frame.bgra[offset + 1] = 35;
            text_background_frame.bgra[offset + 2] = 35;
            text_background_frame.bgra[offset + 3] = 255;
        }
    }
    const std::array tooltip_texts{
        noven::ocr::RecognizedText{
            {20.0F, 20.0F, 90.0F, 42.0F, 0.9F}, "BlackRock", 0.9F,
        },
        noven::ocr::RecognizedText{
            {125.0F, 20.0F, 190.0F, 42.0F, 0.9F}, "Unrelated", 0.9F,
        },
        noven::ocr::RecognizedText{
            {20.0F, 48.0F, 80.0F, 66.0F, 0.9F}, "232/400", 0.9F,
        },
    };
    const auto filtered_texts = noven::scanner::FilterTooltipNameFragments(
        text_background_frame,
        tooltip_texts
    );
    Require(filtered_texts.size() == 1
            && filtered_texts.front().text == "BlackRock",
        "tooltip filtering ignores outside text and numeric status noise");

    std::cout << "Progressive scan tests passed\n";
    return 0;
}
