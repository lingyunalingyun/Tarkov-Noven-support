#include "events/TarkovChangesEnricher.h"
#include "events/OfficialEventSource.h"
#include <iostream>
#include <stdexcept>
using namespace noven::events;
void Check(bool ok){if(!ok)throw std::runtime_error("change boundary assertion");}
int main(int argc,char** argv){try{
    WinHttpEventClient http;TarkovChangesEnricher changes(http);ChangeRecord record;std::string error;
    if(argc==3 && std::string_view(argv[1])=="--live") {
        if(!changes.Fetch(argv[2],record,error))throw std::runtime_error(error);
        std::cout<<"Live change evidence normalized="<<record.changes.size()<<'\n';return 0;
    }
    HttpResponse r;r.status=200;r.contentType="text/html";
    r.body="<meta property=\"og:site_name\" content=\"Tarkov Silent Changes\"><h2>Dated: Friday, 02 October 2026 - 06:09 AM EDT | Game Version 1.1</h2>"
        "<h3>client/globals/response.json</h3><div class=\"diff-line\">['data']</div><div class=\"diff-line\">    ['config']</div>"
        "<div class=\"diff-line\">        ['SkillProgressRate']</div><div class=\"diff-line diff-removed\">- (Old) 0.4</div>"
        "<div class=\"diff-line diff-added\">+ (New) 0.65 (+62.50%)</div>";
    Check(changes.Parse(r,"1",record,error));Check(record.changes.size()==1 && record.changes[0].newValue=="0.65");
    Check(record.publishedAt==ParseTimestamp("2026-10-02T10:09:00Z"));EventCatalog c;
    Check(!changes.Attach(c,"official-telegram:1",record,error));Check(c.Events().empty());
    OfficialAnnouncement a;a.sourceRecordId="1";a.sourceUrl="https://t.me/escapefromtarkovEN/1";
    a.title="An in-game event has started";a.summary=a.title;a.publishedAt=record.publishedAt;
    Check(c.Apply({&a,1},error));Check(!changes.Attach(c,"official-telegram:1",record,error));
    a.linkedChangeRecordIds={"1"};Check(c.Apply({&a,1},error));Check(changes.Attach(c,"official-telegram:1",record,error));
    Check(c.Events().size()==1 && c.Events()[0].sourceStatus==EventStatus::Unknown && !c.Events()[0].startsAt);
    Check(changes.Attach(c,"official-telegram:1",record,error));Check(c.Events()[0].sourceEvidence.size()==2);
    r.body="<html>arbitrary diff</html>";Check(!changes.Parse(r,"1",record,error));
    Check(!changes.Fetch("../../escape",record,error));std::cout<<"Changes authority boundary PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
