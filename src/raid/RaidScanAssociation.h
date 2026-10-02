#pragma once
#include "data/RecentScanStore.h"
#include "raid/RaidSession.h"
#include <limits>
#include <span>

namespace noven::raid {
inline void AssociateScan(data::RecentScanEntry& scan, const std::optional<RaidSession>& active) {
    scan.localSessionId = active ? std::optional(active->localSessionId) : std::nullopt;
}
struct ScanSubtotal final {
    std::optional<std::int64_t> value;
    std::size_t known{}, unknown{};
    void Add(std::optional<std::int64_t> price) {
        if (!price) { ++unknown; return; }
        ++known;
        if (value && *price > (std::numeric_limits<std::int64_t>::max)() - *value) {
            overflow=true; value.reset();
        } else if (!overflow) value=value.value_or(0)+*price;
    }
private:
    bool overflow{};
};
struct RaidScans final {
    std::vector<data::RecentScanEntry> entries;
    ScanSubtotal flea, trader;
};
// 只允许精确身份连接；时间接近、名称相同都不能补猜历史归属。
// Join exact identities only; nearby timestamps or equal names never backfill historical links.
inline RaidScans ScansForRaid(std::string_view id, std::span<const data::RecentScanEntry> scans) {
    RaidScans result;
    if (id.empty()) return result;
    for (const auto& scan : scans) if (scan.localSessionId && *scan.localSessionId==id) {
        result.entries.push_back(scan); result.flea.Add(scan.fleaPrice); result.trader.Add(scan.bestTraderPrice);
    }
    return result;
}
}
