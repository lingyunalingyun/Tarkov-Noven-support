#include "data/TaskBrowser.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {
void Require(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
}

int main(int argc,char** argv){
    Require(argc==2,"assets directory required");const auto assets=std::filesystem::path(argv[1]);std::wstring error;
    noven::data::TaskCatalog tasks;if(!tasks.Load(assets/"data",error)){std::wcerr<<error<<L'\n';Require(false,"task catalog loads");}
    Require(tasks.Tasks().size()==1027,"regular and PvE tasks load");
    Require(tasks.StructureMode(noven::data::GameMode::Seasonal)=="regular","Seasonal uses regular task structure");
    noven::data::ItemCatalog items;Require(items.Load(assets/"data"/"items_catalog.tsv",error),"item catalog loads");
    noven::data::TaskBrowser browser(tasks,items);
    const auto ordered=browser.Query("",noven::data::GameMode::Pvp,"en-US");
    Require(!ordered.empty()&&ordered.front().trader->id=="54cb50c76803fa8b248b4571",
        "Prapor is first in requested trader order");
    const auto english=browser.Query("Debut",noven::data::GameMode::Pvp,"en-US");
    Require(!english.empty()&&english.front().source->id=="5936d90786f7742b1420ba5b","English task search returns stable ID");
    const auto chinese=browser.Query("首秀",noven::data::GameMode::Pvp,"zh-CN");
    Require(!chinese.empty()&&chinese.front().source->id==english.front().source->id,"Chinese search preserves identity");
    Require(!browser.Query("Prapor",noven::data::GameMode::Pve,"en-US").empty(),"PvE trader search works");
    const auto rewardSearch=browser.Query("#Roubles",noven::data::GameMode::Pvp,"en-US");
    Require(std::any_of(rewardSearch.begin(),rewardSearch.end(),[](const auto& row){
        return row.source->id=="5936d90786f7742b1420ba5b";
    }),"reward item query returns tasks rewarding the item");
    Require(browser.Query("#",noven::data::GameMode::Pvp,"en-US").empty(),
        "empty reward query does not return every task");
    const auto* kappa=browser.Task(noven::data::GameMode::Pvp,"59675ea386f77414b32bded2");
    const auto* lightkeeper=browser.Task(noven::data::GameMode::Pvp,"625d6ff5ddc94657c21a1625");
    Require(kappa&&kappa->kappaRequired&&!kappa->lightkeeperRequired,"Kappa marker data loads");
    Require(lightkeeper&&!lightkeeper->kappaRequired&&lightkeeper->lightkeeperRequired,
        "Lightkeeper marker data loads");
    return 0;
}
