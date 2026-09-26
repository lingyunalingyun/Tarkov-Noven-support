#include "data/RecentScanStore.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

noven::data::RecentScanEntry Entry(std::uint64_t id, noven::data::GameMode mode) {
    noven::data::RecentScanEntry entry;
    entry.scanId = id;
    entry.stableItemId = "66b5f22b78bbc0200425f904";
    entry.canonicalName = "Camelbak Tri-Zip 突击背包（复合迷彩）";
    entry.canonicalShortName = "Tri-Zip";
    entry.gameMode = mode;
    entry.scannedAtUnixMs = 1'779'999'999'000;
    entry.fleaPrice = 70'000;
    entry.bestTraderPrice = 64'800;
    entry.bestTraderName = "Ragman";
    entry.valuePerSlot = 2'333.5;
    entry.fleaStatus = noven::data::FleaStatus::Allowed;
    entry.itemWidth = 5;
    entry.itemHeight = 6;
    return entry;
}
} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path()
        / ("noven-recent-tests-" + std::to_string(GetCurrentProcessId())
            + "-" + std::to_string(GetTickCount64()));
    Require(directory != std::filesystem::temp_directory_path(), "isolated temp directory");
    std::filesystem::create_directories(directory);
    const auto path = directory / "recent-scans.json";
    std::wstring error;
    {
        noven::data::RecentScanStore store;
        Require(store.Load(path, error), "missing file loads as empty history");
        Require(store.Snapshot().empty(), "empty store snapshot");
        auto first = Entry(1, noven::data::GameMode::Pvp);
        Require(store.Append(first), "append resolved item");
        first.fleaPrice = 75'000;
        Require(store.Snapshot().front().fleaPrice == 70'000,
                "history keeps scan-time price snapshot");
        Require(!store.Append(Entry(1, noven::data::GameMode::Pvp)),
                "duplicate scan ID is rejected");
        auto second = Entry(2, noven::data::GameMode::Pve);
        second.matchMode = noven::data::RecentMatchMode::BestEffort;
        second.ambiguous = true;
        second.fleaPrice.reset();
        second.bestTraderPrice.reset();
        second.bestTraderName.clear();
        Require(store.Append(second), "append ambiguous best-effort item");
        Require(store.Snapshot().front().scanId == 2, "newest item first");
        Require(store.Snapshot().front().gameMode == noven::data::GameMode::Pve,
                "PvE mode is snapshot data");
        Require(!store.Snapshot().front().fleaPrice
                && !store.Snapshot().front().bestTraderPrice,
                "Unknown prices remain optional, not zero");
        Require(store.Append(Entry(3, noven::data::GameMode::Seasonal)),
                "Seasonal item appended");
    }
    {
        noven::data::RecentScanStore store;
        Require(store.Load(path, error), "UTF-8 history loads after restart");
        const auto entries = store.Snapshot();
        Require(entries.size() == 3 && entries[0].scanId == 3,
                "save/load preserves newest-first ordering");
        Require(entries[0].gameMode == noven::data::GameMode::Seasonal
            && entries[1].gameMode == noven::data::GameMode::Pve
            && entries[2].gameMode == noven::data::GameMode::Pvp,
            "three game modes survive persistence");
        Require(entries[1].matchMode == noven::data::RecentMatchMode::BestEffort
            && entries[1].ambiguous
            && entries[2].matchMode == noven::data::RecentMatchMode::Strict,
            "strict, best-effort and ambiguity metadata survive");
        Require(entries[2].canonicalName == "Camelbak Tri-Zip 突击背包（复合迷彩）"
            && entries[2].canonicalShortName == "Tri-Zip"
            && entries[2].stableItemId == "66b5f22b78bbc0200425f904",
            "Chinese canonical snapshot and stable ID survive UTF-8 round trip");
        Require(entries[1].fleaPrice == std::nullopt
            && entries[1].bestTraderPrice == std::nullopt,
            "Unknown prices survive round trip");
        Require(store.MaxScanId() == 3, "persisted maximum supports restart-safe scan IDs");
        for (std::uint64_t id = 4; id <= 204; ++id)
            Require(store.Append(Entry(id, noven::data::GameMode::Pvp)),
                    "bounded history append");
        const auto bounded = store.Snapshot();
        Require(bounded.size() == 200 && bounded.front().scanId == 204
            && bounded.back().scanId == 5, "entry 201 drops oldest entry");
        Require(!store.Append(Entry(1, noven::data::GameMode::Pvp)),
                "evicted old scan ID cannot be inserted again");
    }
    {
        noven::data::RecentScanStore store;
        Require(store.Load(path, error) && store.Snapshot().size() == 200,
                "bounded history persists after restart");
    }
    const auto damaged = directory / "damaged.json";
    {
        std::ofstream output(damaged, std::ios::binary);
        output << "{broken";
    }
    {
        noven::data::RecentScanStore store;
        Require(!store.Load(damaged, error) && store.Snapshot().empty(),
                "malformed JSON yields empty in-memory history");
    }
    {
        std::ifstream input(damaged, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
        Require(text == "{broken", "malformed file is not overwritten without an append");
    }
    {
        noven::data::RecentScanStore store;
        Require(!store.Load(damaged, error), "malformed file remains loadable as warning");
        Require(store.Append(Entry(205, noven::data::GameMode::Pvp)),
                "new valid append can replace damaged history");
    }
    {
        noven::data::RecentScanStore store;
        Require(store.Load(damaged, error) && store.Snapshot().size() == 1,
                "new valid history replaces damaged file atomically");
    }
    Require(std::filesystem::weakly_canonical(directory).parent_path()
                == std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()),
            "cleanup target stays within the designated temp directory");
    std::filesystem::remove_all(directory);
    std::cout << "Recent scan store tests passed\n";
}
