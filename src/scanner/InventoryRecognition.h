#pragma once

#include "data/ItemCatalog.h"
#include "data/ItemEconomyStore.h"
#include "ocr/OcrTypes.h"
#include "overlay/OverlayTypes.h"
#include "scanner/LocalTextGrouping.h"
#include "scanner/SpatialCandidateSelector.h"
#include "scanner/TooltipHeuristic.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace noven::scanner {

enum class InventoryOutcomeStatus {
    OcrEmpty,
    CatalogNoMatch,
    CatalogAmbiguous,
    Success,
    EconomyMissing,
};

enum class InventoryMatchMode {
    Strict,
    BestEffort,
    OcrOnly,
};

struct BestEffortCandidate final {
    data::ItemMatch match;
    std::string evidenceText;
    float score{};
    float similarity{};
    float coverage{};
    float tokenEvidence{};
    float ocrConfidence{};
    float numericEvidence{};
    float caliberEvidence{};
    float modelEvidence{};
    float categoryEvidence{};
    float contradictionPenalty{};
    std::string ocrCaliber;
    std::string candidateCaliber;
    std::string ocrCategory;
    std::string candidateCategory;
    bool caliberConflict{};
    bool categoryConflict{};
    bool modelConflict{};
};

struct InventoryCatalogCandidate final {
    MatchedText text;
    std::string stage;
    float tokenEvidence{};
    bool accepted{};
    std::string rejectionReason;
};

struct InventoryRecognitionResult final {
    std::uint64_t scanId{};
    InventoryRecognitionPath sourcePath{InventoryRecognitionPath::PrimaryTooltip};
    std::optional<ocr::TextBox> tooltipRect;
    std::vector<ocr::RecognizedText> rawFragments;
    OrderedTextAssembly orderedFragments;
    std::string assembledText;
    std::string normalizedText;
    float ocrConfidence{};
    float tooltipGeometryConfidence{};
    float cropConfidence{};
    bool catalogAttempted{};
    std::vector<InventoryCatalogCandidate> candidates;
    std::optional<std::size_t> selectedCandidate;
    std::string selectedItemId;
    std::string selectedAlias;
    float similarity{};
    float evidenceCoverage{};
    float ambiguityGap{};
    std::string rejectionReason;
    InventoryMatchMode matchMode{InventoryMatchMode::Strict};
    std::vector<BestEffortCandidate> bestEffortTop;
    bool bestEffortAmbiguous{};
    InventoryOutcomeStatus status{InventoryOutcomeStatus::OcrEmpty};
};

[[nodiscard]] InventoryRecognitionResult BuildInventoryRecognition(
    std::uint64_t scan_id,
    InventoryRecognitionPath source_path,
    std::optional<ocr::TextBox> tooltip_rect,
    std::vector<ocr::RecognizedText> fragments
);

void ResolveInventoryCatalog(
    InventoryRecognitionResult& result,
    const data::ItemCatalog& catalog,
    float threshold
);

void BestEffortResolve(
    InventoryRecognitionResult& result,
    const data::ItemCatalog& catalog
);

struct InventoryEconomyResolution final {
    bool found{};
    std::string itemId;
    const data::ItemEconomyInfo* info{};
};

[[nodiscard]] InventoryEconomyResolution ResolveInventoryEconomy(
    const InventoryRecognitionResult& recognition,
    const data::ItemEconomyStore& store,
    data::GameMode mode
);

[[nodiscard]] std::optional<overlay::ScanDisplayResult> BuildInventoryDisplayResult(
    const InventoryRecognitionResult& recognition,
    const InventoryEconomyResolution& economy,
    data::GameMode mode
);

} // namespace noven::scanner
