#include "events/EventService.h"
#include "events/OfficialEventSource.h"
#include "events/EventEnrichment.h"
#include <windows.h>
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace noven::events;
void Check(bool ok){if(!ok)throw std::runtime_error("event service assertion");}
struct Source: IEventSource {
    std::atomic<int> calls{};EventSourceResult result;EventSourceState received;
    std::shared_future<void> gate;
    EventSourceResult Fetch(const EventSourceState& state,std::stop_token) override {
        ++calls;received=state;if(gate.valid())gate.wait();return result;
    }
};
struct CommunitySource: ICommunityEventSource {
    int calls{};CommunitySourceResult result;
    CommunitySourceResult Fetch(std::stop_token) override {++calls;return result;}
};
struct TranslationHttp:IEventHttp {
    int calls{};bool fail{};
    HttpResponse Get(std::wstring_view,std::wstring_view,std::string_view={},std::string_view={}) override {
        ++calls;if(fail)return {503,"text/html","unavailable"};
        return {200,"application/json","{\"responseStatus\":200,\"quotaFinished\":false,\"responseData\":{\"translatedText\":\"活动已经开始\",\"match\":0.85}}"};
    }
};
int main(int argc,char** argv){
    const auto dir=std::filesystem::temp_directory_path()/("noven-event-service-"+std::to_string(GetCurrentProcessId()));
    try {
        if(argc==3 && std::string_view(argv[1])=="--live") {
            WinHttpEventClient http;OfficialEventSource source(http);
            WikiEventSource wiki(http);EventService service(source,MakeEventEnrichment(argv[2],http),&wiki);std::promise<void> done;auto finished=done.get_future();
            service.SetChangedCallback([&]{done.set_value();});Check(service.Start(dir/"live.json"));finished.get();service.Stop();
            if(service.RefreshState().phase!=RefreshPhase::Ready)throw std::runtime_error(service.RefreshState().error);
            std::cout<<"Live service events="<<service.Events().size()<<" enrichment warning="<<service.RefreshState().enrichmentWarning
                <<" source warning="<<service.RefreshState().sourceWarning<<'\n';
            if(!service.RefreshState().sourceWarning.empty())throw std::runtime_error("live validation had a source failure");
            for(const auto& e:service.Events())std::cout<<e.eventId<<" maps="<<e.mapIds.size()<<" tasks="<<e.taskIds.size()<<" bosses="<<e.bossIds.size()<<'\n';
        }else {
            EventCatalog initial;EventSourceState state;std::string error;
            OfficialAnnouncement a;a.sourceRecordId="40";a.sourceUrl="https://t.me/escapefromtarkovEN/40";
            a.title="An in-game event has started";a.summary=a.title;a.status=EventStatus::Active;
            Check(initial.Apply({&a,1},error));state.newestMessageId="40";state.etag="\"v1\"";state.lastSuccessfulRefresh=100;
            {EventCache cache;EventCatalog empty;EventSourceState missing;Check(cache.Load(dir/"catalog.json",empty,missing,error));Check(cache.Save(initial,state,error));}
            Source failingSource;failingSource.result.error="network unavailable";std::promise<void> release;failingSource.gate=release.get_future().share();
            {EventService service(failingSource);std::promise<void> done;auto finished=done.get_future();service.SetChangedCallback([&]{done.set_value();});
            Check(service.Start(dir/"catalog.json"));Check(service.Events()==initial.Events());Check(service.ActiveEvents(100).size()==1);
            Check(!service.RequestRefresh());Check(service.Start(dir/"ignored.json"));
            release.set_value();finished.get();service.Stop();Check(failingSource.calls==1 && failingSource.received==state);
            Check(service.RefreshState().phase==RefreshPhase::Failed);Check(service.Events()==initial.Events());
            Check(service.LastSuccessfulRefresh()==100);Check(service.FindEvent("official-telegram:40").has_value());}
            {Source source;HttpResponse malformed;malformed.status=200;malformed.contentType="text/html";malformed.body="<html>error</html>";
            source.result=OfficialEventSource::Parse(malformed,state);EventService service(source);std::promise<void> done;auto finished=done.get_future();
            service.SetChangedCallback([&]{done.set_value();});Check(service.Start(dir/"catalog.json"));finished.get();service.Stop();
            Check(service.Events()==initial.Events() && service.RefreshState().phase==RefreshPhase::Failed);}
            {Source source;source.result.success=true;source.result.state=state;source.result.announcements={a};
            EventService service(source,[](EventCatalog&,std::stop_token,std::string& warning){warning="optional enrichment unavailable";return false;});
            std::promise<void> done;auto finished=done.get_future();service.SetChangedCallback([&]{done.set_value();});
            Check(service.Start(dir/"catalog.json"));finished.get();service.Stop();Check(source.calls==1);
            Check(service.Events()==initial.Events() && service.RefreshState().phase==RefreshPhase::Ready);
            Check(!service.RefreshState().enrichmentWarning.empty());Check(service.LastSuccessfulRefresh()>100);}
            {EventCache cache;EventCatalog restarted;EventSourceState s;Check(cache.Load(dir/"catalog.json",restarted,s,error));Check(restarted.Events()==initial.Events());}
            CommunityAnnouncement w;w.sourceRecordId="26936:Test_Event";w.sourceUrl=WikiEventUrl(w.sourceRecordId);
            w.sourceRevision="100";w.title="Community event";w.summary="Lighthouse";w.revisionAt=200;
            {Source source;source.result.error="official unavailable";CommunitySource wiki;wiki.result={true,{w},{}};
            EventService service(source,{},&wiki);std::promise<void> done;auto finished=done.get_future();service.SetChangedCallback([&]{done.set_value();});
            Check(service.Start(dir/"catalog.json"));finished.get();service.Stop();
            Check(service.Events().size()==2 && service.RefreshState().phase==RefreshPhase::Ready && !service.RefreshState().sourceWarning.empty());
            Check(service.FindEvent("official-telegram:40")==std::optional<EventRecord>{initial.Events().front()});
            Check(source.calls==1 && wiki.calls==1);}
            {Source source;source.result.success=true;source.result.announcements={a};CommunitySource wiki;wiki.result.error="wiki malformed";
            EventService service(source,{},&wiki);std::promise<void> done;auto finished=done.get_future();service.SetChangedCallback([&]{done.set_value();});
            Check(service.Start(dir/"catalog.json"));finished.get();service.Stop();
            Check(service.FindEvent("community-wiki:26936:Test_Event")->sourceStatus==EventStatus::Active);
            Check(service.Events().size()==2 && service.RefreshState().phase==RefreshPhase::Ready && !service.RefreshState().sourceWarning.empty());}
            {Source source;source.result.error="official unavailable";CommunitySource wiki;wiki.result.error="wiki unavailable";
            EventService service(source,{},&wiki);std::promise<void> done;auto finished=done.get_future();service.SetChangedCallback([&]{done.set_value();});
            Check(service.Start(dir/"catalog.json"));finished.get();service.Stop();
            Check(service.Events().size()==2 && service.RefreshState().phase==RefreshPhase::Failed);}
        }
        {
            Source source;source.result.success=true;source.result.announcements={{"99","https://t.me/escapefromtarkovEN/99","Title","Summary"}};
            TranslationHttp http;EventService service(source,{},nullptr,&http);std::promise<void> done;auto finished=done.get_future();
            service.SetChangedCallback([&]{if(service.RefreshState().phase==RefreshPhase::Ready)done.set_value();});
            Check(service.Start(dir/"translated"/"catalog.json"));finished.get();service.Stop();
            const auto event=service.FindEvent("official-telegram:99");Check(event&&event->machineText.size()==2&&event->title=="Title"&&!event->startsAt);
            Check(http.calls==2&&service.RefreshState().translationWarning.empty());
        }
        {
            Source source;source.result.success=true;source.result.notModified=true;TranslationHttp http;http.fail=true;
            EventService service(source,{},nullptr,&http);std::promise<void> done;auto finished=done.get_future();
            service.SetChangedCallback([&]{if(service.RefreshState().phase==RefreshPhase::Ready)done.set_value();});
            Check(service.Start(dir/"translated"/"catalog.json"));Check(service.Events().front().machineText.size()==2);
            finished.get();service.Stop();Check(http.calls==0&&service.Events().front().machineText.size()==2);
        }
        {
            Source source;source.result.success=true;source.result.announcements={{"99","https://t.me/escapefromtarkovEN/99","Updated title","Summary"}};
            TranslationHttp http;http.fail=true;EventService service(source,{},nullptr,&http);std::promise<void> done;auto finished=done.get_future();
            service.SetChangedCallback([&]{if(service.RefreshState().phase==RefreshPhase::Ready)done.set_value();});
            Check(service.Start(dir/"translated"/"catalog.json"));finished.get();service.Stop();
            Check(service.Events().front().title=="Updated title"&&service.Events().front().machineText.size()==1);
            Check(!service.RefreshState().translationWarning.empty()&&service.RefreshState().error.empty());
        }
        std::filesystem::remove_all(dir);std::cout<<"Single-shot cache-preserving service PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
