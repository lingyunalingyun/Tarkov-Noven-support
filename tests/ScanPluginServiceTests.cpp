#include "plugins/CatalogPluginService.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace noven::plugins;
namespace json=noven::raid::json;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main() try {
    CatalogPluginService service({}, {}, {});CatalogGrants grants{true,{"scan.history.read"},{"scan.history.read"}};
    noven::data::RecentScanEntry a;a.scanId=(std::numeric_limits<std::uint64_t>::max)();a.scannedAtUnixMs=100;
    a.stableItemId="stable_exact_1";a.canonicalName="物品";a.localSessionId="raid-exact";a.bestTraderName="private-price-trader";
    auto b=a;b.scanId=2;b.localSessionId.reset();DataRequest request;request.requestId=1;request.catalog=CatalogKind::RecentScans;request.limit=1;
    Check(service.Query(request,grants).status==DataStatus::Unavailable,"unpublished history");service.PublishRecentScans(std::vector{a,b});auto result=service.Query(request,grants);
    auto value=json::Parser(result.payload).Parse();const auto& row=value.At("records").array[0];
    Check(value.At("schemaVersion").Int()==1&&value.At("total").Int()==2&&value.At("hasMore").Bool(),"paged existing pipeline");
    Check(row.object.size()==5&&row.At("scanId").String()==std::to_string(a.scanId)&&row.At("stableItemId").String()==a.stableItemId,"uint64 and item identities exact");
    Check(row.At("localSessionId").String()==*a.localSessionId,"stored association only");
    for(const auto key:{"private-price-trader","canonicalShortName","ambiguous","matchMode","capture","ocr","confidence","window","image","fleaPrice","itemWidth"})Check(result.payload.find(key)==std::string::npos,"minimal privacy whitelist");
    request.offset=1;value=json::Parser(service.Query(request,grants).payload).Parse();
    Check(value.At("records").array[0].At("localSessionId").type==json::Value::Type::Null&&!value.At("hasMore").Bool(),"missing association and last page");
    request.operation=DataOperation::Get;request.offset=request.limit=0;request.stableId=std::to_string(a.scanId);
    value=json::Parser(service.Query(request,grants).payload).Parse();Check(value.At("record").At("scanId").String()==request.stableId,"get stored identity");
    request.stableId="3";Check(service.Query(request,grants).status==DataStatus::NotFound,"unknown identity");
    for(const auto bad:{"0","01","-1","18446744073709551616","1a"}){request.stableId=bad;Check(service.Query(request,grants).status==DataStatus::InvalidRequest,"strict uint64 identity");}request.stableId="2";
    for(unsigned i=0;i<3;++i){auto denied=grants;if(i==0)denied.declared.clear();if(i==1)denied.granted.clear();if(i==2)denied.valid=false;Check(service.Query(request,denied).status==DataStatus::PermissionDenied,"declaration grant validity required");}
    CatalogGrants events{true,{"scan.events.subscribe"},{"scan.events.subscribe"}};Check(service.Query(request,events).status==DataStatus::PermissionDenied,"subscription is not history grant");
    std::vector<noven::data::RecentScanEntry> many(200,b);for(unsigned i=0;i<many.size();++i){many[i].scanId=i+1;many[i].canonicalName=std::string(4000,'a');}
    service.PublishRecentScans(many);request.operation=DataOperation::List;request.stableId.clear();request.limit=64;unsigned total=0;
    do{result=service.Query(request,grants);Check(result.status==DataStatus::Ok&&result.payload.size()<=MaximumCatalogPayloadBytes,"bounded shrinking page");value=json::Parser(result.payload).Parse();total+=static_cast<unsigned>(value.At("records").array.size());request.offset=static_cast<unsigned>(value.At("nextOffset").Int());}while(value.At("hasMore").Bool());Check(total==200,"complete traversal");
    ScanEventQueue queue;auto record=std::make_shared<const std::string>(ScanRecord(b));Check(!queue.Push(record)&&!queue.Pop(),"no event before subscribe");queue.Subscribe(true);queue.Subscribe(true);Check(!queue.Pop(),"no replay");
    for(unsigned i=1;i<=40;++i){auto entry=b;entry.scanId=i;Check(queue.Push(std::make_shared<const std::string>(ScanRecord(entry))),"enqueue");}
    Check(queue.Pending()==32&&queue.Dropped()==8,"bounded drop oldest");for(unsigned i=9;i<=40;++i)Check(json::Parser(*queue.Pop()).Parse().At("scanId").String()==std::to_string(i),"remaining FIFO");
    queue.Push(record);queue.Subscribe(false);Check(!queue.Pending()&&!queue.Push(record),"unsubscribe teardown");
    Check(!ValidScanRecord("{}")&&!ValidScanRecord(ScanRecord(b).substr(1))&&!ValidScanRecord(std::string(8193,' ')),"malformed oversized projection");
    auto oversized=b;oversized.canonicalName=std::string(4097,'a');service.PublishRecentScans(std::vector{oversized,b});request.offset=0;
    value=json::Parser(service.Query(request,grants).payload).Parse();Check(value.At("total").Int()==1,"unsafe oversized persisted row omitted without suppressing valid neighbors");
    std::cout<<"Scan projection/history/queue PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
