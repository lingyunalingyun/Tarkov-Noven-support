#include "plugins/CatalogData.h"
#include <algorithm>
#include <limits>

namespace noven::plugins {
std::string_view CatalogName(CatalogKind kind){switch(kind){case CatalogKind::Items:return "items";case CatalogKind::Tasks:return "tasks";case CatalogKind::Maps:return "maps";case CatalogKind::RaidHistory:return "raidHistory";case CatalogKind::Events:return "events";}return {};}
std::string_view CatalogPermission(CatalogKind kind){switch(kind){case CatalogKind::Items:return "catalog.items.read";case CatalogKind::Tasks:return "catalog.tasks.read";case CatalogKind::Maps:return "catalog.maps.read";case CatalogKind::RaidHistory:return "raid.history.read";case CatalogKind::Events:return "catalog.events.read";}return {};}
// 沿用历史存储 256 字节及社区事件 15+6+240 字节身份，不截断；旧目录仍限 128。
// Preserve stored history's 256-byte and community events' 15+6+240-byte identities; old catalogs remain at 128.
std::size_t MaximumDataIdBytes(CatalogKind kind){return kind==CatalogKind::Events?261u:kind==CatalogKind::RaidHistory?256u:128u;}
std::string_view DataStatusName(DataStatus status){switch(status){case DataStatus::Ok:return "ok";case DataStatus::PermissionDenied:return "permissionDenied";case DataStatus::NotFound:return "notFound";case DataStatus::InvalidRequest:return "invalidRequest";case DataStatus::Unavailable:return "unavailable";case DataStatus::TooLarge:return "tooLarge";case DataStatus::Limited:return "limited";}return {};}
bool ValidDataRequest(const DataRequest& request){
    if(!request.requestId||request.requestId>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())||CatalogName(request.catalog).empty())return false;
    if(request.operation==DataOperation::List)return request.stableId.empty()&&request.limit>0&&request.limit<=MaximumCatalogRecords
        &&request.offset<=(std::numeric_limits<std::uint32_t>::max)()-request.limit;
    if(request.operation!=DataOperation::Get||request.offset||request.limit||request.stableId.empty()||request.stableId.size()>MaximumDataIdBytes(request.catalog))return false;
    if(request.catalog==CatalogKind::Events)return request.stableId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.:%")==std::string::npos;
    if(request.catalog==CatalogKind::RaidHistory)return request.stableId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:-")==std::string::npos;
    return request.stableId.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;
}
bool CatalogGrants::Allows(CatalogKind kind) const {
    const auto permission=CatalogPermission(kind);
    return valid&&!permission.empty()&&std::find(declared.begin(),declared.end(),permission)!=declared.end()
        &&std::find(granted.begin(),granted.end(),permission)!=granted.end();
}
RequestAdmission DataRequestBudget::Begin(std::uint64_t id,std::chrono::steady_clock::time_point now){
    if(pending_.contains(id))return RequestAdmission::Duplicate;
    if(now-window_>=std::chrono::seconds(1)){window_=now;count_=0;}
    if(pending_.size()>=MaximumDataRequests||count_>=rate_)return RequestAdmission::Limited;
    pending_.insert(id);++count_;return RequestAdmission::Accepted;
}
}
