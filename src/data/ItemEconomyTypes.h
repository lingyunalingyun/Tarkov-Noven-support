#pragma once

// 经济数据以稳定物品 ID 为键；缺失价格使用 optional/Unknown，而非虚构的零。
// Economy data is keyed by stable item ID; absent prices use optional/Unknown, not a fabricated zero.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace noven::data {

enum class FleaStatus {
    // 未知与禁止上架不同；未来 UI 不能将两者合并。
    // Unknown differs from banned; the UI must not collapse them.
    Allowed,
    Banned,
    LockedOrUnavailable,
    Unknown,
};

struct TraderSellValue final {
    std::string traderId;
    std::string traderName;
    std::int64_t priceRoubles{};
};

struct ItemEconomyInfo final {
    std::string itemId;
    int width{};
    int height{};
    std::optional<std::int64_t> fleaPrice;
    std::optional<TraderSellValue> bestTrader;
    FleaStatus fleaStatus{FleaStatus::Unknown};
    std::optional<std::int64_t> bestValue;
    std::optional<double> valuePerSlot;
    std::chrono::system_clock::time_point updatedAt{};
};

[[nodiscard]] const wchar_t* FleaStatusName(FleaStatus status) noexcept;

} // namespace noven::data
