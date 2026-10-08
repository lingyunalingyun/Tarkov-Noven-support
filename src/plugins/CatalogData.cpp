#include "plugins/CatalogData.h"
#include <algorithm>
#include <limits>

namespace noven::plugins {
std::string_view CatalogName(CatalogKind kind){switch(kind){case CatalogKind::Items:return "items";case CatalogKind::Tasks:return "tasks";case CatalogKind::Maps:return "maps";}return {};}
std::string_view CatalogPermission(CatalogKind kind){switch(kind){case CatalogKind::Items:return "catalog.items.read";case CatalogKind::Tasks:return "catalog.tasks.read";case CatalogKind::Maps:return "catalog.maps.read";}return {};}
std::string_view DataStatusName(DataStatus status){switch(status){case DataStatus::Ok:return "ok";case DataStatus::PermissionDenied:return "permissionDenied";case DataStatus::NotFound:return "notFound";case DataStatus::InvalidRequest:return "invalidRequest";case DataStatus::Unavailable:return "unavailable";case DataStatus::TooLarge:return "tooLarge";case DataStatus::Limited:return "limited";}return {};}
bool ValidDataRequest(const DataRequest& request){
    if(!request.requestId||request.requestId>static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())||CatalogName(request.catalog).empty())return false;
    if(request.operation==DataOperation::List)return request.stableId.empty()&&request.limit>0&&request.limit<=MaximumCatalogRecords
        &&request.offset<=(std::numeric_limits<std::uint32_t>::max)()-request.limit;
    if(request.operation!=DataOperation::Get||request.offset||request.limit||request.stableId.empty()||request.stableId.size()>128)return false;
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
