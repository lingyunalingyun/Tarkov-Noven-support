#include "events/EventBrowser.h"
#include <iostream>
#include <stdexcept>
using namespace noven::events;
void Check(bool value){if(!value)throw std::runtime_error("event browser assertion");}
int main(){try{
    EventBrowser b;b.SetNow(100);
    EventRecord active;active.eventId="a";active.title="Official Alpha";active.summary="Stored content";active.sourceStatus=EventStatus::Active;
    active.mapIds={"reserve","missing"};active.taskIds={"task"};active.itemIds={"item"};active.bossIds={"killa"};
    EventRecord future;future.eventId="b";future.startsAt=200;
    EventRecord ended;ended.eventId="c";ended.endsAt=90;
    EventRecord unknown;unknown.eventId="d";unknown.announcedAt=50;
    b.SetEntities({{{EntityKind::Map,"reserve"},"储备站"},{{EntityKind::Task,"task"},"探路者"},
        {{EntityKind::Item,"item"},"保险箱"},{{EntityKind::Boss,"killa"},"Killa"}});
    b.SetEvents({ended,unknown,future,active});Check(b.Rows()==std::vector<std::size_t>({3,2,1,0}));
    for(auto status:{EventStatus::Active,EventStatus::Upcoming,EventStatus::Ended,EventStatus::Unknown}) {
        b.SetFilter(status,{});Check(b.Rows().size()==1&&b.Status(b.Events()[b.Rows()[0]])==status);
    }
    for(auto query:{"alpha","stored","储备站","探路者","保险箱","killa"}) {b.SetFilter({},query);Check(b.Rows()==std::vector<std::size_t>({3}));}
    b.SetFilter({},"nonexistent");Check(b.Rows().empty()&&!b.Events().empty());
    b.SetFilter({},{});const auto rows=b.Rows();Check(b.Find("a")&&!b.Find("missing"));Check(b.Rows()==rows);
    Check(!b.Find("d")->startsAt&&b.Find("d")->modes.empty());
    Check(b.Associations(active).size()==4&&b.Unresolved(active)==1);
    const auto builds=b.Builds();for(int i=0;i<1000;++i){b.SetFilter({},{});b.SetNow(101);Check(b.Rows()==rows);}Check(b.Builds()==builds);
    b.SetNow(201);Check(b.Count(EventStatus::Active)==2);
    Check(SafeEventSourceUrl("https://t.me/escapefromtarkovEN/6880"));
    Check(!SafeEventSourceUrl("https://t.me.evil/escapefromtarkovEN/6880"));Check(!SafeEventSourceUrl("file:///x"));
    Check(!SafeEventSourceUrl("https://changes.tarkov-changes.com/view/12?evil"));
    b.SetEvents({});Check(b.Rows().empty());
    std::cout<<"Cached event browser PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
