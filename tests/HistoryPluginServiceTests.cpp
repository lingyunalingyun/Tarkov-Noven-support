#include "plugins/CatalogPluginService.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace noven::plugins;
namespace raid=noven::raid;
namespace events=noven::events;
namespace json=raid::json;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main() try {
    noven::data::MapRecord map;map.id="interchange";map.nameEn="Interchange";map.nameZh="立交桥";
    CatalogPluginService service({}, {}, std::vector{map});
    CatalogGrants grants{true,{"raid.history.read","catalog.events.read"},{"raid.history.read","catalog.events.read"}};
    raid::RaidSession raid;raid.localSessionId="raid-exact_1";raid.mapId=map.id;raid.startObserved=raid.endObserved=true;
    raid.startedAt=100;raid.endedAt=200;raid.duration=100;raid.gameMode=raid::GameMode::PvE;raid.outcome=raid::RaidOutcome::Survived;
    raid.startSource="private-path";raid.eftRaidId="private-eft-id";raid.startOffset=777;
    auto unknown=raid;unknown.localSessionId="raid-exact_2";unknown.mapId="unrecognized-private-value";unknown.startedAt=300;
    auto active=raid;active.localSessionId="active";active.endObserved=false;
    events::EventRecord official;official.eventId="official-telegram:42";official.title="Original";official.localizedTitles["zh-CN"]="公告";
    official.summary="Summary";official.startsAt=10;official.startsAtFromPublication=true;official.sourceStatus=events::EventStatus::Active;
    official.sourceEvidence.emplace_back().sourceKind=events::SourceKind::OfficialTelegram;
    auto community=official;community.eventId="community-wiki:26936:Anchor.%20";community.startsAt.reset();community.startsAtFromPublication=false;
    community.sourceEvidence[0].sourceKind=events::SourceKind::CommunityWiki;
    DataRequest request;request.requestId=1;request.catalog=CatalogKind::RaidHistory;request.limit=1;
    Check(service.Query(request,grants).status==DataStatus::Unavailable,"unpublished snapshot unavailable");
    service.PublishRaidHistory(std::vector{raid,unknown,active});service.PublishEvents(std::vector{official,community});
    auto result=service.Query(request,grants);auto value=json::Parser(result.payload).Parse();
    Check(result.status==DataStatus::Ok&&value.At("schemaVersion").Int()==1,"history schema");
    const auto& row=value.At("records").array[0];
    Check(row.At("localSessionId").String()==unknown.localSessionId&&value.At("total").Int()==2,"recent completed history only");
    Check(!row.At("mapKnown").boolean&&row.At("stableMapId").type==json::Value::Type::Null,"unknown map not guessed");
    request.offset=1;value=json::Parser(service.Query(request,grants).payload).Parse();
    Check(value.At("records").array[0].At("mapDisplayName").String()=="立交桥"&&!value.At("hasMore").boolean,"localized second page");
    service.SetLocale("en-US");
    auto longestRaid=raid;longestRaid.localSessionId="raid:"+std::string(251,'a');service.PublishRaidHistory(std::vector{raid,longestRaid});
    request={};request.requestId=3;request.catalog=CatalogKind::RaidHistory;request.operation=DataOperation::Get;request.limit=0;request.stableId=longestRaid.localSessionId;
    Check(service.Query(request,grants).status==DataStatus::Ok,"stored history maximum identity and colon round trip");
    request.stableId+='a';Check(!ValidDataRequest(request),"history identity bound");
    for(const auto kind:{CatalogKind::RaidHistory,CatalogKind::Events}){
        request={};request.requestId=2;request.catalog=kind;request.operation=DataOperation::Get;request.limit=0;
        request.stableId=kind==CatalogKind::RaidHistory?raid.localSessionId:community.eventId;
        result=service.Query(request,grants);value=json::Parser(result.payload).Parse();
        Check(result.status==DataStatus::Ok&&value.At("record").At(kind==CatalogKind::RaidHistory?"localSessionId":"eventId").String()==request.stableId,"exact identity round trip");
        for(const auto secret:{"private-path","private-eft-id","startOffset","parserVersion","startSource","sourceUrl","sourceRevision","rawLog"})Check(result.payload.find(secret)==std::string::npos,"private fields absent");
        if(kind==CatalogKind::Events){const auto& record=value.At("record");Check(!record.At("official").boolean&&record.At("source").String()=="communityWiki","community never official");
            Check(record.At("endsAt").type==json::Value::Type::Null&&!record.At("startsAtFromPublication").boolean,"missing end and source provenance");}
        request.stableId="unknown";Check(service.Query(request,grants).status==DataStatus::NotFound,"unknown identity");
        for(unsigned mode=0;mode<3;++mode){auto denied=grants;if(mode==0)denied.declared.clear();if(mode==1)denied.granted.clear();if(mode==2)denied.valid=false;
            Check(service.Query(request,denied).status==DataStatus::PermissionDenied,"declaration grant and fingerprint required");}
        CatalogGrants other{true,{std::string(CatalogPermission(kind==CatalogKind::Events?CatalogKind::RaidHistory:CatalogKind::Events))},{}};other.granted=other.declared;
        Check(service.Query(request,other).status==DataStatus::PermissionDenied,"permissions and sessions isolated");
    }
    request.stableId=official.eventId;value=json::Parser(service.Query(request,grants).payload).Parse();
    Check(value.At("record").At("official").boolean&&value.At("record").At("startsAtFromPublication").boolean,"official publication-derived start preserved");
    auto longest=community;longest.eventId="community-wiki:26936:"+std::string(240,'a');service.PublishEvents(std::vector{longest});
    request.stableId=longest.eventId;Check(request.stableId.size()==261&&service.Query(request,grants).status==DataStatus::Ok,"existing maximum event ID supported");
    request.stableId+='a';Check(!ValidDataRequest(request),"event identity length bounded");
    request.catalog=CatalogKind::Items;request.stableId=std::string(129,'a');Check(!ValidDataRequest(request),"old ID bound unchanged");
    request.catalog=CatalogKind::Events;request.stableId="../x";Check(!ValidDataRequest(request),"no path identity");
    request.operation=DataOperation::List;request.stableId.clear();request.limit=64;
    community.summary=std::string(7000,'x');std::vector<events::EventRecord> large;
    for(unsigned i=0;i<4;++i){auto event=community;event.eventId="community-wiki:26936:"+std::to_string(i);large.push_back(event);}service.PublishEvents(large);
    result=service.Query(request,grants);value=json::Parser(result.payload).Parse();Check(result.payload.size()<=MaximumCatalogPayloadBytes&&value.At("hasMore").boolean,"response budget shrinks event page");
    community.summary=std::string(MaximumCatalogPayloadBytes,'x');service.PublishEvents(std::vector{community});Check(service.Query(request,grants).status==DataStatus::TooLarge,"oversized record bounded");
    service.PublishEvents({});Check(service.Query(request,grants).status==DataStatus::Ok,"published empty snapshot healthy");
    std::jthread writer([&]{for(unsigned i=0;i<100;++i)service.PublishEvents(i%2?std::vector{official}:std::vector{community});});
    for(unsigned i=0;i<100;++i){result=service.Query(request,grants);Check(result.payload.size()<=MaximumCatalogPayloadBytes,"snapshot replacement bounded");json::Parser(result.payload).Parse();}
    std::cout<<"History/event projection privacy, identity, snapshots, bounds and permissions PASS\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
