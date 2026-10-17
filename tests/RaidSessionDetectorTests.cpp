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
    Require(!restarted.ActiveSession() && !restarted.Snapshot().active
        && restarted.CompletedSessions().size()==3 && restarted.CompletedSessions().back().sourceInterrupted
        && !restarted.CompletedSessions().back().endObserved, "source boundary retains incomplete history without claiming completion");
    RaidSessionDetector absent;
    Feed(absent, "GameStarted", 400); Feed(absent, "/client/match/local/end");
    const auto first = absent.CompletedSessions()[0].localSessionId;
    Feed(absent, "GameStarted", 400); Feed(absent, "/client/match/local/end");
    Require(absent.CompletedSessions().size() == 1 && absent.FindSession(first), "missing raid ID replay uses start provenance");
    RaidSessionDetector delayed;
    Feed(delayed,"2026-01-01 13:00:00.000|GameStarted",500);
    Feed(delayed,"2026-01-01 12:00:00.000|FinishScavSession");
    Require(delayed.ActiveSession()->raidType==RaidType::Unknown,"old settlement cannot type a newer raid");
    RaidSessionDetector remote;
    Feed(remote,"scene preset path:maps/factory_day_preset.bundle");
    Feed(remote,"[Transit] RaidId:remote-one, Locations:Unknown -> ");
    Feed(remote,"2026-01-01 14:00:00.000|GameStarted",600);
    Require(remote.ActiveSession()->mapId==NormalizeMap("factory4_day"),"identity without map preserves prepared scene");
    const auto remoteId=remote.ActiveSession()->localSessionId;
    Feed(remote,"scene preset path:maps/factory_day_preset.bundle");
    Feed(remote,"2026-01-01 14:00:00.000|GameStarted",600);
    Require(remote.ActiveSession()->localSessionId==remoteId&&remote.CompletedSessions().empty(),
        "duplicate prepared start does not archive or replace the active identity");
    Feed(remote,"2026-01-01 14:01:00.000|Info|application|BEClient exit successfully",700);
    Require(!remote.ActiveSession()&&remote.CompletedSessions().size()==1
        &&remote.CompletedSessions()[0].localSessionId==remoteId&&remote.CompletedSessions()[0].sourceInterrupted
        &&!remote.CompletedSessions()[0].endObserved&&!remote.CompletedSessions()[0].endedAt
        &&!remote.CompletedSessions()[0].duration&&remote.CompletedSessions()[0].outcome==RaidOutcome::Unknown,
        "network context teardown retains raid without inventing end time or outcome");
    Feed(remote,"2026-01-01 14:01:00.001|Info|application|BEClient exit successfully",701);
    Require(remote.CompletedSessions().size()==1,"duplicate teardown does not duplicate history");
    Feed(remote,"scene preset path:maps/laboratory_preset.bundle");
    Feed(remote,"2026-01-01 14:10:00.000|GameStarted",800);
    const auto replacedId=remote.ActiveSession()->localSessionId;
    Feed(remote,"scene preset path:maps/icebreaker.bundle");
    Feed(remote,"2026-01-01 14:20:00.000|GameStarted",900);
    Require(remote.CompletedSessions().size()==2&&remote.CompletedSessions().back().localSessionId==replacedId
        &&remote.CompletedSessions().back().mapId==NormalizeMap("laboratory")
        &&remote.ActiveSession()->mapId==NormalizeMap("Icebreaker"),"next raid cannot overwrite missing-end predecessor");
    Feed(remote,"2026-01-01 14:30:00.000|GameStarted",1000);
    Require(remote.CompletedSessions().size()==3,"distinct timestamped start retains predecessor even without map preset");
    std::cout << "Raid lifecycle synthetic contracts PASS\n";
}
