#include "plugins/CatalogPluginService.h"
#include "data/LocalizedName.h"
#include "raid/RaidJson.h"
#include <algorithm>

namespace noven::plugins {
namespace {
using raid::json::Quote;
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
        const auto encode=[&](bool en){return Names("stableMapId",map.id,map.nameZh,map.nameEn,en)+","+Field("dataset","regular")+","+Field("players",map.players)
            +",\"raidDuration\":"+std::to_string(map.raidDuration)+","+Field("author",map.author)+"}";};
        records_[2].push_back({map.id,encode(false),encode(true)});
    }
    for(auto& records:records_)std::sort(records.begin(),records.end(),[](const auto& a,const auto& b){return a.id<b.id;});
}
DataResult CatalogPluginService::Error(const DataRequest& request,DataStatus status){return {request.requestId,status,Envelope(request,status,{},"null")};}
DataResult CatalogPluginService::Query(const DataRequest& request,const CatalogGrants& grants) const {
    if(!ValidDataRequest(request))return Error(request,DataStatus::InvalidRequest);
    if(!grants.Allows(request.catalog))return Error(request,DataStatus::PermissionDenied);
    const auto& rows=records_[static_cast<std::size_t>(request.catalog)-1];
    if(rows.empty())return Error(request,DataStatus::Unavailable);
    const bool english=english_.load();
    if(request.operation==DataOperation::Get){
        const auto row=std::lower_bound(rows.begin(),rows.end(),request.stableId,[](const auto& record,const auto& id){return record.id<id;});
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
