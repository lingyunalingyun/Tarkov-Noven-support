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
        std::cout<<"Live official normalized="<<c.Events().size()<<" cursor="<<result.state.newestMessageId<<'\n';return 0;
    }
    HttpResponse r;r.status=200;r.contentType="text/html; charset=utf-8";
    r.body=Page("An in-game event has started in the seasonal #EscapefromTarkov mode.<br/><br/>The increased XP rates will remain active until October 5, 10:00 AM BST / 5:00 AM EDT.<br/><br/>The weekend event has ended.");
    auto parsed=OfficialEventSource::Parse(r);Check(parsed.success && parsed.announcements.size()==1);
    Check(parsed.announcements[0].status==EventStatus::Active && !parsed.announcements[0].endsAt && !parsed.announcements[0].startsAt);
    Check(parsed.announcements[0].summary.find("weekend")==std::string::npos);
    Check(parsed.announcements[0].modes==std::vector<EventMode>{EventMode::Seasonal});
    Check(parsed.announcements[0].titleIsExcerpt);
    r.body+=r.body;Check(OfficialEventSource::Parse(r).announcements.size()==1);
    r.body=Page("An in-game event has started.");
    const auto shape=r.body.find("js-message_text");r.body.replace(shape,15,"changed-text-v2");
    Check(!OfficialEventSource::Parse(r).success);
    r.body=Page("An in-game event will start at 2026-10-04T10:00:00+03:00 in PvE mode. It will remain active until 2026-10-05T10:00:00+03:00.");
    parsed=OfficialEventSource::Parse(r);Check(parsed.success && parsed.announcements[0].startsAt && parsed.announcements[0].endsAt);
    EventCatalog c;std::string error;Check(c.Apply(parsed.announcements,error));Check(c.Apply(parsed.announcements,error));Check(c.Events().size()==1);
    r.body=Page("The in-game event will remain active until 2026-10-06T10:00:00Z. <a href=\"https://t.me/escapefromtarkovEN/100\">Original announcement</a>","101");
    parsed=OfficialEventSource::Parse(r);Check(parsed.success && c.Apply(parsed.announcements,error));Check(c.Events().size()==1 && c.Events()[0].sourceEvidence.size()==2);
    r.body=Page("The in-game event has ended. <a href=\"https://t.me/escapefromtarkovEN/100\">Original announcement</a>","102");
    parsed=OfficialEventSource::Parse(r);Check(parsed.success && c.Apply(parsed.announcements,error));Check(c.Events()[0].sourceStatus==EventStatus::Ended);
    r.body=Page("We have started the installation of an update. #EscapefromTarkov");Check(OfficialEventSource::Parse(r).announcements.empty());
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
