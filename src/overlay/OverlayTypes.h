#pragma once

// 显示模型只接收稳定 ID、规范名称和经济数据；原始 OCR 仅作诊断。
// The display model uses stable ID, canonical name, and economy data; raw OCR is diagnostic only.

#include "data/GameMode.h"
#include "data/ItemCatalog.h"
#include "data/ItemEconomyTypes.h"

#include <cstdint>
#include <optional>
#include <string>

namespace noven::overlay {

enum class MatchQuality {
    // 低置信度与 OCR_ONLY 是不同状态；前者仍有规范物品身份。
    // Low confidence still has canonical item identity, unlike OCR_ONLY.
    Strict,
    LowConfidence,
    OcrOnly,
};

struct ScanDisplayResult final {
    std::string itemId;
    // 物品匹配成功时，此标题来自目录规范名称而非 OCR。
    // Once an item resolves, this title comes from the catalog, not OCR.
    std::string displayName;
    data::GameMode mode{data::GameMode::Pvp};
    std::optional<std::int64_t> fleaPrice;
    std::optional<data::TraderSellValue> bestTrader;
    std::optional<double> valuePerSlot;
    data::FleaStatus fleaStatus{data::FleaStatus::Unknown};
    int width{};
    int height{};
    MatchQuality matchQuality{MatchQuality::Strict};
    std::string rawOcrText;
    std::string assembledOcrText;
    bool bestEffortAmbiguous{};
};

struct OverlayPlacement final {
    long left{};
    long top{};
    long width{};
    long height{};
};

[[nodiscard]] std::string DisplayNameForItem(const data::ItemRecord& item);
[[nodiscard]] std::wstring FormatRoubles(std::int64_t value);
[[nodiscard]] std::wstring FormatOptionalRoubles(
    const std::optional<std::int64_t>& value
);
[[nodiscard]] const wchar_t* FleaStatusDisplayName(data::FleaStatus status) noexcept;
[[nodiscard]] OverlayPlacement CalculateOverlayPlacement(
    long anchor_x,
    long anchor_y,
    long card_width,
    long card_height,
    long work_left,
    long work_top,
    long work_right,
    long work_bottom
) noexcept;

} // namespace noven::overlay
