#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace noven::data {

enum class FleaStatus {
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
