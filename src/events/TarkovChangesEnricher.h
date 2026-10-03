#pragma once
#include "events/EventCatalog.h"
#include "events/EventHttp.h"

namespace noven::events {
struct ConfigurationChange {std::string key,oldValue,newValue;};
struct ChangeRecord {
    std::string sourceRecordId,sourceUrl;
    std::optional<Timestamp> publishedAt;
    std::vector<ConfigurationChange> changes;
};
class TarkovChangesEnricher final {
public:
    explicit TarkovChangesEnricher(IEventHttp& http):http_(http){}
    bool Fetch(std::string_view recordId,ChangeRecord&,std::string& error);
    static bool Parse(const HttpResponse&,std::string_view recordId,ChangeRecord&,std::string& error);
    static bool Attach(EventCatalog&,std::string_view eventId,const ChangeRecord&,std::string& error);
private:
    IEventHttp& http_;
};
}
