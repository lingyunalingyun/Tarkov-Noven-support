#include "data/HideoutBrowser.h"
#include "common/DebugLog.h"
#include <algorithm>
#include <limits>
#include <tuple>

namespace noven::data {
namespace {
std::string Normalize(std::string_view text) {
    std::string result;
    for (unsigned char c : text) {
        if (c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='-') continue;
        result += static_cast<char>(c>='A' && c<='Z' ? c+32 : c);
    }
    return result;
}
int Rank(const std::string& query, std::string_view name) {
    const auto n=Normalize(name);
    return n==query ? 0 : n.starts_with(query) ? 1 : n.find(query)!=std::string::npos ? 2 : 9;
}
int ItemRank(const ItemCatalog& items,const std::string& needle,const std::string& id) {
    int rank=needle==Normalize(id)?0:9;
    if(const auto* item=items.FindById(id))
        for(const auto* text:{&item->nameZh,&item->nameEn,&item->shortNameZh,&item->shortNameEn})
            rank=(std::min)(rank,Rank(needle,*text));
    return rank;
}
}
bool AddFleaEstimate(HideoutMaterial& m, std::int64_t& total) {
    m.subtotal.reset();
    if (m.status != FleaStatus::Allowed || !m.unitPrice || *m.unitPrice<=0 || m.count<=0) return false;
    // 收购成本只用可用跳蚤价；商人回收价永远不能作为替代。
    // Acquisition cost uses available flea prices only, never trader sell proceeds.
    constexpr auto max=(std::numeric_limits<std::int64_t>::max)();
    if (*m.unitPrice>max/m.count || total>max-*m.unitPrice*m.count) {
        common::DebugLog(L"[hideout] material estimate overflow"); return false;
    }
    m.subtotal=*m.unitPrice*m.count; total+=*m.subtotal; return true;
}
std::vector<HideoutCraftView> HideoutBrowser::Crafts(const HideoutLevel& level, std::string_view locale,std::string_view query) const {
    auto needle=Normalize(query);
    const bool requirements=needle.starts_with('#');
    if(requirements) needle.erase(0,1);
    if(requirements && !needle.empty()) return {};
    const auto* station=hideout_.Station(level.mode,level.stationId);
    const bool filtered=!needle.empty() && (requirements ||
        (Rank(needle,station->nameZh)>=9 && Rank(needle,station->nameEn)>=9));
    const auto name=[&](const std::string& id) {
        const auto* item=items_.FindById(id);
        return item?LocalizedName(item->nameZh,item->nameEn,locale):id;
    };
    std::vector<HideoutCraftView> result;
    // 显示当前查看等级及以下解锁的配方；不判断玩家进度，也不计算利润。
    // Show recipes unlocked at or below the viewed level, without player state or profit calculations.
    for (const auto& craft:hideout_.Crafts()) if(craft.mode==level.mode && craft.stationId==level.stationId && craft.level<=level.level) {
        if(filtered) {
            const bool matches=ItemRank(items_,needle,craft.itemId)<9;
            if(!matches) continue;
        }
        HideoutCraftView view; view.source=&craft; view.name=name(craft.itemId);
        for(const auto& m:craft.materials) {
            view.materials.push_back({m.itemId,name(m.itemId),m.count,m.tool,m.functional});
        }
        result.push_back(std::move(view));
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return std::tie(a.name,a.source->id)<std::tie(b.name,b.source->id);});
    return result;
}
std::vector<HideoutRow> HideoutBrowser::Query(std::string_view query, GameMode mode, std::string_view locale) const {
    auto needle=Normalize(query);
    const bool requirements=needle.starts_with('#');
    if(requirements) needle.erase(0,1);
    // 普通查询匹配设施/产物；# 仅匹配设施建造/升级材料，不包含制造原料。
    // Plain queries match stations/outputs; # matches construction/upgrade materials, not recipe inputs.
    const auto itemRank=[&](const std::string& id) {
        return ItemRank(items_,needle,id);
    };
    struct Candidate { const HideoutLevel* level; int rank; std::string name; };
    std::vector<Candidate> candidates;
    for (const auto& level : hideout_.Levels()) {
        if (level.mode!=hideout_.StructureMode(mode)) continue;
        const auto* station=hideout_.Station(level.mode,level.stationId);
        int rank=needle.empty()?0:requirements?9:(std::min)(Rank(needle,station->nameZh),Rank(needle,station->nameEn));
        if(!needle.empty()) {
            if(requirements) for(const auto& req:level.items) rank=(std::min)(rank,itemRank(req.itemId));
            if(!requirements) for(const auto& craft:hideout_.Crafts()) {
                if(craft.mode!=level.mode || craft.stationId!=level.stationId || craft.level>level.level) continue;
                rank=(std::min)(rank,3+itemRank(craft.itemId));
            }
        }
        if (rank<9) candidates.push_back({&level,rank,LocalizedName(station->nameZh,station->nameEn,locale)});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
        return std::tie(a.rank,a.name,a.level->level,a.level->id)<std::tie(b.rank,b.name,b.level->level,b.level->id); });
    std::vector<HideoutRow> result;
    for (const auto& c : candidates) {
        HideoutRow row; row.source=c.level; row.name=c.name;
        const auto* station=hideout_.Station(c.level->mode,c.level->stationId);
        row.filteredDetails=!needle.empty() && (requirements ||
            (Rank(needle,station->nameZh)>=9 && Rank(needle,station->nameEn)>=9));
        for (const auto& req : c.level->items) {
            HideoutMaterial m; m.id=req.itemId; m.name=m.id; m.count=req.count;
            if (const auto* item=items_.FindById(m.id)) m.name=LocalizedName(item->nameZh,item->nameEn,locale);
            if (const auto* economy=economy_.Lookup(mode,m.id)) {
                m.status=economy->fleaStatus;
                if (m.status==FleaStatus::Allowed) m.unitPrice=economy->fleaPrice;
            }
            if (!AddFleaEstimate(m,row.knownSubtotal)) ++row.unknownRequirementCount;
            if(!row.filteredDetails || (requirements && itemRank(req.itemId)<9)) row.materials.push_back(std::move(m));
        }
        row.completeEstimate=row.unknownRequirementCount==0;
        // 过滤只改变可见明细，不把局部材料费用冒充完整升级费用。
        // Filtering changes visible details only, never reinterprets partial materials as full upgrade costs.
        if(row.filteredDetails) { result.push_back(std::move(row)); continue; }
        for (const auto& req : c.level->stations) {
            const auto* requiredStation=hideout_.Station(c.level->mode,req.stationId);
            row.stations.push_back({LocalizedName(requiredStation->nameZh,requiredStation->nameEn,locale),req.level});
        }
        for (bool trader : {false,true}) for (const auto& req : trader ? c.level->traders : c.level->skills)
            (trader ? row.traders : row.skills).push_back({LocalizedName(req.nameZh,req.nameEn,locale),req.level});
        result.push_back(std::move(row));
    }
    return result;
}
}
