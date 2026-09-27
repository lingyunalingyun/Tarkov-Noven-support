#include "data/PriceBrowser.h"

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

int main(int argc, char** argv) {
    Require(argc == 2, "catalog path is provided");
    noven::data::ItemCatalog catalog;
    std::wstring error;
    Require(catalog.Load(argv[1], error), "production catalog loads");
    noven::data::ItemEconomyStore economy;
    const std::string payload =
        R"({"data":{"fleaMarket":{"enabled":true},"items":{
        "5447a9cd4bdc2dbd208b4567":{"id":"5447a9cd4bdc2dbd208b4567","lastLowPrice":80000,"width":1,"height":1,"sellToTrader":[{"trader":{"id":"a"},"price":1000}]}}
        }})";
    Require(economy.ReplaceFromUpstreamJson(noven::data::GameMode::Pvp, payload, error),
        "economy snapshot loads");
    noven::data::PriceBrowserModel browser(catalog, economy);
    const std::vector<noven::data::PriceTagAlias> tags{
        {"容器", "container"}, {"子弹", "ammo"}, {"武器", "gun"},
        {"装备", "wearable"}, {"Ammo pack", "ammoBox"}};
    const auto queryTags = [&](std::string_view query) {
        return browser.Query(query, noven::data::GameMode::Pvp,
            noven::data::PriceSortMode::FleaPrice, true, noven::data::PriceTraderSide::Sell, 120, tags);
    };
    Require(!queryTags("#容器").empty() && !queryTags("#子弹").empty(), "Chinese tags match upstream types");
    Require(!queryTags("M855 #子弹").empty() && !queryTags("#子弹 M855").empty(),
        "name and tag work in either order");
    Require(!queryTags("#AMMO").empty() && !queryTags("#\"Ammo pack\"").empty(),
        "raw English types ignore case and quoted labels support spaces");
    Require(!queryTags("#武器 #装备").empty() && queryTags("#容器 #子弹").empty(),
        "multiple tags intersect");
    Require(queryTags("#不存在").empty(), "unknown tag never falls back to unfiltered results");
    Require(queryTags("#").size() == queryTags("").size(), "unfinished hash keeps results while typing");
    Require(queryTags("#子弹#子弹").size() == queryTags("#子弹").size(), "adjacent repeated tags are stable");
    const auto tagged = queryTags("#容器");
    for (const auto& row : tagged)
        Require(std::find(row.item->types.begin(), row.item->types.end(), "container") != row.item->types.end(),
            "tag filtering never leaks nonmatching stable IDs");
    const auto nl = browser.Query("NL545", noven::data::GameMode::Pvp);
    Require(!nl.empty() && nl.front().item != nullptr, "English search returns a stable item");
    const auto chinese = browser.Query("垃圾箱", noven::data::GameMode::Pvp);
    Require(!chinese.empty(), "Chinese substring search returns an item");
    const auto empty = browser.Query({}, noven::data::GameMode::Pvp);
    Require(!empty.empty(), "empty query has deterministic catalog results");
    const auto exact = browser.Query("colt m4a1", noven::data::GameMode::Pvp);
    Require(!exact.empty() && exact.front().economy.has_value(),
        "exact search assembles economy by stable ID");
    Require(exact.front().economy->fleaPrice == 80000, "economy snapshot is exposed unchanged");
    Require(!exact.front().economy->fleaChangeAmount, "missing change is Unknown, not zero");
    noven::data::ItemEconomyStore changes;
    noven::data::PriceBrowserModel changeBrowser(catalog, changes);
    const std::string changePayload = R"({"data":{"items":{
        "5447a9cd4bdc2dbd208b4567":{"id":"5447a9cd4bdc2dbd208b4567","changeLast48h":-100.5,"changeLast48hPercent":99},
        "5447ac644bdc2d6c208b4567":{"id":"5447ac644bdc2d6c208b4567","changeLast48h":0},
        "5448ba0b4bdc2d02308b456c":{"id":"5448ba0b4bdc2d02308b456c","changeLast48h":500,"changeLast48hPercent":-99}
    }}})";
    for (auto mode : noven::data::AllGameModes()) {
        Require(changes.ReplaceFromUpstreamJson(mode, changePayload, error), "mode change data parses");
        const auto ascending = changeBrowser.Query({}, mode, noven::data::PriceSortMode::FleaChange, false);
        const auto descending = changeBrowser.Query({}, mode, noven::data::PriceSortMode::FleaChange, true);
        Require(ascending[0].economy->fleaChangeAmount == -100.5
            && ascending[1].economy->fleaChangeAmount == 0
            && ascending[2].economy->fleaChangeAmount == 500,
            "ascending sorts signed amounts including real zero, never percentages");
        Require(descending[0].economy->fleaChangeAmount == 500
            && descending[2].economy->fleaChangeAmount == -100.5
            && !descending[3].economy, "descending keeps Unknown last");
        const auto cache = std::filesystem::temp_directory_path() /
            ("NovenChangeTest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        Require(changes.SaveCacheFile(mode, cache, error), "change cache saves");
        noven::data::ItemEconomyStore restored;
        Require(restored.LoadCacheFile(mode, cache, error), "change cache loads");
        Require(restored.Lookup(mode, "5447a9cd4bdc2dbd208b4567")->fleaChangeAmount == -100.5
            && restored.Lookup(mode, "5447ac644bdc2d6c208b4567")->fleaChangeAmount == 0,
            "cache round trip preserves signed fractional amount and zero");
        std::filesystem::remove(cache);
    }
    std::cout << "Price browser tests passed\n";
}
