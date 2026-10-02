#include "raid/RaidHistoryBrowser.h"
#include <windows.h>
#include <algorithm>
#include <set>

namespace noven::raid {
namespace {
std::wstring Fold(std::string_view text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring wide(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), n);
    CharLowerBuffW(wide.data(), n);
    return wide;
}
}
void RaidHistoryBrowser::SetSessions(std::vector<RaidSession> sessions) {
    sessions_ = std::move(sessions);
    std::set<std::string> maps;
    for (const auto& s : sessions_) maps.insert(s.mapId);
    maps_.assign(maps.begin(), maps.end());
    Rebuild();
}
void RaidHistoryBrowser::SetMapNames(std::map<std::string, std::string> names) {
    if (names_ == names) return;
    names_ = std::move(names); Rebuild();
}
void RaidHistoryBrowser::SetFilter(HistoryFilter filter) {
    if (filter_ == filter) return;
    filter_ = std::move(filter); Rebuild();
}
std::string RaidHistoryBrowser::MapName(const RaidSession& s) const {
    const auto it = names_.find(s.mapId);
    return it == names_.end() ? s.mapId : it->second;
}
const RaidSession* RaidHistoryBrowser::Find(std::string_view id) const noexcept {
    const auto it = std::find_if(sessions_.begin(), sessions_.end(), [&](const auto& s) { return s.localSessionId == id; });
    return it == sessions_.end() ? nullptr : &*it;
}
void RaidHistoryBrowser::Rebuild() {
    ++builds_; rows_.clear(); summary_ = {};
    const auto query = Fold(filter_.search);
    const int days = filter_.date == HistoryDate::Today ? 1 : filter_.date == HistoryDate::SevenDays ? 7 : 30;
    constexpr std::int64_t day = 86400000;
    long double durationSum{}; std::size_t durationCount{};
    for (std::size_t i = 0; i < sessions_.size(); ++i) {
        const auto& s = sessions_[i];
        if (!s.endObserved || !s.startObserved) continue;
        if (filter_.mode && s.gameMode != *filter_.mode) continue;
        if (filter_.type && s.raidType != *filter_.type) continue;
        if (filter_.mapId && s.mapId != *filter_.mapId) continue;
        if (filter_.date != HistoryDate::All && (!s.startedAt
            || *s.startedAt < filter_.localMidnight - (days - 1) * day
            || *s.startedAt >= filter_.localMidnight + day)) continue;
        if (!query.empty() && Fold(MapName(s) + " " + s.mapId + " " + s.eftRaidId + " " + s.localSessionId).find(query) == std::wstring::npos) continue;
        rows_.push_back(i); ++summary_.count;
        if (s.raidType == RaidType::PMC) ++summary_.pmc;
        else if (s.raidType == RaidType::Scav) ++summary_.scav;
        else ++summary_.unknown;
        if (s.gameMode == GameMode::PvP) ++summary_.pvp;
        if (s.gameMode == GameMode::PvE) ++summary_.pve;
        if (s.duration) { durationSum += *s.duration; ++durationCount; }
    }
    std::stable_sort(rows_.begin(), rows_.end(), [&](auto a, auto b) {
        return sessions_[a].startedAt.value_or(INT64_MIN) > sessions_[b].startedAt.value_or(INT64_MIN);
    });
    if (durationCount) summary_.averageDuration = static_cast<std::int64_t>(durationSum / durationCount);
}
}
