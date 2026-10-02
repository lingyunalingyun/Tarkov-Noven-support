#pragma once

#include "data/GameMode.h"
#include "data/ItemCatalog.h"
#include "data/ItemEconomyStore.h"

#include <cstddef>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <span>

namespace noven::data {

enum class PriceSortMode {
    FleaPrice,
    FleaChange,
    TraderPrice,
};

enum class PriceTraderSide {
    Sell,
    Buy,
};

// 价格浏览只组合目录身份与内存经济快照，不参与扫描或网络刷新。
// The browser combines catalog identity with in-memory economy snapshots; it never scans or refreshes data.
struct PriceRow final {
    const ItemRecord* item{};
    std::optional<ItemEconomyInfo> economy;
};

// UI 提供本地化标签别名；数据层只匹配上游类型，不依赖语言服务。
// UI supplies localized tag aliases; the data layer matches upstream types without a localization dependency.
struct PriceTagAlias { std::string label; std::string type; };

class PriceBrowserModel final {
public:
    PriceBrowserModel(const ItemCatalog& catalog, const ItemEconomyStore& economy) noexcept
        : catalog_(catalog), economy_(economy) {}

    [[nodiscard]] std::vector<PriceRow> Query(
        std::string_view query, GameMode mode,
        PriceSortMode sortMode = PriceSortMode::FleaPrice,
        bool descending = true,
        PriceTraderSide traderSide = PriceTraderSide::Sell,
        std::size_t maximum = 120,
        std::span<const PriceTagAlias> tagAliases = {},
        std::size_t offset = 0, std::size_t* total = nullptr) const;
    [[nodiscard]] std::chrono::system_clock::time_point LastUpdated(GameMode mode) const noexcept {
        return economy_.GetLastUpdated(mode);
    }

private:
    const ItemCatalog& catalog_;
    const ItemEconomyStore& economy_;
};

} // namespace noven::data
