#include "raid/RaidSessionDetector.h"
#include <cstdlib>
#include <iostream>
using namespace noven::raid;
void Require(bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; std::exit(1); } }
void Feed(RaidSessionDetector& d, std::string_view text, std::uint64_t offset = 0) {
    for (const auto& event : ParseRaidEvents(text, "synthetic-file", offset)) d.Consume(event);
}
int main() {
    RaidSessionDetector d;
    Feed(d, "Session mode: Pve");
    Feed(d, "[Transit] RaidId:one, Locations:RezervBase -> ");
    Require(d.Snapshot().state == SessionState::PreparingRaid && !d.ActiveSession(), "preparation only");
    Feed(d, "/client/match/local/end");
    Require(d.CompletedSessions().empty(), "cancelled loading is not a raid");
    Feed(d, "[Transit] RaidId:one, Locations:RezervBase -> ");
    Feed(d, "2026-01-01 12:00:00.000|GameStarted", 100);
    Require(d.ActiveSession() && d.ActiveSession()->raidType == RaidType::Unknown, "start activates without guessing PMC");
    const auto local = d.ActiveSession()->localSessionId;
    Feed(d, "GameStarted", 100);
    Require(d.ActiveSession()->localSessionId == local, "duplicate start does not replace active raid");
    Feed(d, "2026-01-01 12:01:00.000|/client/match/local/end");
    Feed(d, "2026-01-01 12:01:01.000|/client/match/local/end");
    Require(d.CompletedSessions().size() == 1 && d.CompletedSessions()[0].duration == 60000, "one completed session");
    Feed(d, "SellAsSavage"); Feed(d, "FinishScavSession"); Feed(d, "Quest failed");
    Require(d.CompletedSessions()[0].raidType == RaidType::Scav
        && d.CompletedSessions()[0].outcome == RaidOutcome::Unknown, "scav is not survival evidence");
    RaidSessionDetector restarted; restarted.Restore(d.Snapshot());
    Feed(restarted, "[Transit] RaidId:one, Locations:RezervBase -> ");
    Feed(restarted, "GameStarted", 100); Feed(restarted, "/client/match/local/end");
    Require(restarted.CompletedSessions().size() == 1, "replayed upstream identity deduplicates after restart");
    Feed(restarted, "[Transit] RaidId:two, Locations:Unknown -> ");
    Feed(restarted, "GameStarted", 200); Feed(restarted, "/client/match/local/end");
    Require(restarted.CompletedSessions().size() == 2 && restarted.CompletedSessions()[1].mapId.empty()
        && !restarted.CompletedSessions()[1].duration && restarted.CompletedSessions()[1].raidType == RaidType::Unknown,
        "multiple independent raids, missing time and unknown map explicit");
    Feed(restarted, "GameStarted", 300);
    Require(restarted.ActiveSession() && restarted.CompletedSessions().size() == 2, "EOF does not complete active");
    restarted.SourceBoundary();
    Require(!restarted.ActiveSession() && restarted.Snapshot().active->sourceInterrupted, "source boundary interrupts, never completes");
    RaidSessionDetector absent;
    Feed(absent, "GameStarted", 400); Feed(absent, "/client/match/local/end");
    const auto first = absent.CompletedSessions()[0].localSessionId;
    Feed(absent, "GameStarted", 400); Feed(absent, "/client/match/local/end");
    Require(absent.CompletedSessions().size() == 1 && absent.FindSession(first), "missing raid ID replay uses start provenance");
    std::cout << "Raid lifecycle synthetic contracts PASS\n";
}
