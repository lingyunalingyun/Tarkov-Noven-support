#pragma once

// 将独立 OCR 片段组装为查询，解析稳定物品 ID，并构建显示数据。
// Assemble independent OCR fragments into a query, resolve a stable item ID,
// and build display data.

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

// 记录 OCR 到目录、经济数据的结果；经济数据缺失不抹去物品身份。
// Records OCR/catalog/economy outcomes; missing economy data does not erase item identity.
enum class InventoryOutcomeStatus {
    OcrEmpty,
    CatalogNoMatch,
    CatalogAmbiguous,
    Success,
    EconomyMissing,
};

// 严格阈值区分置信度，不决定是否把原始 OCR 当作物品名。
// Strict thresholds classify confidence, not whether raw OCR becomes an item name.
enum class InventoryMatchMode {
    Strict,
    BestEffort,
    OcrOnly,
};

// 各评分信号独立保留，便于解释低置信度排序和冲突惩罚。
// Keep scoring signals separate to explain low-confidence ranking and contradictions.
struct BestEffortCandidate final {
    data::ItemMatch match;
    std::string evidenceText;
    float score{};
    // 字符串相似度与标题覆盖率分开：短词完美命中不能压过完整名称。
    // Keep string similarity separate from title coverage so short exact tokens cannot outrank full names.
    float similarity{};
    float coverage{};
    // 词元、普通数字、完整口径及型号分别计分；共用 x39 不等于口径一致。
    // Score tokens, generic numbers, full calibers, and model codes separately;
    // sharing x39 does not establish caliber agreement.
    float tokenEvidence{};
    float ocrConfidence{}; // 原识别置信度。 / Original OCR confidence.
    float numericEvidence{};
    float caliberEvidence{};
    float modelEvidence{};
    // 粗类别兼容加分；口径、型号或类别冲突另行扣分。
    // Coarse category compatibility adds evidence; caliber/model/category conflicts incur penalties.
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

// 同一扫描的 OCR、目录候选与拒绝原因随此对象传递，避免中途静默丢失。
// Carries OCR, catalog candidates, and rejection reasons through one scan.
struct InventoryRecognitionResult final {
    std::uint64_t scanId{};
    InventoryRecognitionPath sourcePath{InventoryRecognitionPath::PrimaryTooltip};
    std::optional<ocr::TextBox> tooltipRect;
    // 保留原框及阅读顺序，只组装局部标题而非整个 ROI。
    // Preserve original boxes and reading order; assemble a local title, not the whole ROI.
    std::vector<ocr::RecognizedText> rawFragments;
    OrderedTextAssembly orderedFragments;
    std::string assembledText;
    std::string normalizedText; // 仅用于匹配。 / Used only for matching.
    float ocrConfidence{};
    // 面板方位与标题裁剪的可信度独立于 OCR 置信度。
    // Panel placement and title-crop confidence are separate from OCR confidence.
    float tooltipGeometryConfidence{};
    float cropConfidence{};
    bool catalogAttempted{};
    std::vector<InventoryCatalogCandidate> candidates;
    std::optional<std::size_t> selectedCandidate;
    std::string selectedItemId;
    std::string selectedAlias;
    float similarity{};
    float evidenceCoverage{};
    float ambiguityGap{}; // 前两名分差，标记歧义。 / Top-two gap, indicating ambiguity.
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

// 严格匹配失败后，对健康目录的可用查询仍选最高排名候选；OCR_ONLY 只表示目录不可用或搜索技术失败。
// After strict rejection, still choose the top catalog candidate for a usable query;
// OCR_ONLY denotes unavailable catalog data or technical search failure.
void BestEffortResolve(
    InventoryRecognitionResult& result,
    const data::ItemCatalog& catalog
);

struct InventoryEconomyResolution final {
    bool found{};
    std::string itemId;
    const data::ItemEconomyInfo* info{};
};

// 后续经济查询只使用已选稳定 ID，绝不重新按 OCR 文本查找。
// Downstream economy lookup uses only the selected stable ID, never OCR text.
[[nodiscard]] InventoryEconomyResolution ResolveInventoryEconomy(
    const InventoryRecognitionResult& recognition,
    const data::ItemEconomyStore& store,
    data::GameMode mode
);

// 已解析物品的标题来自目录规范名称；即使经济数据缺失也可构建卡片。
// Resolved cards use the catalog name and can be built without economy data.
[[nodiscard]] std::optional<overlay::ScanDisplayResult> BuildInventoryDisplayResult(
    const InventoryRecognitionResult& recognition,
    const InventoryEconomyResolution& economy,
    data::GameMode mode
);

} // namespace noven::scanner
