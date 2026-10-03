#pragma once
#include "events/EventCatalog.h"
#include <filesystem>

namespace noven::events {
struct EventSourceState {
    std::string etag, lastModified, newestMessageId;
    std::optional<Timestamp> lastSuccessfulRefresh;
    bool operator==(const EventSourceState&) const = default;
};
// 一个原子文件同时提交目录和游标，避免游标领先于已落盘内容。
// One atomic file commits catalog and cursor together, preventing cursor/data divergence.
class EventCache final {
public:
    EventCache() = default;
    ~EventCache();
    EventCache(const EventCache&) = delete;
    EventCache& operator=(const EventCache&) = delete;
    bool Load(const std::filesystem::path&, EventCatalog&, EventSourceState&, std::string& error);
    bool Save(const EventCatalog&, const EventSourceState&, std::string& error);
    static std::string Encode(const EventCatalog&, const EventSourceState&);
    static bool Decode(std::string_view, EventCatalog&, EventSourceState&, std::string& error);
private:
    std::filesystem::path file_;
    void* lease_{reinterpret_cast<void*>(-1)};
    bool writable_{};
};
}
