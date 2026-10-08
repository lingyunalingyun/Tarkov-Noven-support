#include "plugins/PluginRuntimeController.h"
#include "plugins/PluginPipe.h"
#include "ui/PluginPages.h"
#include "ui/BuiltinPages.h"
#include "ui/SidebarLayout.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <set>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-catalog-demo-"+ipc::RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==5,"Host, Catalog Demo DLL, manifest, assets required");
    const std::filesystem::path assets(argv[4]);std::wstring error;
    noven::data::ItemCatalog items;noven::data::TaskCatalog tasks;noven::data::MapCatalog maps;
    Check(items.Load(assets/"data"/"items_catalog.tsv",error)&&tasks.Load(assets/"data",error)&&maps.Load(assets/"data",error),"actual Noven static catalogs load");
    auto service=std::make_shared<CatalogPluginService>(items.Items(),tasks.Tasks(),maps.Maps(),"en-US");
    CatalogGrants scope{true,{"catalog.items.read","catalog.tasks.read","catalog.maps.read"},{"catalog.items.read","catalog.tasks.read","catalog.maps.read"}};
    DataRequest request;request.requestId=1;request.limit=64;
    for(const auto kind:{CatalogKind::Items,CatalogKind::Tasks,CatalogKind::Maps}){
        request.catalog=kind;request.offset=0;std::set<std::string> ids;bool more=true;
        const auto key=kind==CatalogKind::Items?"stableItemId":kind==CatalogKind::Tasks?"stableTaskId":"stableMapId";
        while(more){
            const auto result=service->Query(request,scope);
            Check(result.status==DataStatus::Ok&&result.payload.size()<=MaximumCatalogPayloadBytes,"actual catalogs fit bounded paginated responses");
            ipc::Message wire{ipc::MessageType::DataResult};wire.dataResult=result;
            Check(ipc::Serialize(wire).size()<=ipc::MaximumFrameBytes,"real projections fit unchanged frame budget");
            const auto json=noven::raid::json::Parser(result.payload).Parse();
            for(const auto& record:json.At("records").Array())Check(ids.insert(record.At(key).String()).second,"real IDs traverse exactly once");
            more=json.At("hasMore").Bool();request.offset=static_cast<std::uint32_t>(json.At("nextOffset").Int());
            if(more)Check(request.offset==ids.size(),"smaller pages continue without skipped identities");
            else Check(json.At("total").Int()==static_cast<std::int64_t>(ids.size()),"real traversal covers complete projected catalog");
        }
        if(kind==CatalogKind::Items)Check(ids.size()==items.Items().size(),"all static items exposed without economy");
        if(kind==CatalogKind::Maps)Check(ids.size()==maps.Maps().size(),"all accepted regular map identities");
    }
    Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    const auto directory=temp.path/"plugins"/"com.example.noven-catalog";std::filesystem::create_directories(directory);
    std::filesystem::copy_file(argv[2],directory/"noven-catalog.dll");std::filesystem::copy_file(argv[3],directory/"manifest.json");
    PluginDiscovery discovery(temp.path);discovery.Refresh();PluginRuntimeManager runtime(temp.path);runtime.SetCatalogService(service);
    ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    PluginRuntimeController controller(discovery,runtime,temp.path/"data"/"plugin-state.json");
    controller.StartEnabled();controller.Refresh();Check(runtime.SessionCount()==0,"discovery/refresh remains Disabled");
    const std::string id="com.example.noven-catalog";
    Check(controller.Enable(id,[](const auto&){return false;})==ControlResult::ConsentDeclined&&runtime.SessionCount()==0,"catalog execution still needs explicit user consent");
    Check(controller.Enable(id,[](const auto&){return true;})==ControlResult::Success&&runtime.WaitFor(id,HostState::Running,10000),"catalog example enables after explicit consent");
    const auto snapshot=runtime.Snapshot(id);
    Check(snapshot->dataResults==0&&snapshot->pages.size()==1&&snapshot->pages[0].title=="Catalog Demo","no catalog query before button interaction");
    auto registry=noven::ui::MakeBuiltinPageRegistry();noven::ui::PluginPages pages(registry);pages.Sync(runtime.Snapshots());
    const noven::ui::PageId pageId{"plugin.com.example.noven-catalog.dashboard"};
    const auto initial=noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{});
    const auto geometry=noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{},initial.maximum);
    Check(geometry.Find(pageId)&&geometry.Find(pageId)->visible&&registry.Find(pageId)->source==noven::ui::PageSource::Plugin,"real catalog page enters reachable dynamic sidebar");
    unsigned results=0;
    for(const auto action:{"items","tasks","maps"}){
        Check(runtime.Action(id,snapshot->generation,"dashboard",action),"declarative catalog button dispatch");results+=2;
        bool received=false;const auto deadline=ipc::After(5000);
        while(std::chrono::steady_clock::now()<deadline){
            const auto state=runtime.Snapshot(id);
            if(state->dataResults==results&&state->pendingData==0&&state->pages.size()==1){
                const auto& text=state->pages[0].document.blocks.back().text;
                const auto json=noven::raid::json::Parser(text).Parse();
                Check(json.At("operation").String()=="get"&&json.At("status").String()=="ok"&&json.At("catalog").String()==action,"async list triggers exact stable-ID get result");
                const auto key=std::string(action)=="items"?"stableItemId":std::string(action)=="tasks"?"stableTaskId":"stableMapId";
                Check(!json.At("record").At(key).String().empty(),"declarative text visibly retains stable identity");received=true;break;
            }
            WaitForSingleObject(changed.Get(),100);
        }
        Check(received,"actual Items/Tasks/Maps asynchronous end-to-end response");
    }
    Check(GetModuleHandleW(L"noven-catalog.dll")==nullptr,"catalog DLL never loaded in owner");
    Check(controller.Disable(id)==ControlResult::Success&&runtime.WaitForTerminal(id,6000)&&runtime.Snapshot(id)->shutdownAcknowledged,"example disabled and Host exits");
    pages.Sync(runtime.Snapshots());Check(!registry.Contains(pageId)&&!controller.State().Intent(id).enabled,"Disable removes registered page and persisted intent");
    Check(!items.Items().empty()&&maps.Maps().size()==17,"catalog reads never mutate product catalog objects");
    std::cout<<"Catalog Demo real catalog / async list-get / dynamic page PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
