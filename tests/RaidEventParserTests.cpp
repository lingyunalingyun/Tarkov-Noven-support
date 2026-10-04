#include "raid/RaidEventParser.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
using namespace noven::raid;
void Require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv) {
    struct Map {const char* nameId; const char* preset; const char* id;};
    const Map maps[]{
        {"factory4_day", "factory_day_preset", "55f2d3fd4bdc2d5f408b4567"},
        {"bigmap", "customs_preset", "56f40101d2720b2a4d8b45d6"},
        {"Woods", "woods_preset", "5704e3c2d2720bac5b8b4567"},
        {"Lighthouse", "lighthouse_preset", "5704e4dad2720bb55b8b4567"},
        {"Shoreline", "shoreline_preset", "5704e554d2720bac5b8b456e"},
        {"RezervBase", "rezerv_base_preset", "5704e5fad2720bc05b8b4567"},
        {"Interchange", "shopping_mall", "5714dbc024597771384a510d"},
        {"TarkovStreets", "city_preset", "5714dc692459777137212e12"},
        {"factory4_night", "factory_night_preset", "59fc81d786f774390775787e"},
        {"laboratory", "laboratory_preset", "5b0fc42d86f7744a585f9105"},
        {"Sandbox", "sandbox_preset", "653e6760052c01c1c805532f"},
        {"Sandbox_high", "sandbox_high_preset", "65b8d6f5cdde2479cb2a3125"},
        {"Terminal", "terminal_preset", "65cc8f81a9aac3e77d0cfd3e"},
        {"Labyrinth", "labyrinth_preset", "6733700029c367a3d40b02af"},
        {"Sandbox_start", "sandbox_start_preset", "68236e8153654e8c1200798a"},
        {"Icebreaker", "icebreaker", "69af492a4819ea4ba10a69c5"},
        {"laboratory_dark", "laboratory_dark_preset", "6a294a5b5eb5f9a1700417b7"},
    };
    Require(argc == 2, "map catalog fixture path");
    std::ifstream catalog(argv[1]);
    Require(catalog.good(), "existing map catalog opens");
    const std::string catalogText((std::istreambuf_iterator<char>(catalog)), {});
    std::size_t rows{}; std::istringstream table(catalogText); std::string row;
    std::getline(table,row); while(std::getline(table,row)) if(!row.empty()) ++rows;
    Require(rows == std::size(maps), "all catalog maps have verified log identities");
    for (const auto& map : maps) {
        Require(catalogText.find(std::string(map.id)+"\t") != std::string::npos, "reuse stable catalog identity");
        Require(NormalizeMap(map.nameId) == map.id, "exact upstream location");
        const auto preset = std::string("maps/")+map.preset+".bundle";
        Require(NormalizeMap(preset) == map.id, "exact upstream scene path");
        for (const auto* gap : {"", " ", "\t"}) {
            const auto events=ParseRaidEvents(std::string("scene preset path:")+gap+preset);
            Require(events.size()==1&&events[0].mapId==map.id, "scene spacing contract");
        }
        const auto events=ParseRaidEvents(std::string("[Transit] RaidId:map-test, Locations:")+map.nameId+" -> Woods");
        Require(events.size()==1&&events[0].mapId==map.id, "use transit origin, not destination");
        Require(NormalizeMap(std::string(map.nameId)+"_unverified").empty(), "reject near matches");
    }
    // 最小合成片段保留已核对的事件格式，不包含用户日志或账户信息。
    // Minimal synthetic fragments retain verified event syntax, never user logs/account data.
    auto mode = ParseRaidEvents("2026-01-02 12:00:00.123|Info|application|Session mode: Pve", "synthetic", 42);
    Require(mode.size() == 1 && mode[0].mode == GameMode::PvE && mode[0].time
        && mode[0].offset == 42, "PvE/time/provenance");
    Require(ParseRaidEvents("Session mode: Pvp")[0].mode == GameMode::PvP, "PvP");
    Require(ParseRaidEvents("Session mode: Offline").empty(), "unverified mode not inferred");
    auto id = ParseRaidEvents("[Transit] Flag:Common, RaidId:synthetic-1, Count:0, Locations:RezervBase -> ");
    Require(id.size() == 1 && id[0].raidId == "synthetic-1"
        && id[0].mapId == "5704e5fad2720bc05b8b4567", "reserve stable catalog identity");
    Require(NormalizeMap("UnknownRezervBase").empty(), "no substring map guessing");
    Require(ParseRaidEvents("scene preset path: maps/rezerv_base_preset.bundle")[0].mapId == id[0].mapId, "preset");
    Require(ParseRaidEvents("GameStarted:91.23(0) real:125.71(0)")[0].kind == EventKind::RaidStarted, "start");
    Require(ParseRaidEvents("/client/match/local/end")[0].kind == EventKind::RaidEnded, "end");
    Require(ParseRaidEvents("Request https://example.invalid/client/match/local/end. Status: OK")[0].kind
        == EventKind::RaidEnded, "backend full URL path, host discarded");
    Require(ParseRaidEvents("https://example.invalid/client/game/profile/savage/regenerate, status: OK")[0].kind
        == EventKind::ScavEvidenceDetected, "backend scav URL");
    for (const auto* text : {"SellAsSavage", "FinishScavSession", "/client/game/profile/savage/regenerate"})
        Require(ParseRaidEvents(text)[0].kind == EventKind::ScavEvidenceDetected, "positive scav evidence only");
    for (const auto* text : {"NotGameStarted", "/client/match/local/endless", "Quest failed", "ExitStatus Survived", "[Transit] RaidId:??"})
        Require(ParseRaidEvents(text).empty(), "malformed/unrelated not events");
    Require(!ParseLogTime("2026-02-30 12:00:00.000"), "invalid calendar date");
    Require(!ParseLogTime("2026-01-02 25:00:00.000"), "invalid clock");
    std::cout << "Raid parser synthetic contracts PASS\n";
}
