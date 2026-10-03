#pragma once
#include "events/EventCache.h"
#include <stop_token>

namespace noven::events {
struct EventSourceResult {
    bool success{},notModified{};
    std::vector<OfficialAnnouncement> announcements;
    EventSourceState state;
    std::string error;
};
class IEventSource {
public:
    virtual ~IEventSource()=default;
    virtual EventSourceResult Fetch(const EventSourceState&,std::stop_token)=0;
};
}
