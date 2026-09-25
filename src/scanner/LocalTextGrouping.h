#pragma once

// 只对当前局部裁剪区内相邻文字框分组，不跨 ROI 拼接无关 UI 文本。
// Group neighboring boxes within the current local crop only; never join unrelated UI across ROIs.

#include "data/ItemCatalog.h"
#include "ocr/OcrTypes.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace noven::scanner {

struct LocalTextGroupingProfile final {
    std::size_t maximum_boxes{4};
    std::size_t maximum_neighbors{8};
    float maximum_gap_multiplier{2.5F};
    float maximum_line_offset_multiplier{0.75F};
    float maximum_height_ratio{2.0F};
    float maximum_group_width{720.0F};
    float maximum_group_height{220.0F};
};

struct TextNeighborDiagnostic final {
    std::size_t boxIndex{};
    float horizontalGap{};
    float verticalGap{};
    bool sameLine{};
    bool stacked{};
};

struct LocalTextGroup final {
    std::vector<std::size_t> boxIndices;
    ocr::TextBox combinedBox;
    std::string combinedText;
    float groupingConfidence{};
    float ocrConfidence{};
};

struct TextGroupMatch final {
    std::vector<std::size_t> boxIndices;
    ocr::TextBox combinedBox;
    std::string combinedText;
    data::ItemMatch match;
    float groupingConfidence{};
    float ocrConfidence{};
};

struct OrderedTextLine final {
    std::vector<std::size_t> boxIndices;
    ocr::TextBox combinedBox;
    std::string text;
    float ocrConfidence{};
};

struct OrderedTextAssembly final {
    // 行内从左到右、行间从上到下；原始框索引仍可用于诊断。
    // Left-to-right within lines, top-to-bottom across lines; retain original box indices.
    std::vector<OrderedTextLine> lines;
    std::vector<std::size_t> orderedBoxIndices;
    ocr::TextBox combinedBox;
    std::string completeText;
    float ocrConfidence{};
};

[[nodiscard]] LocalTextGroupingProfile DefaultLocalTextGroupingProfile() noexcept;

[[nodiscard]] bool GroupedMatchClearlyBetter(
    float grouped_score,
    float single_score
) noexcept;

[[nodiscard]] std::vector<TextNeighborDiagnostic> FindNearestTextNeighbors(
    std::span<const ocr::RecognizedText> texts,
    std::size_t box_index,
    const LocalTextGroupingProfile& profile,
    std::size_t maximum_neighbors
);

[[nodiscard]] std::vector<LocalTextGroup> BuildLocalTextGroups(
    std::span<const ocr::RecognizedText> texts,
    const LocalTextGroupingProfile& profile = DefaultLocalTextGroupingProfile()
);

[[nodiscard]] std::vector<TextGroupMatch> MatchLocalTextGroups(
    const data::ItemCatalog& catalog,
    std::span<const ocr::RecognizedText> texts,
    float acceptance_threshold,
    const LocalTextGroupingProfile& profile = DefaultLocalTextGroupingProfile()
);

[[nodiscard]] OrderedTextAssembly AssembleLocalTextInReadingOrder(
    std::span<const ocr::RecognizedText> texts
);

// 覆盖率与字符串相似度分开：短词即使完全匹配也解释不了完整标题。
// Coverage is separate from string similarity: a perfect short token
// cannot explain a complete title.
[[nodiscard]] float CatalogEvidenceCoverage(
    const std::string& complete_local_text,
    const std::string& catalog_alias
);

} // namespace noven::scanner
