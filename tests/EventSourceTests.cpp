#include "events/OfficialEventSource.h"
#include <iostream>
#include <stdexcept>
using namespace noven::events;
void Check(bool ok){if(!ok)throw std::runtime_error("official event source assertion");}
std::string Page(std::string text,std::string id="100") {
    return "<meta property=\"og:title\" content=\"Escape from Tarkov Official\"><meta content=\"tg://resolve?domain=escapefromtarkovEN\">"
        "<div data-post=\"escapefromtarkovEN/"+id+"\"><div class=\"tgme_widget_message_text js-message_text\">"+text+
        "</div><time datetime=\"2026-10-04T00:00:00+03:00\"></time></div>";
}
int main(int argc,char** argv){try{
    WinHttpEventClient http;
    if(argc>1 && std::string_view(argv[1])=="--live") {
        OfficialEventSource source(http);auto result=source.Fetch({},{});
        Check(result.success);EventCatalog c;std::string error;Check(c.Apply(result.announcements,error));
        std::cout<<"Live official normalized="<<c.Events().size()<<" cursor="<<result.state.newestMessageId<<'\n';
        for(const auto& e:c.Events())std::cout<<e.eventId<<" | "<<e.title<<" | status="<<static_cast<int>(e.sourceStatus)<<'\n';return 0;
    }
    HttpResponse r;r.status=200;r.contentType="text/html; charset=utf-8";
    // 官方 6928 的措辞回归；合成正文避免复制整篇第三方公告。
    // Reproduce official post 6928's wording using a synthetic body, not the full announcement.
    r.body=Page("The in-game event “Scourge Incursion” is live in <a href=\"https://t.me/escapefromtarkovEN\">#EscapefromTarkov</a>.<br/>Synthetic event details.<br/>The event will run until October 20, 8:00 AM BST / 3:00 AM EDT.","6928");
    auto named=OfficialEventSource::Parse(r);Check(named.success && named.announcements.size()==1);
    Check(named.announcements[0].title=="Scourge Incursion" && !named.announcements[0].titleIsExcerpt);
    Check(named.announcements[0].status==EventStatus::Active && named.announcements[0].startsAtFromPublication);
    Check(named.announcements[0].startsAt==named.announcements[0].publishedAt);
    Check(!named.announcements[0].endsAt && named.announcements[0].summary.find("October 20")!=std::string::npos);
    EventCatalog namedCatalog;std::string namedError;Check(namedCatalog.Apply(named.announcements,namedError));
    Check(namedCatalog.Apply(named.announcements,namedError) && namedCatalog.Events().size()==1);
    for(auto text:{"The in-game event &quot;Test Event&quot; is live in #EscapefromTarkov.",
                   "The in-game event “Test Event” is live in PvE mode."}) {
        r.body=Page(text);auto value=OfficialEventSource::Parse(r);
        Check(value.success && value.announcements.size()==1 && value.announcements[0].title=="Test Event");
    }
    r.body=Page("The in-game event “Test Event” is live in PvE mode.<br/>The event will run until 2026-10-20T08:00:00Z.");
    named=OfficialEventSource::Parse(r);Check(named.announcements.size()==1);
    Check(named.announcements[0].endsAt==ParseTimestamp("2026-10-20T08:00:00Z"));
    Check(named.announcements[0].modes==std::vector<EventMode>{EventMode::PvE});
    for(auto text:{"The in-game event “Test Event” is live in #TarkovArena.",
                   "The in-game event “Test. Event” is live in #TarkovArena.",
                   "The in-game event “Test Event” is not live in #EscapefromTarkov.",
                   "The in-game event “Test Event” will be live in #EscapefromTarkov.",
                   "The in-game event “Test Event is live in #EscapefromTarkov.",
                   "The in-game event “” is live in #EscapefromTarkov.",
                   "The in-game event “Test Event” is live in a trailer.",
                   "Watch our stream: the in-game event “Test Event” is live in #EscapefromTarkov."}) {
        r.body=Page(text);auto value=OfficialEventSource::Parse(r);Check(value.success && value.announcements.empty());
    }
    r.body=Page("An in-game event has started in the seasonal #EscapefromTarkov mode.<br/><br/>The increased XP rates will remain active until October 5, 10:00 AM BST / 5:00 AM EDT.<br/><br/>The weekend event has ended.");
    auto parsed=OfficialEventSource::Parse(r);Check(parsed.success && parsed.announcements.size()==1);
    Check(parsed.announcements[0].status==EventStatus::Active && !parsed.announcements[0].endsAt);
    Check(parsed.announcements[0].startsAt==parsed.announcements[0].publishedAt&&parsed.announcements[0].startsAtFromPublication);
    Check(parsed.announcements[0].summary.find("weekend")==std::string::npos);
    Check(parsed.announcements[0].modes==std::vector<EventMode>{EventMode::Seasonal});
    Check(parsed.announcements[0].titleIsExcerpt);
    r.body=Page("An in-game event has started at 2026-10-03T10:00:00Z.");
    const auto explicitStart=OfficialEventSource::Parse(r);Check(explicitStart.success);
    Check(explicitStart.announcements[0].startsAt==ParseTimestamp("2026-10-03T10:00:00Z")&&!explicitStart.announcements[0].startsAtFromPublication);
    r.body=Page("An in-game event will start soon.");const auto preview=OfficialEventSource::Parse(r);
    Check(preview.success&&!preview.announcements[0].startsAt&&!preview.announcements[0].startsAtFromPublication);
    r.body=Page("An in-game event has started in the seasonal #EscapefromTarkov mode.");
    r.body+=r.body;Check(OfficialEventSource::Parse(r).announcements.size()==1);
    r.body=Page("An in-game event has started.");
    const auto shape=r.body.find("js-message_text");r.body.replace(shape,15,"changed-text-v2");
    Check(!OfficialEventSource::Parse(r).success);
    r.body=Page("An in-game event will start at 2026-10-04T10:00:00+03:00 in PvE mode. It will remain active until 2026-10-05T10:00:00+03:00.");
    parsed=OfficialEventSource::Parse(r);Check(parsed.success && parsed.announcements[0].startsAt && parsed.announcements[0].endsAt);
    Check(!parsed.announcements[0].startsAtFromPublication);
    EventCatalog c;std::string error;Check(c.Apply(parsed.announcements,error));Check(c.Apply(parsed.announcements,error));Check(c.Events().size()==1);
    r.body=Page("The in-game event will remain active until 2026-10-06T10:00:00Z. <a href=\"https://t.me/escapefromtarkovEN/100\">Original announcement</a>","101");
    parsed=OfficialEventSource::Parse(r);Check(parsed.success && c.Apply(parsed.announcements,error));Check(c.Events().size()==1 && c.Events()[0].sourceEvidence.size()==2);
    r.body=Page("The in-game event has ended. <a href=\"https://t.me/escapefromtarkovEN/100\">Original announcement</a>","102");
    parsed=OfficialEventSource::Parse(r);Check(parsed.success && c.Apply(parsed.announcements,error));Check(c.Events()[0].sourceStatus==EventStatus::Ended);
    r.body=Page("We have started the installation of an update. #EscapefromTarkov");Check(OfficialEventSource::Parse(r).announcements.empty());
    r.body=Page("An in-game event has started. It is not available in PvE mode.");
    Check(OfficialEventSource::Parse(r).announcements[0].modes.empty());
    r.body=Page("An in-game event has started, but not in PvP mode.");
    Check(OfficialEventSource::Parse(r).announcements[0].modes.empty());
    r.body=Page("An in-game event has started in #TarkovArena.");Check(OfficialEventSource::Parse(r).announcements.empty());
    r.body=Page("An in-game event has started in PvE mode and in PvP mode.");
    Check(OfficialEventSource::Parse(r).announcements[0].modes.size()==2);
    r.body=Page("The in-game event has ended.");Check(OfficialEventSource::Parse(r).announcements.empty());
    r.body="<html>login</html>";Check(!OfficialEventSource::Parse(r).success);
    r.body=Page("An in-game event has started.")+"challenge-platform";Check(!OfficialEventSource::Parse(r).success);
    r.body=Page("An in-game event has started.","oops");Check(!OfficialEventSource::Parse(r).success);
    r.body.assign(kMaximumResponseBytes+1,'x');Check(!OfficialEventSource::Parse(r).success);
    r.body=Page("An in-game event has started. &unknown;");Check(!OfficialEventSource::Parse(r).success);
    r.body=Page("An in-game event has started.");r.contentType="application/json";Check(!OfficialEventSource::Parse(r).success);
    r.status=304;Check(!OfficialEventSource::Parse(r).success);
    EventSourceState state;state.etag="\"v1\"";state.lastSuccessfulRefresh=100;Check(OfficialEventSource::Parse(r,state).notModified);
    std::cout<<"Official source fixtures PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
