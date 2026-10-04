#include "events/EventCache.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace noven::events;
void Check(bool ok){if(!ok)throw std::runtime_error("event cache assertion");}
int main(){
    const auto dir=std::filesystem::temp_directory_path()/("noven-event-cache-"+std::to_string(GetCurrentProcessId()));
    try{
        EventCatalog c;EventSourceState s;std::string error;
        {EventCache cache;Check(cache.Load(dir/"catalog.json",c,s,error));Check(c.Events().empty());
        OfficialAnnouncement a;a.sourceRecordId="20";a.sourceUrl="https://t.me/escapefromtarkovEN/20";
        a.title="活动 Event";a.summary="Explicit official facts";a.status=EventStatus::Active;
        Check(c.Apply({&a,1},error));s.newestMessageId="20";s.etag="\"v1\"";s.lastSuccessfulRefresh=100;
        Check(cache.Save(c,s,error));
        const auto encoded=EventCache::Encode(c,s);EventCatalog restored;EventSourceState state;
        Check(EventCache::Decode(encoded,restored,state,error));Check(restored.Events()==c.Events() && state==s);
        auto legacy=encoded;legacy.replace(legacy.find("\"schemaVersion\":2"),17,"\"schemaVersion\":1");
        const std::string extra=",\"sourceRevision\":\"\",\"summary\":\"\"";
        legacy.erase(legacy.find(extra),extra.size());
        Check(EventCache::Decode(legacy,restored,state,error)&&restored.Events()==c.Events());
        Check(!EventCache::Decode("{\"schemaVersion\":1,\"schemaVersion\":1}",restored,state,error));
        Check(restored.Events()==c.Events());Check(!EventCache::Decode("<html>captcha</html>",restored,state,error));
        auto bad=s;bad.etag="\r\nInjected: true";Check(!cache.Save(c,bad,error));}
        {EventCache restarted;EventCatalog restored;EventSourceState state;
        Check(restarted.Load(dir/"catalog.json",restored,state,error));Check(restored.Events()==c.Events() && state==s);}
        {std::ofstream out(dir/"broken.json",std::ios::binary);out<<"{}";}
        {EventCache broken;Check(!broken.Load(dir/"broken.json",c,s,error));Check(c.Events().size()==1);
        Check(!broken.Save(c,s,error));}
        CommunityAnnouncement wiki;wiki.sourceRecordId="26936:Example_Event";wiki.sourceUrl=WikiEventUrl(wiki.sourceRecordId);
        wiki.sourceRevision="100";wiki.title="Community title";wiki.summary="Glukhar moved to Lighthouse";wiki.revisionAt=200;
        Check(c.ApplyCommunity({&wiki,1},error));EventCatalog roundTrip;EventSourceState restoredState;
        Check(EventCache::Decode(EventCache::Encode(c,s),roundTrip,restoredState,error)&&roundTrip.Events()==c.Events());
        Check(!roundTrip.FindEvent("community-wiki:26936:Example_Event")->startsAt);
        std::filesystem::remove_all(dir);std::cout<<"Event cache contracts PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
