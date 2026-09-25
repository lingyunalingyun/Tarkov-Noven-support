#pragma once

// 候选矩形相对锚点分类；输入 OCR 框是 ROI 局部坐标。
// Classify candidate rectangles relative to the anchor; input OCR boxes are ROI-local.

#include "data/ItemCatalog.h"
#include "ocr/OcrTypes.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace noven::scanner {

enum class ScanProfileType {
    Inventory,
    RaidPickup,
};

enum class ScanDirection {
    UpperRight,
    Right,
    LowerRight,
    Down,
    LowerLeft,
    Left,
    UpperLeft,
    Up,
};

struct AnchorPoint final {
    // 当前 ROI 内的锚点坐标；不能直接与虚拟桌面框比较。
    // Anchor coordinates within the current ROI, not directly comparable to screen rectangles.
    float x{};
    float y{};
};

struct ScannerProfile final {
    ScanProfileType type{};
    std::vector<ScanDirection> direction_priority;
    float ring_step{};
    float max_search_radius{};
    float minimum_ocr_confidence{};
    float catalog_threshold{};
    float match_quality_weight{};
    float ocr_confidence_weight{};
    float direction_prior_weight{};
    float distance_weight{};
    float row_alignment_weight{};
    float row_alignment_tolerance{};
    bool use_composite_ranking{};
    float sector_half_angle_degrees{22.5F};
};

struct MatchedText final {
    ocr::RecognizedText recognized;
    data::ItemMatch match;
    std::vector<std::size_t> sourceBoxIndices;
    float groupingConfidence{1.0F};
    bool grouped{};
    float evidenceCoverage{1.0F};
};

enum class SpatialSearchAction {
    Continue,
    Select,
};

struct SpatialSearchStep final {
    int ring{};
    ScanDirection direction{};
    std::size_t boxCount{};
    std::size_t validCandidateCount{};
    std::size_t ambiguousCandidateCount{};
    std::size_t acceptedCandidateCount{};
    SpatialSearchAction action{SpatialSearchAction::Continue};
};

struct ScanCandidate final {
    // distanceToAnchor 是点到矩形边界的最短距离；中心距离只作诊断。
    // distanceToAnchor is point-to-rectangle distance; center distance is diagnostic.
    ocr::RecognizedText recognized;
    data::ItemMatch match;
    ScanDirection direction{};
    float distanceToAnchor{};
    float centerDistanceToAnchor{};
    float directionalScore{};
    float spatialScore{};
    float horizontalDistance{};
    float verticalDistance{};
    bool overlapsAnchorRow{};
    bool overlapsAnchorColumn{};
    float boxWidth{};
    float boxHeight{};
    float centerAngleDegrees{};
    float rowAlignmentScore{};
    float distanceScore{};
    float rankingScore{};
    int ringIndex{};
    int directionRank{};
    std::vector<std::size_t> sourceBoxIndices;
    float groupingConfidence{1.0F};
    bool grouped{};
    bool participatingInSearch{};
    bool insideTooltip{};
    float evidenceCoverage{1.0F};
};

struct ScanResult final {
    // 保留参与和未参与的候选以便调试；Inventory 主路径不做全 ROI 竞争。
    // Retain candidates for diagnostics; the Inventory primary path does not
    // run a whole-ROI candidate competition.
    bool found{};
    ScanProfileType profile{};
    std::optional<ScanCandidate> selected;
    std::vector<ScanCandidate> considered;
    std::optional<ScanCandidate> nearestValid;
    std::optional<ocr::TextBox> tooltipRegion;
    std::vector<SpatialSearchStep> searchSteps;
};

enum class ScanClassification {
    CaptureFailed,
    Success,
    NoText,
    OcrFailed,
    NoCatalogMatch,
    CatalogAmbiguous,
    BestEffortMatch,
    OcrOnly,
    NoSpatialCandidate,
    EconomyMissing,
    DisplayFailed,
    OverlayFailed,
};

[[nodiscard]] const wchar_t* ScanClassificationName(
    ScanClassification classification
) noexcept;

[[nodiscard]] ScannerProfile InventoryProfile() noexcept;
[[nodiscard]] ScannerProfile RaidPickupProfile() noexcept;
[[nodiscard]] ScannerProfile ProfileFor(ScanProfileType type) noexcept;
[[nodiscard]] ScanDirection ClassifyDirection(float delta_x, float delta_y) noexcept;
[[nodiscard]] const wchar_t* ScanProfileName(ScanProfileType type) noexcept;
[[nodiscard]] const wchar_t* ScanDirectionName(ScanDirection direction) noexcept;
[[nodiscard]] const wchar_t* SpatialSearchActionName(
    SpatialSearchAction action
) noexcept;

class SpatialCandidateSelector final {
public:
    [[nodiscard]] ScanResult Select(
        ScanProfileType profile,
        AnchorPoint anchor,
        std::span<const MatchedText> matched_texts,
        std::optional<ocr::TextBox> tooltip_region = std::nullopt
    ) const;
};

} // namespace noven::scanner
