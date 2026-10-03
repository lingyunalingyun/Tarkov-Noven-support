#include "events/EventEnrichment.h"
#include "events/TarkovDevEventEnricher.h"
#include "events/TarkovChangesEnricher.h"
#include "data/ItemCatalog.h"
#include "data/TaskCatalog.h"
#include "data/MapCatalog.h"
#include <algorithm>
#include <set>
#include <map>

namespace noven::events {
EventEnrichment MakeEventEnrichment(std::filesystem::path assets,IEventHttp& http) {
    return [assets=std::move(assets),&http](EventCatalog& catalog,std::stop_token stop,std::string& warning) {
        if(catalog.Events().empty())return true;
        data::ItemCatalog items;data::TaskCatalog tasks;data::MapCatalog maps;std::wstring loadError;
        // 本地 DEV 资产已由各模块生成验证；临时目录/索引在刷新结束释放。
        // Reuse validated generated DEV assets; temporary catalogs/index are released after refresh.
        if(!items.Load(assets/L"data"/L"items_catalog.tsv",loadError)
            || !tasks.Load(assets/L"data",loadError) || !maps.Load(assets/L"data",loadError)) {
            warning="DEV identity catalogs unavailable; official facts retained";return false;
        }
        TarkovDevEventEnricher dev(items,tasks,maps);std::vector<std::string> ids;
        for(const auto& event:catalog.Events())ids.push_back(event.eventId);
        for(const auto& id:ids) {
            if(stop.stop_requested())return false;
            std::string error;if(!dev.EnrichText(catalog,id,error)){warning=error;return false;}
        }
        TarkovChangesEnricher changes(http);std::map<std::string,ChangeRecord> fetched;std::set<std::string> failed;unsigned requests{};
        for(const auto& id:ids) {
            const auto* event=catalog.FindEvent(id);std::set<std::string> linked;
            for(const auto& evidence:event->sourceEvidence)if(evidence.sourceKind==SourceKind::OfficialTelegram)
                for(const auto& change:evidence.linkedChangeRecordIds)linked.insert(change);
            for(const auto& change:linked) {
                if(stop.stop_requested())return false;
                const bool already=std::ranges::any_of(event->sourceEvidence,[&](const auto& evidence){return evidence.sourceKind==SourceKind::TarkovChanges && evidence.sourceRecordId==change;});
                if(already || failed.contains(change))continue;
                std::string error;
                if(!fetched.contains(change)) {
                    if(requests>=4){warning="change refresh request window exhausted";continue;}
                    ++requests;ChangeRecord record;
                    if(!changes.Fetch(change,record,error)){failed.insert(change);warning=error;continue;}
                    fetched.emplace(change,std::move(record));
                }
                if(!changes.Attach(catalog,id,fetched.at(change),error))warning=error;
                event=catalog.FindEvent(id);
            }
        }
        return true;
    };
}
}
