#include "data/TaskBrowser.h"
#include "data/LocalizedName.h"

#include <algorithm>
#include <cctype>
#include <tuple>
#include <unordered_map>

namespace noven::data {
namespace {
std::string Lower(std::string_view value){std::string result(value);std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return result;}
bool Contains(std::string_view value,const std::string& query){return Lower(value).find(query)!=std::string::npos;}
bool ItemMatchesQuery(const ItemRecord& item,const std::string& query){
    if(Contains(item.id,query)||Contains(item.nameZh,query)||Contains(item.shortNameZh,query)||Contains(item.nameEn,query)||Contains(item.shortNameEn,query))return true;
    return std::any_of(item.aliases.begin(),item.aliases.end(),[&](const auto& alias){return Contains(alias.text,query);});
}
int TraderRank(std::string_view id){
    static const std::unordered_map<std::string_view,int> ranks{
        {"54cb50c76803fa8b248b4571",0},{"54cb57776803fa99248b456e",1},
        {"579dc571d53a0658a154fbec",2},{"58330581ace78e27b8b10cee",3},
        {"5935c25fb3acc3127c3d8cd9",4},{"5a7c2eca46aef81a7ca2145d",5},
        {"5ac3b934156ae10c4430e83c",6},{"5c0647fdd443bc2504c2d371",7},
        {"6617beeaa9cfa777ca915b7c",8},{"638f541a29ffd1183d187f57",9},
        {"656f0f98d80a697f855d34b1",10}};
    const auto found=ranks.find(id);return found==ranks.end()?100:found->second;
}
}
std::vector<TaskView> TaskBrowser::Query(std::string_view query,GameMode mode,std::string_view locale) const {
    const auto structure=tasks_.StructureMode(mode);auto normalized=Lower(query);std::vector<TaskView> result;
    const bool rewardQuery=!normalized.empty()&&normalized.front()=='#';
    if(rewardQuery){normalized.erase(normalized.begin());while(!normalized.empty()&&std::isspace(static_cast<unsigned char>(normalized.front())))normalized.erase(normalized.begin());}
    for(const auto& task:tasks_.Tasks()){
        if(task.mode!=structure)continue;const auto* trader=tasks_.Trader(structure,task.traderId);if(!trader)continue;
        bool match=false;
        if(rewardQuery){
            if(!normalized.empty())for(const auto& reward:task.rewards){
                if(reward.type!="item"&&reward.type!="craftUnlock"&&reward.type!="offerUnlock")continue;
                const auto* item=items_.FindById(reward.targetId);
                if(item&&ItemMatchesQuery(*item,normalized)){match=true;break;}
            }
        } else match=normalized.empty()||Contains(task.id,normalized)||Contains(task.nameZh,normalized)||Contains(task.nameEn,normalized)
            ||Contains(task.locationZh,normalized)||Contains(task.locationEn,normalized)||Contains(trader->nameZh,normalized)||Contains(trader->nameEn,normalized);
        if(!rewardQuery&&!match)for(const auto& objective:task.objectives){
            match=Contains(objective.descriptionZh,normalized)||Contains(objective.descriptionEn,normalized);
            if(!match)for(const auto& id:objective.itemIds)if(const auto* item=items_.FindById(id);item&&ItemMatchesQuery(*item,normalized)){match=true;break;}
            if(match)break;
        }
        if(match)result.push_back({&task,trader});
    }
    std::sort(result.begin(),result.end(),[&](const auto& a,const auto& b){
        const auto& at=LocalizedName(a.trader->nameZh,a.trader->nameEn,locale);const auto& bt=LocalizedName(b.trader->nameZh,b.trader->nameEn,locale);
        const auto& an=LocalizedName(a.source->nameZh,a.source->nameEn,locale);const auto& bn=LocalizedName(b.source->nameZh,b.source->nameEn,locale);
        return std::tuple(TraderRank(a.trader->id),at,an,a.source->id)
            <std::tuple(TraderRank(b.trader->id),bt,bn,b.source->id);
    });return result;
}
} // namespace noven::data
