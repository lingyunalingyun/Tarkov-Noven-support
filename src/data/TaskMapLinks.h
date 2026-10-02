#pragma once
#include "data/MapCatalog.h"
#include <map>
#include <span>

namespace noven::data {
// 仅索引已加载任务坐标；复合目标 ID 与生成器 sourceId 前两段完全相同。
// Index positioned tasks only; composite objective IDs equal the first two generator sourceId fields.
class TaskMapLinks final {
public:
    struct Target final {
        std::string id,mapId,mapZh,mapEn;
        MapWorldPosition position;
    };
    void Bind(const MapCatalog& catalog){
        links_.clear();
        metadata_.clear();
        for(const auto& point:catalog.Points())if(point.kind=="task"){
            const auto first=point.sourceId.find('_');if(first==std::string::npos||first==0)continue;
            const auto second=point.sourceId.find('_',first+1);const auto objective=point.sourceId.substr(0,second);
            if(objective.size()>first+1){
                links_[objective].push_back(point.id);
                const auto* map=catalog.Map(point.mapId);
                metadata_[point.id]={point.id,point.mapId,map?map->nameZh:std::string{},
                    map?map->nameEn:std::string{},point.position};
            }
        }
    }
    std::span<const std::string> Targets(std::string_view task,std::string_view objective) const {
        if(!objective.starts_with(std::string(task)+"_"))return {};
        const auto found=links_.find(objective);return found==links_.end()?std::span<const std::string>{}:found->second;
    }
    const Target* Metadata(std::string_view id) const {
        const auto found=metadata_.find(id);return found==metadata_.end()?nullptr:&found->second;
    }
private:
    // 自有 ID 跨页面驻留，不借用目录内存或虚构缺失坐标。
    // Own resident IDs without borrowing catalog memory or inventing missing coordinates.
    std::map<std::string,std::vector<std::string>,std::less<>> links_;
    std::map<std::string,Target,std::less<>> metadata_;
};
}
