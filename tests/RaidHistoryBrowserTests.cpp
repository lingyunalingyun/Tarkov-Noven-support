#include "raid/RaidHistoryBrowser.h"
#include <cstdlib>
#include <iostream>
using namespace noven::raid;
void Require(bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; std::exit(1); } }
int main() {
    constexpr std::int64_t day = 86400000, today = 100 * day;
    RaidSession a; a.localSessionId="a"; a.eftRaidId="eft-A"; a.mapId="reserve";
    a.startObserved=a.endObserved=true; a.startedAt=today; a.duration=60000;
    a.gameMode=GameMode::PvE; a.raidType=RaidType::Scav;
    auto b=a; b.localSessionId="b"; b.mapId="woods"; b.startedAt=today-1;
    b.gameMode=GameMode::PvP; b.raidType=RaidType::PMC;
    auto c=a; c.localSessionId="c"; c.startedAt=today-7*day; c.duration.reset();
    c.gameMode=GameMode::Unknown; c.raidType=RaidType::Unknown;
    RaidHistoryBrowser browser; browser.SetMapNames({{"reserve","储备站 Reserve"},{"woods","森林 Woods"}});
    browser.SetSessions({a,b,c}); Require(browser.Rows().size()==3 && browser.Summary().averageDuration==60000,"completed population and known duration average");
    HistoryFilter f;
    for (auto type:{RaidType::PMC,RaidType::Scav,RaidType::Unknown}) { f.type=type; browser.SetFilter(f); Require(browser.Rows().size()==1,"role filtering"); }
    f={}; for (auto mode:{GameMode::PvP,GameMode::PvE,GameMode::Unknown}) { f.mode=mode; browser.SetFilter(f); Require(browser.Rows().size()==1,"mode filtering"); }
    f={}; f.mapId="woods"; browser.SetFilter(f); Require(browser.Rows().size()==1,"map filtering");
    f={}; f.localMidnight=today; f.date=HistoryDate::Today; browser.SetFilter(f); Require(browser.Rows().size()==1,"local midnight inclusive and previous millisecond excluded");
    f.date=HistoryDate::SevenDays; browser.SetFilter(f); Require(browser.Rows().size()==2,"seven calendar days");
    f.date=HistoryDate::ThirtyDays; browser.SetFilter(f); Require(browser.Rows().size()==3,"thirty days");
    for (const auto* query:{"储备站","RESERVE","eft-A","c"}) { f={}; f.search=query; browser.SetFilter(f); Require(!browser.Rows().empty(),"localized map/id case-insensitive search"); }
    Require(browser.Find("c")->outcome==RaidOutcome::Unknown,"unknown outcome not inferred");
    f.search="no-match"; browser.SetFilter(f); Require(browser.Rows().empty(),"filter empty state");
    browser.SetSessions({}); Require(browser.Sessions().empty(),"no fake history");
    std::vector<RaidSession> many(5000,a); browser.SetSessions(std::move(many)); f={}; browser.SetFilter(f);
    const auto builds=browser.Builds();
    for (int i=0;i<10000;++i) { browser.SetFilter(f); Require(browser.Rows().size()==5000,"large snapshot"); }
    Require(browser.Builds()==builds,"idle reads and unchanged filters never rebuild");
    std::cout << "Raid history browser contracts PASS\n";
}
