#include "raid/RaidEventParser.h"
#include <cstdlib>
#include <iostream>
using namespace noven::raid;
void Require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
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
    for (const auto* text : {"SellAsSavage", "FinishScavSession", "/client/game/profile/savage/regenerate"})
        Require(ParseRaidEvents(text)[0].kind == EventKind::ScavEvidenceDetected, "positive scav evidence only");
    for (const auto* text : {"NotGameStarted", "/client/match/local/endless", "Quest failed", "ExitStatus Survived", "[Transit] RaidId:??"})
        Require(ParseRaidEvents(text).empty(), "malformed/unrelated not events");
    Require(!ParseLogTime("2026-02-30 12:00:00.000"), "invalid calendar date");
    Require(!ParseLogTime("2026-01-02 25:00:00.000"), "invalid clock");
    std::cout << "Raid parser synthetic contracts PASS\n";
}
