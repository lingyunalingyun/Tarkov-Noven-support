#include "plugins/PluginProtocol.h"
#include "plugins/PluginScanData.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace noven::plugins::ipc;
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
template<class F> void Reject(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Check(rejected,"protocol must reject invalid input");}
int main(){
    Message hello{MessageType::Hello,1,"com.example.loot-route",std::string(64,'a')};
    const auto encoded=Serialize(hello);Check(MatchesSession(ParseMessage(encoded),hello.pluginId,hello.session),"handshake round trip");
    for(auto bad: {Message{MessageType::Hello,2,hello.pluginId,hello.session},Message{MessageType::Hello,1,"com.other.test",hello.session},Message{MessageType::Hello,1,hello.pluginId,std::string(64,'b')}})
        Check(!MatchesSession(bad,hello.pluginId,hello.session),"all handshake facts checked");
    for(const auto type:{MessageType::HelloAck,MessageType::Ping,MessageType::Pong,MessageType::Shutdown,MessageType::ShutdownAck,MessageType::ProtocolError}){
        auto message=hello;message.type=type;Check(ParseMessage(Serialize(message)).type==type,"all tiny protocol messages");
    }
    const auto frame=Frame(encoded);
    for(std::size_t split=0;split<=frame.size();++split){
        FrameDecoder decoder;unsigned count=0;auto consume=[&](std::string_view text){Check(text==encoded,"partial reads preserve bytes");++count;};
        decoder.Feed(std::span(frame).first(split),consume);decoder.Feed(std::span(frame).subspan(split),consume);
        Check(count==1&&decoder.Complete(),"every header/payload split");
    }
    auto two=frame;two.insert(two.end(),frame.begin(),frame.end());FrameDecoder decoder;unsigned count=0;
    decoder.Feed(two,[&](std::string_view text){ParseMessage(text);++count;});Check(count==2&&decoder.Complete(),"coalesced frames");
    FrameDecoder partial;partial.Feed(std::span(frame).first(frame.size()-1),[](auto){});Check(!partial.Complete(),"disconnect mid-frame rejects completeness");
    for(const auto bytes:{std::vector<std::uint8_t>{0,0,0,0},std::vector<std::uint8_t>{1,0,1,0}}){FrameDecoder bad;Reject([&]{bad.Feed(bytes,[](auto){});});Check(!bad.Complete(),"invalid decoder remains poisoned");}
    Reject([]{Frame("");});Reject([]{Frame(std::string(MaximumFrameBytes+1,'x'));});
    Check(Frame(std::string(MaximumFrameBytes,'x')).size()==MaximumFrameBytes+4,"exact frame capacity");
    for(const auto bad:{"{","[]",R"({"type":"unknown"})",R"({"type":"hello"})",R"({"type":1})",R"({"type":"ping","method":"execute"})",R"({"type":"hello","protocolVersion":"1","pluginId":"a.b","session":"x"})"})Reject([&]{ParseMessage(bad);});
    Reject([]{ParseMessage(std::string("{\"type\":\"ping")+static_cast<char>(0xff)+"\"}");});
    Check(!ValidSecret(std::string(64,'z'))&&!ValidSecret("secret"),"strict token format");
    Message load{MessageType::LoadPlugin};load.directory="C:\\Noven\\plugins\\com.example.test";load.entry="plugin.dll";load.pagePermission=true;
    const auto loaded=ParseMessage(Serialize(load));Check(loaded.entry==load.entry&&loaded.directory==load.directory&&loaded.pagePermission,"explicit v2 runtime request");
    Message page{MessageType::UiRegisterPage};page.pageId="dashboard";page.title="Example";
    Check(ParseMessage(Serialize(page)).title==page.title,"local page registration");
    page.type=MessageType::UiPublishPage;page.document=R"({"schemaVersion":1,"blocks":[{"type":"button","id":"refresh","label":"Refresh"}]})";
    Check(ParseMessage(Serialize(page)).document==page.document,"declarative document round trip");
    Message action{MessageType::UiAction};action.pageId="dashboard";action.actionId="refresh";
    Check(ParseMessage(Serialize(action)).actionId=="refresh","bounded local action");
    for(const auto type:{MessageType::LoadPluginResult,MessageType::UiActionResult}){Message reply{type};reply.result=6;Check(ParseMessage(Serialize(reply)).result==6,"bounded result codes");}
    Message log{MessageType::Log};log.text="Scoped log";Check(ParseMessage(Serialize(log)).text==log.text,"log carries no spoofed plugin identity");
    for(const auto bad:{R"({"type":"uiRegisterPage","pageId":"builtin.plugins","title":"Spoof"})",R"({"type":"uiAction","pageId":"dashboard","actionId":"../x"})",R"({"type":"log","text":"x","pluginId":"com.other.test"})",R"({"type":"loadPluginResult","result":7})",R"({"type":"loadPlugin","manifestVersion":1,"apiVersion":1,"abiVersion":1,"directory":"C:\\x","entry":"plugin.dll","pagePermission":true})"})Reject([&]{ParseMessage(bad);});
    load.entry="../plugin.dll";Reject([&]{Serialize(load);});
    Message scan{MessageType::ScanEvent};noven::data::RecentScanEntry entry;entry.scanId=1;entry.scannedAtUnixMs=100;entry.stableItemId="exact";entry.canonicalName="Item";
    scan.text=noven::plugins::ScanRecord(entry);scan.sequence=1;scan.dropped=8;
    const auto event=ParseMessage(Serialize(scan));Check(event.sequence==1&&event.dropped==8&&event.text==scan.text,"bounded safe scan event round trip");
    scan.sequence=0;Reject([&]{Serialize(scan);});scan.sequence=1;scan.text="{}";Reject([&]{Serialize(scan);});
    for(const auto type:{MessageType::ScanSubscribe,MessageType::ScanUnsubscribe,MessageType::ScanSubscriptionAck})Check(ParseMessage(Serialize(Message{type})).type==type,"scoped subscription commands");
    for(const auto bad:{R"({"type":"scanSubscribe","pluginId":"com.other.test"})",R"({"type":"scanEventAck","sequence":0})",R"({"type":"scanEventAck","sequence":"1"})",R"({"type":"scan.trigger"})",R"({"type":"scan.capture"})",R"({"type":"screen.read"})"})Reject([&]{ParseMessage(bad);});
    page.document=R"({"schemaVersion":1,"blocks":[{"type":"html","text":"x"}]})";Reject([&]{Serialize(page);});
    std::cout<<"Bounded plugin protocol PASS\n";
    Message access{MessageType::CatalogAccess};access.catalogMask=7;
    Check(ParseMessage(Serialize(access)).catalogMask==7,"catalog grant configuration");
    access.catalogMask=31;Check(ParseMessage(Serialize(access)).catalogMask==31,"five isolated data permissions");
    access.catalogMask=63;Check(ParseMessage(Serialize(access)).catalogMask==63,"six isolated data permissions");
    access.catalogMask=64;Reject([&]{Serialize(access);});
    Message scanRequest{MessageType::DataRequest};scanRequest.dataRequest={1,noven::plugins::CatalogKind::RecentScans,noven::plugins::DataOperation::Get,"18446744073709551615",0,0};
    Check(ParseMessage(Serialize(scanRequest)).dataRequest.stableId=="18446744073709551615","persisted uint64 scan identity wire round trip");
    scanRequest.dataRequest.stableId="18446744073709551616";Reject([&]{Serialize(scanRequest);});
    Message scanAccess{MessageType::ScanAccess};scanAccess.scanPermission=true;Check(ParseMessage(Serialize(scanAccess)).scanPermission,"separate subscribe grant");
    Message subscription{MessageType::ScanSubscriptionResult};subscription.subscribed=true;Check(ParseMessage(Serialize(subscription)).subscribed,"explicit activation result");
    subscription.result=1;Reject([&]{Serialize(subscription);});
    Message request{MessageType::DataRequest};request.dataRequest.requestId=99;
    for(const auto kind:{noven::plugins::CatalogKind::Items,noven::plugins::CatalogKind::Tasks,noven::plugins::CatalogKind::Maps,noven::plugins::CatalogKind::RaidHistory,noven::plugins::CatalogKind::Events}){
        request.dataRequest.catalog=kind;request.dataRequest.operation=noven::plugins::DataOperation::List;request.dataRequest.limit=32;
        Check(ParseMessage(Serialize(request)).dataRequest.catalog==kind,"list data request round-trip");
        request.dataRequest.operation=noven::plugins::DataOperation::Get;request.dataRequest.limit=0;request.dataRequest.stableId="stable_id";
        Check(ParseMessage(Serialize(request)).dataRequest.stableId=="stable_id","get exact stable identity");
        request.dataRequest.stableId.clear();
    }
    request.dataRequest.catalog=noven::plugins::CatalogKind::Events;
    request.dataRequest.stableId="community-wiki:26936:"+std::string(240,'a');
    Check(ParseMessage(Serialize(request)).dataRequest.stableId==request.dataRequest.stableId,"maximum event identity wire round trip");
    request.dataRequest.stableId+='a';Reject([&]{Serialize(request);});
    Message result{MessageType::DataResult};result.dataResult.requestId=99;
    result.dataResult.payload=R"({"schemaVersion":1,"requestId":99,"catalog":"items","operation":"list","status":"ok","records":[],"record":null,"offset":0,"limit":32,"total":0,"nextOffset":0,"hasMore":false})";
    Check(ParseMessage(Serialize(result)).dataResult.payload==result.dataResult.payload,"catalog JSON copy round-trip");
    auto badResult=result;badResult.dataResult.requestId=98;Reject([&]{Serialize(badResult);});
    badResult=result;badResult.dataResult.status=noven::plugins::DataStatus::PermissionDenied;Reject([&]{Serialize(badResult);});
    badResult=result;badResult.dataResult.payload="{";Reject([&]{Serialize(badResult);});
    badResult=result;badResult.dataResult.payload=std::string(noven::plugins::MaximumCatalogPayloadBytes+1,'x');Reject([&]{Serialize(badResult);});
    Message ack{MessageType::DataResultAck};ack.dataResult.requestId=99;
    Check(ParseMessage(Serialize(ack)).dataResult.requestId==99,"callback completion acknowledgment");
    ack.dataResult.requestId=0;Reject([&]{Serialize(ack);});
    for(const auto bad:{R"({"type":"dataRequest","requestId":1,"catalog":"raids","operation":"list","stableId":"","offset":0,"limit":32})",R"({"type":"dataRequest","requestId":1,"catalog":"items","operation":"write","stableId":"","offset":0,"limit":32})",R"({"type":"dataRequest","requestId":1,"catalog":"items","operation":"list","stableId":"","offset":4294967295,"limit":32})",R"({"type":"dataRequest","requestId":1,"catalog":"items","operation":"list","stableId":"","offset":0,"limit":0})",R"({"type":"dataResultAck","requestId":1,"pluginId":"com.other.test"})"})Reject([&]{ParseMessage(bad);});
    result.dataResult.payload.insert(result.dataResult.payload.find("[]")+1,"{\"displayName\":\""+std::string(11000,'\\')+"\"}");
    Check(Serialize(result).size()<MaximumFrameBytes,"escaped result fits existing transport");
    const auto dataFrame=Frame(Serialize(result));FrameDecoder dataDecoder;unsigned dataCount=0;
    for(const auto byte:dataFrame)dataDecoder.Feed(std::span(&byte,1),[&](auto payload){ParseMessage(payload);++dataCount;});
    Check(dataCount==1&&dataDecoder.Complete(),"catalog frame split at every byte");
    Message storage{MessageType::StorageRequest};storage.storageRequest={1,noven::plugins::StorageOperation::Set,"中文",std::string(32768,'\xff')};
    Check(Serialize(storage).size()<MaximumFrameBytes&&ParseMessage(Serialize(storage)).storageRequest.value==storage.storageRequest.value,"bounded maximum opaque storage frame");
    Message stored{MessageType::StorageResult};stored.storageResult.requestId=1;stored.storageResult.value=std::string("a\0\xff",3);
    Check(ParseMessage(Serialize(stored)).storageResult.value==stored.storageResult.value,"binary storage response exact");
    stored.storageResult.value.clear();stored.storageResult.keys={"a","中文"};stored.storageResult.total=2;stored.storageResult.nextOffset=2;
    Check(ParseMessage(Serialize(stored)).storageResult.keys==stored.storageResult.keys,"logical keys only");
    for(const auto bad:{R"({"type":"storageRequest","schemaVersion":1,"requestId":1,"operation":1,"key":"settings","value":"","offset":0,"limit":0,"pluginId":"com.other.test"})",R"({"type":"storageRequest","schemaVersion":1,"requestId":1,"operation":2,"key":"../x","value":"","offset":0,"limit":0})",R"({"type":"storageResultAck","requestId":0})"})Reject([&]{ParseMessage(bad);});
}
