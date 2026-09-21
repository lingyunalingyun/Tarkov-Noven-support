#pragma once

#include "data/GameMode.h"
#include "data/ItemCatalog.h"
#include "data/ItemEconomyTypes.h"

#include <cstdint>
#include <optional>
#include <string>

namespace noven::overlay {

struct ScanDisplayResult final {
    std::string itemId;
    std::string displayName;
    data::GameMode mode{data::GameMode::Pvp};
    std::optional<std::int64_t> fleaPrice;
    std::optional<data::TraderSellValue> bestTrader;
    std::optional<double> valuePerSlot;
    data::FleaStatus fleaStatus{data::FleaStatus::Unknown};
    int width{};
    int height{};
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
