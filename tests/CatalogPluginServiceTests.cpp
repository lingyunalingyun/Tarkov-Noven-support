#include "plugins/CatalogPluginService.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace noven::plugins;
namespace data=noven::data;
namespace json=noven::raid::json;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
json::Value Decode(const DataResult& result){
    return json::Parser(result.payload).Parse();
}
int main() try {
    data::ItemRecord item;item.id="item_1";item.nameZh="物品";item.nameEn="Item";item.width=2;item.height=1;
    item.types={"key"};item.caliber="";
    auto second=item;second.id="item_2";
    std::vector items{second,item};
    data::TaskRecord task;task.mode="regular";task.id="task_1";task.nameZh="任务";task.nameEn="Task";
    task.traderId="trader_1";task.minimumLevel=5;task.experience=100;task.kappaRequired=true;
    auto pve=task;pve.mode="pve";pve.nameEn="PvE variant";
    std::vector tasks{task,pve};
    data::MapRecord map;map.id="interchange";map.nameZh="立交桥";map.nameEn="Interchange";map.players="10-14";map.raidDuration=40;
    std::vector maps{map};
    CatalogPluginService service(items,tasks,maps);
    CatalogGrants grants{true,{"catalog.items.read","catalog.tasks.read","catalog.maps.read"},{"catalog.items.read","catalog.tasks.read","catalog.maps.read"}};
    DataRequest request;request.requestId=7;request.limit=1;
    const auto list=service.Query(request,grants);auto value=Decode(list);
    Check(list.status==DataStatus::Ok&&value.At("schemaVersion").Int()==1&&value.At("requestId").Int()==7,"schema/request identity");
    Check(value.At("records").array.size()==1&&value.At("records").array[0].At("stableItemId").String()=="item_1","deterministic stable item order");
    Check(value.At("records").array[0].At("displayName").String()=="物品","current locale projection");
    Check(value.At("hasMore").boolean&&value.At("nextOffset").Int()==1,"bounded pagination");
    request.offset=1;value=Decode(service.Query(request,grants));
    Check(value.At("records").array[0].At("stableItemId").String()=="item_2"&&!value.At("hasMore").boolean,"second page traversal");
    request.offset=100;value=Decode(service.Query(request,grants));Check(value.At("records").array.empty(),"offset beyond end is empty page");
    request.offset=0;
    for(const auto kind:{CatalogKind::Items,CatalogKind::Tasks,CatalogKind::Maps}){
        request.catalog=kind;
        Check(service.Query(request,grants).status==DataStatus::Ok,"all catalog list operations");
        request.operation=DataOperation::Get;request.limit=0;
        request.stableId=kind==CatalogKind::Items?item.id:kind==CatalogKind::Tasks?task.id:map.id;
        const auto result=service.Query(request,grants);value=Decode(result);
        Check(result.status==DataStatus::Ok&&value.At("record").type==json::Value::Type::Object,"all stable ID lookup operations");
        const auto key=kind==CatalogKind::Items?"stableItemId":kind==CatalogKind::Tasks?"stableTaskId":"stableMapId";
        Check(value.At("record").At(key).String()==request.stableId,"stable ID round-trip");
        Check(result.payload.find("price")==std::string::npos&&result.payload.find("scenePath")==std::string::npos,"no economy/internal scene data");
        request.stableId="unknown";Check(service.Query(request,grants).status==DataStatus::NotFound,"unknown stable ID");
        request.stableId.clear();request.operation=DataOperation::List;request.limit=1;
        auto missing=grants;missing.declared.clear();Check(service.Query(request,missing).status==DataStatus::PermissionDenied,"declaration required");
        missing=grants;missing.granted.clear();Check(service.Query(request,missing).status==DataStatus::PermissionDenied,"grant required");
        missing=grants;missing.valid=false;Check(service.Query(request,missing).status==DataStatus::PermissionDenied,"invalid fingerprint denied");
        for(const auto other:{CatalogKind::Items,CatalogKind::Tasks,CatalogKind::Maps})if(kind!=other){
            CatalogGrants isolated{true,{std::string(CatalogPermission(other))},{std::string(CatalogPermission(other))}};
            Check(service.Query(request,isolated).status==DataStatus::PermissionDenied,"catalog permissions isolated");
        }
    }
    request.catalog=CatalogKind::Tasks;value=Decode(service.Query(request,grants));Check(value.At("total").Int()==1,"regular projection avoids duplicate variant IDs");
    service.SetLocale("en-US");value=Decode(service.Query(request,grants));Check(value.At("records").array[0].At("displayName").String()=="Task","locale updates without mutable catalog reads");
    task.nameEn="changed";items.clear();Check(service.Query(request,grants).payload.find("changed")==std::string::npos,"immutable copied projections");
    auto invalid=request;invalid.limit=0;Check(!ValidDataRequest(invalid),"zero limit rejected");
    invalid.limit=65;Check(!ValidDataRequest(invalid),"maximum limit enforced");
    invalid.limit=1;invalid.offset=(std::numeric_limits<std::uint32_t>::max)();Check(!ValidDataRequest(invalid),"offset arithmetic overflow rejected");
    invalid=request;invalid.requestId=0;Check(!ValidDataRequest(invalid),"zero request identity rejected");
    invalid.requestId=(std::numeric_limits<std::uint64_t>::max)();Check(!ValidDataRequest(invalid),"JSON integer ID bound");
    invalid=request;invalid.operation=DataOperation::Get;invalid.limit=0;invalid.stableId=std::string(129,'a');Check(!ValidDataRequest(invalid),"stable ID bound");
    invalid.stableId="../x";Check(!ValidDataRequest(invalid),"no traversal in IDs");
    invalid.stableId="task_1";Check(ValidDataRequest(invalid),"safe stable ID");
    invalid.catalog=static_cast<CatalogKind>(4);Check(service.Query(invalid,grants).status==DataStatus::InvalidRequest,"unknown kind fails safely");
    CatalogPluginService empty({}, {}, {});Check(empty.Query(request,grants).status==DataStatus::Unavailable,"unavailable catalog diagnostic");
    auto large=second;large.nameZh=std::string(7000,'x');large.nameEn=large.nameZh;
    std::vector bigItems{large,large,large};bigItems[0].id="a";bigItems[1].id="b";bigItems[2].id="c";
    CatalogPluginService bounded(bigItems,{},{});request.catalog=CatalogKind::Items;request.limit=64;
    const auto page=bounded.Query(request,grants);value=Decode(page);
    Check(page.status==DataStatus::Ok&&page.payload.size()<=MaximumCatalogPayloadBytes&&value.At("hasMore").boolean,"response shrinks page to byte budget");
    Check(value.At("nextOffset").Int()==1,"shrunk page advances correctly");
    large.nameZh=std::string(MaximumCatalogPayloadBytes,'x');bigItems={large};CatalogPluginService tooLarge(bigItems,{},{});
    Check(tooLarge.Query(request,grants).status==DataStatus::TooLarge,"single oversized record returns bounded error");
    request.operation=DataOperation::Get;request.limit=0;request.stableId=large.id;
    Check(tooLarge.Query(request,grants).status==DataStatus::TooLarge,"oversized get bounded");
    const auto now=std::chrono::steady_clock::now();DataRequestBudget budget,other;
    for(unsigned id=1;id<=16;++id)Check(budget.Begin(id,now)==RequestAdmission::Accepted,"bounded outstanding queue");
    Check(budget.Begin(1,now)==RequestAdmission::Duplicate,"duplicate outstanding rejected");
    Check(budget.Begin(17,now)==RequestAdmission::Limited,"outstanding/rate bounds");
    Check(other.Begin(1,now)==RequestAdmission::Accepted,"independent sessions do not share request IDs");
    Check(budget.Complete(1)&&!budget.Complete(1),"completion only once");
    Check(budget.Begin(17,now)==RequestAdmission::Limited,"completion does not evade burst bound");
    Check(budget.Begin(1,now+std::chrono::seconds(1))==RequestAdmission::Accepted,"completed IDs reusable in next window");
    budget.Clear();Check(budget.Pending()==0,"stop invalidates all outstanding IDs");
    std::cout<<"Catalog projections, permissions, pagination and budgets PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
