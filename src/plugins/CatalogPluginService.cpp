#include "plugins/CatalogPluginService.h"
#include "data/LocalizedName.h"
#include "raid/RaidJson.h"
#include <algorithm>
#include <stdexcept>

namespace noven::plugins {
namespace {
using raid::json::Quote;
std::string Number(std::optional<std::int64_t> number){return number?std::to_string(*number):"null";}
std::string_view Mode(raid::GameMode mode){switch(mode){case raid::GameMode::PvP:return "pvp";case raid::GameMode::PvE:return "pve";case raid::GameMode::Practice:return "practice";case raid::GameMode::Offline:return "offline";default:return "unknown";}}
std::string_view Outcome(raid::RaidOutcome outcome){switch(outcome){case raid::RaidOutcome::Survived:return "survived";case raid::RaidOutcome::RunThrough:return "runThrough";case raid::RaidOutcome::KIA:return "kia";case raid::RaidOutcome::MIA:return "mia";case raid::RaidOutcome::Left:return "left";default:return "unknown";}}
std::string_view Status(events::EventStatus status){switch(status){case events::EventStatus::Upcoming:return "upcoming";case events::EventStatus::Active:return "active";case events::EventStatus::Ended:return "ended";default:return "unknown";}}
std::string_view Source(events::SourceKind source){switch(source){case events::SourceKind::OfficialTelegram:return "officialTelegram";case events::SourceKind::TarkovDev:return "tarkovDev";case events::SourceKind::TarkovChanges:return "tarkovChanges";case events::SourceKind::CommunityWiki:return "communityWiki";}return "unknown";}
std::string Field(std::string_view key,std::string_view value){return Quote(key)+":"+Quote(value);}
std::string Names(std::string_view idKey,const std::string& id,const std::string& zh,const std::string& en,bool english){
    return "{"+Field(idKey,id)+","+Field("nameZh",zh)+","+Field("nameEn",en)+","+Field("displayName",data::LocalizedName(zh,en,english?"en-US":"zh-CN"));
}
std::string Envelope(const DataRequest& request,DataStatus status,std::string_view records,std::string_view record,
    std::size_t total=0,std::size_t next=0,bool more=false){
    return "{\"schemaVersion\":1,\"requestId\":"+std::to_string(request.requestId)+","+Field("catalog",CatalogName(request.catalog))+","
        +Field("operation",request.operation==DataOperation::List?"list":"get")+","+Field("status",DataStatusName(status))+",\"records\":["+std::string(records)
        +"],\"record\":"+std::string(record)+",\"offset\":"+std::to_string(request.offset)+",\"limit\":"+std::to_string(request.limit)
        +",\"total\":"+std::to_string(total)+",\"nextOffset\":"+std::to_string(next)+",\"hasMore\":"+(more?"true":"false")+"}";
}
}
CatalogPluginService::CatalogPluginService(std::span<const data::ItemRecord> items,std::span<const data::TaskRecord> tasks,
    std::span<const data::MapRecord> maps,std::string_view locale){
    SetLocale(locale);
    for(const auto& item:items){
        const auto encode=[&](bool en){auto text=Names("stableItemId",item.id,item.nameZh,item.nameEn,en)+",\"width\":"+std::to_string(item.width)
            +",\"height\":"+std::to_string(item.height)+","+Field("caliber",item.caliber)+",\"types\":[";
            for(std::size_t i=0;i<item.types.size();++i){if(i)text+=',';text+=Quote(item.types[i]);}return text+"]}";};
        records_[0].push_back({item.id,encode(false),encode(true)});
    }
    // v1 固定为 regular 结构，避免 PvE 与 regular 同 ID 产生歧义；不依赖用户当前游戏模式。
    // V1 fixes task/map structure to regular, avoiding same-ID mode ambiguity and live/selected game mode dependency.
    for(const auto& task:tasks)if(task.mode=="regular"){
        const auto encode=[&](bool en){return Names("stableTaskId",task.id,task.nameZh,task.nameEn,en)+","+Field("dataset","regular")+","+Field("traderId",task.traderId)
            +","+Field("location",data::LocalizedName(task.locationZh,task.locationEn,en?"en-US":"zh-CN"))+","+Field("faction",task.faction)
            +",\"minimumLevel\":"+std::to_string(task.minimumLevel)+",\"experience\":"+std::to_string(task.experience)
            +",\"kappaRequired\":"+(task.kappaRequired?"true":"false")+",\"lightkeeperRequired\":"+(task.lightkeeperRequired?"true":"false")+"}";};
        records_[1].push_back({task.id,encode(false),encode(true)});
    }
    for(const auto& map:maps){
        mapNames_.emplace(map.id,std::pair{map.nameZh,map.nameEn});
        const auto encode=[&](bool en){return Names("stableMapId",map.id,map.nameZh,map.nameEn,en)+","+Field("dataset","regular")+","+Field("players",map.players)
            +",\"raidDuration\":"+std::to_string(map.raidDuration)+","+Field("author",map.author)+"}";};
        records_[2].push_back({map.id,encode(false),encode(true)});
    }
    for(auto& records:records_)std::sort(records.begin(),records.end(),[](const auto& a,const auto& b){return a.id<b.id;});
}
void CatalogPluginService::PublishRaidHistory(std::span<const raid::RaidSession> completed){
    if(completed.size()>100000)throw std::runtime_error("history snapshot capacity");
    auto rows=std::make_shared<std::vector<Record>>();rows->reserve(completed.size());
    std::vector<const raid::RaidSession*> sorted;
    for(const auto& session:completed)if(session.startObserved&&session.endObserved&&!session.localSessionId.empty())sorted.push_back(&session);
    std::sort(sorted.begin(),sorted.end(),[](const auto* a,const auto* b){return a->startedAt!=b->startedAt?a->startedAt>b->startedAt:a->localSessionId<b->localSessionId;});
    for(const auto* session:sorted){
        const auto& s=*session;const auto map=mapNames_.find(s.mapId);
        const auto encode=[&](bool en){return "{"+Field("localSessionId",s.localSessionId)+",\"stableMapId\":"+(map==mapNames_.end()?"null":Quote(s.mapId))
            +",\"mapDisplayName\":"+(map==mapNames_.end()?"null":Quote(data::LocalizedName(map->second.first,map->second.second,en?"en-US":"zh-CN")))
            +",\"mapKnown\":"+(map==mapNames_.end()?"false":"true")+","+Field("timeBasis","local-wall-clock-ms")
            +",\"startedAt\":"+Number(s.startedAt)+",\"endedAt\":"+Number(s.endedAt)+",\"durationMs\":"+Number(s.duration)
            +","+Field("completion","completed")+","+Field("gameMode",Mode(s.gameMode))+","+Field("raidType",s.raidType==raid::RaidType::PMC?"pmc":s.raidType==raid::RaidType::Scav?"scav":"unknown")
            +","+Field("outcome",Outcome(s.outcome))+"}";};
        rows->push_back({s.localSessionId,encode(false),encode(true)});
    }
    history_.store(std::move(rows));
}
void CatalogPluginService::PublishEvents(std::span<const events::EventRecord> snapshot){
    if(snapshot.size()>events::kMaximumEvents)throw std::runtime_error("event snapshot capacity");
    auto rows=std::make_shared<std::vector<Record>>();rows->reserve(snapshot.size());
    for(const auto& event:snapshot){
        const bool community=events::CommunitySourced(event);
        const bool official=event.eventId.starts_with("official-telegram:");
        const auto encode=[&](std::string_view locale){
            const auto title=event.localizedTitles.find(std::string(locale));
            auto text="{"+Field("eventId",event.eventId)+","+Field("title",title==event.localizedTitles.end()?event.title:title->second)
                +","+Field("originalTitle",event.title)+","+Field("summary",event.summary)+","+Field("source",community?"communityWiki":official?"officialTelegram":"unknown")
                +",\"official\":"+(official?"true":"false")+","+Field("sourceStatus",Status(event.sourceStatus))+","+Field("timeBasis","utc-seconds")
                +",\"announcedAt\":"+Number(event.announcedAt)+",\"startsAt\":"+Number(event.startsAt)+",\"endsAt\":"+Number(event.endsAt)
                +",\"lastUpdatedAt\":"+Number(event.lastUpdatedAt)+",\"startsAtFromPublication\":"+(event.startsAtFromPublication?"true":"false")+",\"sourceKinds\":[";
            std::vector<events::SourceKind> seen;
            for(const auto& evidence:event.sourceEvidence)if(std::find(seen.begin(),seen.end(),evidence.sourceKind)==seen.end()){
                if(!seen.empty())text+=',';seen.push_back(evidence.sourceKind);text+=Quote(Source(evidence.sourceKind));
            }
            return text+"]}";
        };
        rows->push_back({event.eventId,encode("zh-CN"),encode("en-US")});
    }
    std::sort(rows->begin(),rows->end(),[](const auto& a,const auto& b){return a.id<b.id;});events_.store(std::move(rows));
}
void CatalogPluginService::PublishRecentScans(std::span<const data::RecentScanEntry> scans){
    if(scans.size()>data::RecentScanStore::kMaxEntries)throw std::runtime_error("scan snapshot capacity");
    auto rows=std::make_shared<std::vector<Record>>();rows->reserve(scans.size());
    for(const auto& scan:scans)try{auto text=ScanRecord(scan);rows->push_back({std::to_string(scan.scanId),text,std::move(text)});}catch(const std::runtime_error&){}
    // Recent Scans 的现有顺序（最新追加在前）不变；Get 使用真正持久化身份。
    // Preserve Recent Scans order (newest append first); Get uses the real persisted identity.
    scans_.store(std::move(rows));
}
DataResult CatalogPluginService::Error(const DataRequest& request,DataStatus status){return {request.requestId,status,Envelope(request,status,{},"null")};}
DataResult CatalogPluginService::Query(const DataRequest& request,const CatalogGrants& grants) const {
    if(!ValidDataRequest(request))return Error(request,DataStatus::InvalidRequest);
    if(!grants.Allows(request.catalog))return Error(request,DataStatus::PermissionDenied);
    const auto snapshot=request.catalog==CatalogKind::RaidHistory?history_.load():request.catalog==CatalogKind::Events?events_.load():request.catalog==CatalogKind::RecentScans?scans_.load():nullptr;
    if(static_cast<unsigned>(request.catalog)>3&&!snapshot)return Error(request,DataStatus::Unavailable);
    const auto& rows=snapshot?*snapshot:records_[static_cast<std::size_t>(request.catalog)-1];
    if(rows.empty()&&!snapshot)return Error(request,DataStatus::Unavailable);
    const bool english=english_.load();
    if(request.operation==DataOperation::Get){
        const auto row=(request.catalog==CatalogKind::RaidHistory||request.catalog==CatalogKind::RecentScans)?std::find_if(rows.begin(),rows.end(),[&](const auto& record){return record.id==request.stableId;})
            :std::lower_bound(rows.begin(),rows.end(),request.stableId,[](const auto& record,const auto& id){return record.id<id;});
        if(row==rows.end()||row->id!=request.stableId)return Error(request,DataStatus::NotFound);
        auto text=Envelope(request,DataStatus::Ok,{},english?row->en:row->zh,rows.size());
        if(text.size()>MaximumCatalogPayloadBytes)return Error(request,DataStatus::TooLarge);
        return {request.requestId,DataStatus::Ok,std::move(text)};
    }
    const auto start=std::min<std::size_t>(request.offset,rows.size()),end=std::min<std::size_t>(start+request.limit,rows.size());
    std::string records;auto next=start;
    for(;next<end;++next){const auto& text=english?rows[next].en:rows[next].zh;
        if(records.size()+text.size()+1>MaximumCatalogPayloadBytes-512)break;
        if(next>start)records+=',';records+=text;
    }
    if(next==start&&start<end)return Error(request,DataStatus::TooLarge);
    return {request.requestId,DataStatus::Ok,Envelope(request,DataStatus::Ok,records,"null",rows.size(),next,next<rows.size())};
}
}
