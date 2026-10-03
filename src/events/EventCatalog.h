#pragma once
#include "events/EventTypes.h"
#include <span>

namespace noven::events {
class EventCatalog final {
public:
    bool Apply(std::span<const OfficialAnnouncement>, std::string& error);
    bool Restore(std::vector<EventRecord>, std::string& error);
    [[nodiscard]] const std::vector<EventRecord>& Events() const noexcept { return events_; }
    [[nodiscard]] const EventRecord* FindEvent(std::string_view id) const noexcept;
    [[nodiscard]] std::vector<EventRecord> ActiveEvents(Timestamp now) const;
    bool AttachEvidence(std::string_view eventId, EventEvidence evidence, std::string& error);
    [[nodiscard]] const std::vector<OfficialAnnouncement>& UnresolvedUpdates() const { return unresolved_; }
private:
    std::vector<EventRecord> events_;
    std::vector<OfficialAnnouncement> unresolved_;
};
bool ValidRecord(const EventRecord&);
bool ValidEvidence(const EventEvidence&);
}
