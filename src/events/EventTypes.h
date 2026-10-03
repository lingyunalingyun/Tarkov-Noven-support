#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace noven::events {
using Timestamp = std::int64_t; // UTC 秒；缺失时间使用 optional。 / UTC seconds; missing times are optional.
enum class EventStatus { Unknown, Upcoming, Active, Ended };
enum class EventMode { PvP, PvE, Seasonal };
enum class SourceKind { OfficialTelegram, TarkovDev, TarkovChanges };
enum class EvidenceType { Announcement, Update, EntityReference, ConfigurationChange };
struct EventEvidence {
    std::string evidenceId, sourceRecordId, sourceUrl;
    SourceKind sourceKind{SourceKind::OfficialTelegram};
    EvidenceType type{EvidenceType::Announcement};
    std::optional<Timestamp> publishedAt;
    std::vector<std::string> taskIds, itemIds, mapIds, bossIds;
    std::vector<std::string> linkedChangeRecordIds;
    std::string changedKey, oldValue, newValue;
    bool operator==(const EventEvidence&) const = default;
};
struct EventRecord {
    std::string eventId, title, summary;
    std::map<std::string, std::string> localizedTitles;
    // sourceStatus 是公告事实；StatusAt 的时钟派生不修改来源事实。
    // sourceStatus is an announcement fact; clock-derived StatusAt never mutates it.
    EventStatus sourceStatus{EventStatus::Unknown};
    std::optional<Timestamp> announcedAt, startsAt, endsAt, lastUpdatedAt;
    std::vector<EventMode> modes;
    std::vector<std::string> taskIds, itemIds, mapIds, bossIds;
    std::vector<EventEvidence> sourceEvidence;
    bool partial{true};
    bool operator==(const EventRecord&) const = default;
};
struct OfficialAnnouncement {
    std::string sourceRecordId, sourceUrl, title, summary;
    // 更新只能使用公告中明确指向原消息的身份；未知关联保留为 unresolved。
    // Updates require an explicit original-message identity; unknown links remain unresolved.
    std::optional<std::string> updatesRecordId;
    EventStatus status{EventStatus::Unknown};
    std::optional<Timestamp> publishedAt, startsAt, endsAt;
    std::vector<EventMode> modes;
    std::vector<std::string> linkedChangeRecordIds;
};
[[nodiscard]] std::optional<Timestamp> ParseTimestamp(std::string_view iso8601);
[[nodiscard]] EventStatus StatusAt(const EventRecord&, Timestamp now) noexcept;
inline constexpr std::size_t kMaximumEvents = 256;
inline constexpr std::size_t kMaximumEvidence = 32;
}
