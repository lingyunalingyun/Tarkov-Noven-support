#pragma once
#include "events/EventTypes.h"
#include <map>

namespace noven::events {
enum class EntityKind { Map, Task, Item, Boss };
struct EventEntity {
    EntityKind kind{};
    std::string id, name;
    bool operator==(const EventEntity&) const = default;
};
using EntityKey = std::pair<EntityKind,std::string>;
// 目录仅注入已验证的身份/本地译名；浏览器不能从自由文本推测关联。
// Inject verified identities/local names only; the browser never infers associations from free text.
class EventBrowser final {
public:
    void SetEvents(std::vector<EventRecord> records);
    void SetEntities(std::map<EntityKey,std::string> names);
    void SetFilter(std::optional<EventStatus> status, std::string query);
    void SetNow(Timestamp now);
    const std::vector<EventRecord>& Events() const noexcept {return events_;}
    const std::vector<std::size_t>& Rows() const noexcept {return rows_;}
    const EventRecord* Find(std::string_view id) const noexcept;
    EventStatus Status(const EventRecord& event) const noexcept {return StatusAt(event,now_);}
    std::vector<EventEntity> Associations(const EventRecord& event) const;
    std::size_t Unresolved(const EventRecord& event) const;
    std::size_t Count(EventStatus status) const;
    std::size_t Builds() const noexcept {return builds_;}
    std::optional<EventStatus> Filter() const noexcept {return filter_;}
    const std::string& Query() const noexcept {return query_;}
private:
    void PrepareSearch();
    void Rebuild();
    std::vector<EventRecord> events_;
    std::map<EntityKey,std::string> names_;
    std::vector<std::string> search_;
    std::vector<std::size_t> rows_;
    std::optional<EventStatus> filter_;
    std::string query_;
    Timestamp now_{};
    std::size_t builds_{};
};
// 外部来源动作只接受已支持的 HTTPS 来源和数值记录 ID，不打开任意缓存 URL。
// External actions accept supported HTTPS origins and numeric record IDs, never arbitrary cached URLs.
bool SafeEventSourceUrl(std::string_view url) noexcept;
}
