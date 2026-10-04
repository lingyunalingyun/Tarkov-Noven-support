#include "events/TarkovDevEventEnricher.h"
#include "data/ItemCatalog.h"
#include "data/TaskCatalog.h"
#include "data/MapCatalog.h"
#include <algorithm>
#include <cctype>

namespace noven::events {
namespace {
std::string Normalize(std::string_view name) {
    std::string text(name);for(auto& c:text)if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;
}
std::string Key(EntityKind kind,std::string_view name){return std::to_string(static_cast<int>(kind))+":"+Normalize(name);}
bool Word(unsigned char c){return c<128 && (std::isalnum(c) || c=='_');}
bool Mention(std::string_view text,std::string_view name) {
    if(name.size()<4)return false;
    std::size_t p{};while((p=text.find(name,p))!=text.npos) {
        const auto end=p+name.size();
        if((p==0 || !Word(static_cast<unsigned char>(text[p-1])) || !Word(static_cast<unsigned char>(name.front())))
            && (end==text.size() || !Word(static_cast<unsigned char>(text[end])) || !Word(static_cast<unsigned char>(name.back()))))return true;
        ++p;
    }return false;
}
}
void TarkovDevEventEnricher::Add(const EntityAlias& a) {
    if(a.id.empty() || a.name.empty())return;
    auto& values=aliases_[Key(a.kind,a.name)];if(std::ranges::find(values,a.id)==values.end())values.push_back(a.id);
}
TarkovDevEventEnricher::TarkovDevEventEnricher(std::span<const EntityAlias> aliases){for(const auto& alias:aliases)Add(alias);}
TarkovDevEventEnricher::TarkovDevEventEnricher(const data::ItemCatalog& items,const data::TaskCatalog& tasks,const data::MapCatalog& maps) {
    // 只索引现有 DEV 目录的完整名称，短名称/OCR 模糊规则不适用于公告事实。
    // Index existing DEV full names only; short-name/OCR fuzzy rules do not apply to announcement facts.
    for(const auto& item:items.Items()) {Add({EntityKind::Item,item.id,item.nameEn});Add({EntityKind::Item,item.id,item.nameZh});}
    for(const auto& task:tasks.Tasks()){Add({EntityKind::Task,task.id,task.nameEn});Add({EntityKind::Task,task.id,task.nameZh});}
    for(const auto& map:maps.Maps()){Add({EntityKind::Map,map.id,map.nameEn});Add({EntityKind::Map,map.id,map.nameZh});}
    // 当前地图目录已保留 API mob 身份；不以出生点 ID 代替 Boss 身份。
    // Current map catalog retains API mob identity; never substitute spawn-point IDs for boss identities.
    for(const auto& point:maps.Points())if(point.kind=="boss" && !point.sourceId.empty()) {
        Add({EntityKind::Boss,point.sourceId,point.nameEn});Add({EntityKind::Boss,point.sourceId,point.nameZh});
    }
}
std::optional<std::string> TarkovDevEventEnricher::Resolve(EntityKind kind,std::string_view name) const {
    const auto it=aliases_.find(Key(kind,name));if(it==aliases_.end() || it->second.size()!=1)return {};return it->second.front();
}
bool TarkovDevEventEnricher::EnrichReferences(EventCatalog& catalog,std::string_view id,std::span<const EntityReference> refs,std::string& error) const {
    if(!catalog.FindEvent(id)){error="enrichment event missing";return false;}
    if(refs.size()>256){error="entity reference capacity exceeded";return false;}
    EventEvidence evidence;evidence.evidenceId="tarkov-dev:"+std::string(id);evidence.sourceRecordId=std::string(id);
    evidence.sourceKind=SourceKind::TarkovDev;evidence.type=EvidenceType::EntityReference;evidence.sourceUrl="https://tarkov.dev/api/";
    for(const auto& ref:refs)if(auto resolved=Resolve(ref.kind,ref.name)) {
        auto& ids=ref.kind==EntityKind::Item?evidence.itemIds:ref.kind==EntityKind::Task?evidence.taskIds:
            ref.kind==EntityKind::Map?evidence.mapIds:evidence.bossIds;
        if(std::ranges::find(ids,*resolved)==ids.end())ids.push_back(*resolved);
    }
    if(evidence.itemIds.empty() && evidence.taskIds.empty() && evidence.mapIds.empty() && evidence.bossIds.empty()
        && std::ranges::none_of(catalog.FindEvent(id)->sourceEvidence,[&](const auto& e){return e.evidenceId==evidence.evidenceId;})){error.clear();return true;}
    return catalog.AttachEvidence(id,std::move(evidence),error);
}
bool TarkovDevEventEnricher::EnrichText(EventCatalog& catalog,std::string_view id,std::string& error) const {
    const auto* e=catalog.FindEvent(id);if(!e){error="enrichment event missing";return false;}
    auto text=Normalize(e->summary);std::vector<EntityReference> refs;
    for(const auto& evidence:e->sourceEvidence)if(evidence.sourceKind==SourceKind::CommunityWiki)
        text+='\n'+Normalize(evidence.summary);
    for(const auto& [key,ids]:aliases_)if(ids.size()==1 && Mention(text,std::string_view(key).substr(2))) {
        refs.push_back({static_cast<EntityKind>(key[0]-'0'),key.substr(2)});
        if(refs.size()>256){error="entity reference capacity exceeded";return false;}
    }
    std::ranges::sort(refs,[](const auto& a,const auto& b){return std::tie(a.kind,a.name)<std::tie(b.kind,b.name);});
    return EnrichReferences(catalog,id,refs,error);
}
}
