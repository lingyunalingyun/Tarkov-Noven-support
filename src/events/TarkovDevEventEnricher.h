#pragma once
#include "events/EventCatalog.h"
#include <span>
#include <unordered_map>

namespace noven::data {class ItemCatalog;class TaskCatalog;class MapCatalog;}
namespace noven::events {
enum class EntityKind { Item, Task, Map, Boss };
struct EntityAlias {EntityKind kind{};std::string id,name;};
struct EntityReference {EntityKind kind{};std::string name;};
class TarkovDevEventEnricher final {
public:
    explicit TarkovDevEventEnricher(std::span<const EntityAlias>);
    TarkovDevEventEnricher(const data::ItemCatalog&,const data::TaskCatalog&,const data::MapCatalog&);
    [[nodiscard]] std::optional<std::string> Resolve(EntityKind,std::string_view) const;
    bool EnrichReferences(EventCatalog&,std::string_view eventId,std::span<const EntityReference>,std::string& error) const;
    bool EnrichText(EventCatalog&,std::string_view eventId,std::string& error) const;
private:
    void Add(const EntityAlias&);
    std::unordered_map<std::string,std::vector<std::string>> aliases_;
};
}
