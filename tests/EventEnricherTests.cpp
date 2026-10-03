#include "events/TarkovDevEventEnricher.h"
#include "data/ItemCatalog.h"
#include "data/TaskCatalog.h"
#include "data/MapCatalog.h"
#include <iostream>
#include <stdexcept>
using namespace noven::events;
void Check(bool ok){if(!ok)throw std::runtime_error("event enrichment assertion");}
int main(int argc,char** argv){try{
    std::vector<EntityAlias> aliases{{EntityKind::Item,"item1","Insurance case"},{EntityKind::Item,"item2","Shared name"},
        {EntityKind::Item,"item3","Shared name"},{EntityKind::Map,"reserve","Reserve"},{EntityKind::Task,"task1","Explicit task"}};
    TarkovDevEventEnricher enrich(aliases);Check(!enrich.Resolve(EntityKind::Item,"Shared name"));
    Check(!enrich.Resolve(EntityKind::Map,"Reserved"));Check(!enrich.Resolve(EntityKind::Item,"Insuranc case"));
    Check(enrich.Resolve(EntityKind::Map,"RESERVE")=="reserve");
    OfficialAnnouncement a;a.sourceRecordId="30";a.sourceUrl="https://t.me/escapefromtarkovEN/30";a.title="An in-game event has started";
    a.summary="Insurance case at Reserve. Explicit task. Shared name.";EventCatalog c;std::string error;
    Check(c.Apply({&a,1},error));Check(enrich.EnrichText(c,"official-telegram:30",error));
    Check(c.Events()[0].itemIds==std::vector<std::string>{"item1"});Check(c.Events()[0].mapIds==std::vector<std::string>{"reserve"});
    Check(c.Events()[0].taskIds==std::vector<std::string>{"task1"});Check(c.Events()[0].sourceStatus==EventStatus::Unknown);
    Check(enrich.EnrichText(c,"official-telegram:30",error));Check(c.Events()[0].sourceEvidence.size()==2);
    a.summary="Unresolved new announcement content";Check(c.Apply({&a,1},error));
    Check(enrich.EnrichText(c,"official-telegram:30",error));Check(c.Events()[0].itemIds.empty() && c.Events()[0].mapIds.empty());
    Check(argc==2);const std::filesystem::path assets=argv[1];std::wstring loadError;
    noven::data::ItemCatalog items;noven::data::TaskCatalog tasks;noven::data::MapCatalog maps;
    Check(items.Load(assets/"data"/"items_catalog.tsv",loadError));Check(tasks.Load(assets/"data",loadError));Check(maps.Load(assets/"data",loadError));
    TarkovDevEventEnricher real(items,tasks,maps);
    const auto* reserve=std::ranges::find(maps.Maps(),std::string("reserve"),&noven::data::MapRecord::normalizedName)==maps.Maps().end()?nullptr:
        &*std::ranges::find(maps.Maps(),std::string("reserve"),&noven::data::MapRecord::normalizedName);
    Check(reserve && real.Resolve(EntityKind::Map,reserve->nameEn)==reserve->id);
    std::cout<<"Exact entity enrichment PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
