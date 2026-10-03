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
int main(int argc,char** argv){
    const auto dir=std::filesystem::temp_directory_path()/("noven-event-service-"+std::to_string(GetCurrentProcessId()));
    try {
        if(argc==3 && std::string_view(argv[1])=="--live") {
            WinHttpEventClient http;OfficialEventSource source(http);
            EventService service(source,MakeEventEnrichment(argv[2],http));std::promise<void> done;auto finished=done.get_future();
            service.SetChangedCallback([&]{done.set_value();});Check(service.Start(dir/"live.json"));finished.get();service.Stop();
            if(service.RefreshState().phase!=RefreshPhase::Ready)throw std::runtime_error(service.RefreshState().error);
            std::cout<<"Live service events="<<service.Events().size()<<" warning="<<service.RefreshState().enrichmentWarning<<'\n';
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
        }
        std::filesystem::remove_all(dir);std::cout<<"Single-shot cache-preserving service PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
