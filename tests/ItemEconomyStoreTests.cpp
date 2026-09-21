#include "data/GameMode.h"
#include "data/ItemEconomyStore.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

const noven::data::ItemEconomyInfo& RequireItem(
    const noven::data::ItemEconomyStore& store,
    noven::data::GameMode mode,
    std::string_view id
) {
    const auto* item = store.Lookup(mode, id);
    Require(item != nullptr, "known item can be looked up");
    return *item;
}

std::string Payload(
    const std::string& id,
    const std::string& second_id,
    bool include_flea_market = true
) {
    return std::string("{\"data\":{")
        + (include_flea_market ? "\"fleaMarket\":{\"enabled\":true}," : "")
        + "\"items\":{"
        + "\"" + id + "\":{"
          "\"id\":\"" + id + "\",\"width\":2,\"height\":3,"
          "\"lastLowPrice\":100,\"types\":[],\"updated\":\"2026-09-17T12:00:00Z\","
          "\"sellFor\":["
          "{\"priceRUB\":250,\"source\":\"prapor\",\"vendor\":{\"trader_id\":\"trader-a\",\"name\":\"Prapor\"}},"
          "{\"priceRUB\":400,\"source\":\"therapist\",\"vendor\":{\"trader_id\":\"trader-b\",\"name\":\"Therapist\"}},"
          "{\"priceRUB\":999,\"source\":\"fleaMarket\",\"vendor\":{\"trader_id\":\"flea\"}}"
          "]},"
        + "\"" + second_id + "\":{"
          "\"id\":\"" + second_id + "\",\"width\":1,\"height\":2,"
          "\"lastLowPrice\":50,\"types\":[\"noFlea\"],"
          "\"sellFor\":[{\"priceRUB\":75,\"source\":\"prapor\",\"vendor\":{\"trader_id\":\"trader-a\",\"name\":\"Prapor\"}}]"
          "}"
        + "}}}";
}

} // namespace

int main() {
    constexpr char kItemA[] = "61bf7b6302b3924be92fa8c3";
    constexpr char kItemB[] = "5df8a2ca86f7740bfe6df777";
    constexpr char kItemC[] = "5447a9cd4bdc2dbd208b4567";

    noven::data::ItemEconomyStore store;
    std::wstring error;
    Require(store.ReplaceFromUpstreamJson(
        noven::data::GameMode::Pvp, Payload(kItemA, kItemB), error),
        "PVP payload loads");
    Require(store.ReplaceFromUpstreamJson(
        noven::data::GameMode::Pve, Payload(kItemC, kItemB), error),
        "PVE payload loads");
    Require(store.ReplaceFromUpstreamJson(
        noven::data::GameMode::Seasonal, Payload(kItemB, kItemC, false), error),
        "Seasonal payload loads");

    const auto& pvp_item = RequireItem(store, noven::data::GameMode::Pvp, kItemA);
    Require(store.Lookup(noven::data::GameMode::Pve, kItemA) == nullptr,
        "PVP and PVE caches are isolated");
    Require(store.Lookup(noven::data::GameMode::Seasonal, kItemA) == nullptr,
        "Seasonal cache is isolated");
    Require(pvp_item.width == 2 && pvp_item.height == 3,
        "item dimensions are loaded");
    Require(pvp_item.fleaPrice == 100, "flea price uses lastLowPrice");
    Require(pvp_item.bestTrader.has_value()
            && pvp_item.bestTrader->traderId == "trader-b"
            && pvp_item.bestTrader->priceRoubles == 400,
        "highest valid trader value is selected");
    Require(pvp_item.bestValue == 400, "best value chooses trader over flea");
    Require(pvp_item.valuePerSlot.has_value()
            && *pvp_item.valuePerSlot > 66.6 && *pvp_item.valuePerSlot < 66.7,
        "value per slot is calculated from dimensions");
    Require(pvp_item.fleaStatus == noven::data::FleaStatus::Allowed,
        "flea allowed state is preserved");

    const auto& banned_item = RequireItem(store, noven::data::GameMode::Pvp, kItemB);
    Require(banned_item.fleaStatus == noven::data::FleaStatus::Banned,
        "noFlea item is marked banned");
    const auto& unknown_flea = RequireItem(store, noven::data::GameMode::Seasonal, kItemB);
    Require(unknown_flea.fleaStatus == noven::data::FleaStatus::Unknown,
        "missing flea metadata remains unknown");
    Require(store.Lookup(noven::data::GameMode::Pvp, "000000000000000000000000") == nullptr,
        "missing item returns no result");

    const std::string invalid_id_payload =
        "{\"data\":{\"items\":{\"not-an-item\":{\"id\":\"not-an-item\"}}}}";
    Require(!store.ReplaceFromUpstreamJson(
        noven::data::GameMode::Pvp, invalid_id_payload, error),
        "malformed upstream payload is rejected");
    Require(store.Lookup(noven::data::GameMode::Pvp, kItemA) != nullptr,
        "failed refresh keeps the old cache");

    const auto cache_directory = std::filesystem::temp_directory_path()
        / "noven_economy_store_tests";
    std::error_code directory_error;
    std::filesystem::remove_all(cache_directory, directory_error);
    std::filesystem::create_directories(cache_directory, directory_error);
    Require(!directory_error, "test cache directory can be created");
    const auto cache_path = cache_directory / "regular.json";
    Require(store.SaveCacheFile(noven::data::GameMode::Pvp, cache_path, error),
        "valid cache saves atomically");
    Require(!std::filesystem::exists(cache_path.wstring() + L".tmp"),
        "temporary cache file is not left behind");

    noven::data::ItemEconomyStore loaded_store;
    Require(loaded_store.LoadCacheFile(
        noven::data::GameMode::Pvp, cache_path, error),
        "saved cache loads");
    Require(loaded_store.Lookup(noven::data::GameMode::Pvp, kItemA) != nullptr,
        "saved cache contains the known item");
    Require(!loaded_store.LoadCacheFile(
        noven::data::GameMode::Pve, cache_path, error),
        "cache with the wrong mode is rejected");

    {
        std::ofstream malformed(cache_path, std::ios::binary | std::ios::trunc);
        malformed << "not json";
    }
    Require(!loaded_store.LoadCacheFile(
        noven::data::GameMode::Pvp, cache_path, error),
        "malformed disk cache is rejected");
    Require(loaded_store.Lookup(noven::data::GameMode::Pvp, kItemA) != nullptr,
        "malformed disk cache does not replace memory");

    const auto lookup_start = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < 10'000; ++iteration) {
        static_cast<void>(store.Lookup(noven::data::GameMode::Pvp, kItemA));
    }
    const double lookup_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - lookup_start).count();
    std::cout << "Economy ID lookup average ms=" << lookup_ms / 10'000.0 << '\n';

    std::filesystem::remove_all(cache_directory, directory_error);
    Require(!directory_error, "test cache directory is removed");
    std::cout << "Item economy store tests passed\n";
    return 0;
}
