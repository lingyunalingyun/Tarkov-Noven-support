#pragma once
#include "raid/RaidSession.h"
#include <map>
#include <vector>

namespace noven::raid {
enum class HistoryDate { All, Today, SevenDays, ThirtyDays };
struct HistoryFilter final {
    std::optional<GameMode> mode;
    std::optional<RaidType> type;
    std::optional<std::string> mapId;
    std::string search;
    HistoryDate date{HistoryDate::All};
    // 日界由调用方提供本地墙钟时间，与日志时间语义一致。
    // Caller supplies local wall-clock midnight, matching log timestamp semantics.
    std::int64_t localMidnight{};
    bool operator==(const HistoryFilter&) const = default;
};
struct HistorySummary final {
    std::size_t count{}, pmc{}, scav{}, unknown{}, pvp{}, pve{};
    std::optional<std::int64_t> averageDuration;
};
// 值快照只在数据/筛选改变时重建；绘制只读取索引，不复制或排序历史。
// Rebuild on snapshot/filter changes only; rendering reads indices without copying/sorting history.
class RaidHistoryBrowser final {
public:
    void SetSessions(std::vector<RaidSession> sessions);
    void SetMapNames(std::map<std::string, std::string> names);
    void SetFilter(HistoryFilter filter);
    const HistoryFilter& Filter() const noexcept { return filter_; }
    const std::vector<std::size_t>& Rows() const noexcept { return rows_; }
    const std::vector<RaidSession>& Sessions() const noexcept { return sessions_; }
    const std::vector<std::string>& Maps() const noexcept { return maps_; }
    const HistorySummary& Summary() const noexcept { return summary_; }
    std::string MapName(const RaidSession& session) const;
    const RaidSession* Find(std::string_view localId) const noexcept;
    std::size_t Builds() const noexcept { return builds_; }
private:
    void Rebuild();
    std::vector<RaidSession> sessions_;
    std::vector<std::size_t> rows_;
    std::vector<std::string> maps_;
    std::map<std::string, std::string> names_;
    HistoryFilter filter_;
    HistorySummary summary_;
    std::size_t builds_{};
};
}
