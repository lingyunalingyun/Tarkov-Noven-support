#include "data/PriceHistory.h"
#include <winrt/base.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <atomic>
#include <future>

void Require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    using namespace noven::data;
    winrt::init_apartment();
    std::vector<HistoryPoint> points;
    Require(ParseHistory(R"({"data":[{"price":200,"timestamp":2000,"priceMin":180},
        {"timestamp":1000,"extra":true,"price":100},{"price":210,"timestamp":2000},
        {"price":0,"timestamp":3000},{"price":-1,"timestamp":4000}]})", points), "order-independent parse");
    Require(points.size() == 2 && points[0].timeMs == 1000 && points[1].price == 210,
        "ascending timestamps, duplicate replaced, invalid prices skipped");
    Require(HistoryExtrema(points) == std::pair<std::int64_t,std::int64_t>{100,210}, "sample extrema");
    Require(!ParseHistory("{broken", points) && points.empty(), "malformed history fails safely");
    Require(ParseHistory(R"({"data":[]})", points) && !HistoryExtrema(points), "empty is unknown, not zero");
    Require(!ParseHistory(R"({"data":[{"price":2}]})", points), "missing timestamp rejected");
    Require(!ParseHistory(R"({"schemaVersion":2,"data":[]})", points), "unsupported cache schema rejected");
    Require(ValidHistoryId("59faff1d86f7746c51718c9c") && !ValidHistoryId("../anything"),
        "stable ID prevents cache path injection");
    const std::int64_t now = 1800000000000;
    points = {{now - 90*kHistoryDayMs, 90}, {now - 31*kHistoryDayMs, 31},
        {now - 30*kHistoryDayMs, 30}, {now - 7*kHistoryDayMs, 7},
        {now, 1}, {now + 1, 999}};
    Require(HistoryRange(points, now, 30).size() == 3, "30-day inclusive retention excludes future");
    Require(HistoryRange(points, now, 7).size() == 2, "7-day view");
    Require(HistoryRange(points, now, 90).size() == 5, "older data available in memory");
    const auto directory = std::filesystem::temp_directory_path()
        / ("NovenHistoryTests-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directory(directory);
    const auto file = directory / "history.json";
    Require(!LoadHistory(file, points), "missing file is cache miss");
    points = {{now-31*kHistoryDayMs,500},{now-30*kHistoryDayMs,200},{now,100}};
    Require(SaveHistory(file, points, now), "atomic write");
    std::vector<HistoryPoint> loaded;
    Require(LoadHistory(file, loaded) && loaded.size() == 2 && loaded.front().price == 200,
        "older query samples never reach disk");
    Require(!std::filesystem::exists(directory / "history.json.tmp"), "atomic temp consumed");
    Require(SaveHistory(file, loaded, now+31*kHistoryDayMs) && LoadHistory(file, loaded)
        && loaded.empty(), "rolling cleanup expires old samples");
    for (auto mode : AllGameModes())
        Require(std::string(UpstreamGameModeCode(mode)) != "", "all mode paths supported");
    std::filesystem::remove(file);
    const auto liveNow = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string id = "59faff1d86f7746c51718c9c";
    const auto payload = [&](GameMode mode) {
        return "{\"data\":[{\"timestamp\":" + std::to_string(liveNow - 40*kHistoryDayMs)
            + ",\"price\":500},{\"timestamp\":" + std::to_string(liveNow)
            + ",\"price\":" + std::to_string(mode == GameMode::Pvp ? 100 : 200) + "}]}";
    };
    const auto wait = [](PriceHistoryService& service) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            auto snapshot = service.TakeReady();
            if (snapshot && !snapshot->loading) return *snapshot;
            Sleep(5);
        }
        Require(false, "background result deadline");
        return HistorySnapshot{};
    };
    std::atomic<int> downloads{};
    {
        PriceHistoryService service([&](const auto&, auto mode, auto& json) {
            ++downloads; json = payload(mode); return true;
        });
        service.Start(nullptr, directory);
        service.Request(id, GameMode::Pvp, 90);
        auto result = wait(service);
        Require(result.points.size() == 2 && !result.failed, "90-day network history available in memory");
        Require(LoadHistory(directory / ("regular-" + id + ".json"), loaded)
            && loaded.size() == 1 && loaded[0].price == 100, "worker persists only latest 30 days");
        service.Request(id, GameMode::Pvp, 7);
        result = wait(service);
        Require(result.points.size() == 1 && downloads == 1, "range switch reuses memory without network");
        service.Request(id, GameMode::Pve, 30);
        result = wait(service);
        Require(result.mode == GameMode::Pve && result.points[0].price == 200 && downloads == 2,
            "modes never reuse each other's history");
        service.Request(id, GameMode::Seasonal, 30);
        result = wait(service);
        Require(result.mode == GameMode::Seasonal
            && std::filesystem::exists(directory / ("pvp-season-" + id + ".json")), "Seasonal cache isolated");
        service.Stop();
    }
    {
        PriceHistoryService offline([](const auto&, auto, auto&) { return false; });
        offline.Start(nullptr, directory);
        offline.Request(id, GameMode::Pvp, 30);
        const auto result = wait(offline);
        Require(result.failed && result.cached && result.points.size() == 1, "offline uses recent cache");
        offline.Stop();
    }
    {
        std::promise<void> started, release;
        auto released = release.get_future().share();
        PriceHistoryService service([&](const auto&, auto mode, auto& json) {
            if (mode == GameMode::Pvp) { started.set_value(); released.wait(); }
            json = payload(mode); return true;
        });
        service.Start(nullptr, directory);
        service.Request(id, GameMode::Pvp, 30);
        Require(started.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "first request started");
        service.Request(id, GameMode::Pve, 90);
        release.set_value();
        const auto result = wait(service);
        Require(result.mode == GameMode::Pve && result.days == 90, "stale reply cannot replace latest request");
        service.Stop();
    }
    for (auto mode : AllGameModes())
        std::filesystem::remove(directory / (std::string(UpstreamGameModeCode(mode)) + "-" + id + ".json"));
    std::filesystem::remove(directory);
    winrt::uninit_apartment();
    std::cout << "Price history tests passed\n";
}
