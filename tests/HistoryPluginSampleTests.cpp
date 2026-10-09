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
    std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-history-demo-"+ipc::RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==4,"Host, History Demo DLL and manifest required");
    auto service=std::make_shared<CatalogPluginService>(std::span<const noven::data::ItemRecord>{},std::span<const noven::data::TaskRecord>{},std::span<const noven::data::MapRecord>{},"en-US");
    noven::raid::RaidSession raid;raid.localSessionId="raid-roundtrip_1";raid.startObserved=raid.endObserved=true;raid.startedAt=100;raid.endedAt=200;raid.duration=100;
    noven::events::EventRecord event;event.eventId="community-wiki:26936:Exact.%20";event.title="Community event";event.sourceEvidence.emplace_back().sourceKind=noven::events::SourceKind::CommunityWiki;
    service->PublishRaidHistory(std::vector{raid});service->PublishEvents(std::vector{event});
    Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    const auto directory=temp.path/"plugins"/"com.example.noven-history";std::filesystem::create_directories(directory);
    std::filesystem::copy_file(argv[2],directory/"noven-history.dll");std::filesystem::copy_file(argv[3],directory/"manifest.json");
    PluginDiscovery discovery(temp.path);discovery.Refresh();PluginRuntimeManager runtime(temp.path);runtime.SetCatalogService(service);
    ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    PluginRuntimeController controller(discovery,runtime,temp.path/"data"/"plugin-state.json");
    controller.StartEnabled();controller.Refresh();Check(runtime.SessionCount()==0,"discovery/refresh remains Disabled");
    const std::string id="com.example.noven-history";
    Check(controller.Enable(id,[](const auto&){return false;})==ControlResult::ConsentDeclined&&runtime.SessionCount()==0,"catalog execution still needs explicit user consent");
    Check(controller.Enable(id,[](const auto&){return true;})==ControlResult::Success&&runtime.WaitFor(id,HostState::Running,10000),"catalog example enables after explicit consent");
    const auto snapshot=runtime.Snapshot(id);
    Check(snapshot->dataResults==0&&snapshot->pages.size()==1&&snapshot->pages[0].title=="History Demo","no catalog query before button interaction");
    auto registry=noven::ui::MakeBuiltinPageRegistry();noven::ui::PluginPages pages(registry);pages.Sync(runtime.Snapshots());
    const noven::ui::PageId pageId{"plugin.com.example.noven-history.dashboard"};
    const auto initial=noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{});
    const auto geometry=noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{},initial.maximum);
    Check(geometry.Find(pageId)&&geometry.Find(pageId)->visible&&registry.Find(pageId)->source==noven::ui::PageSource::Plugin,"real catalog page enters reachable dynamic sidebar");
    unsigned results=0;
    for(const auto action:{"raids","events"}){
        Check(runtime.Action(id,snapshot->generation,"dashboard",action),"declarative catalog button dispatch");results+=2;
        bool received=false;const auto deadline=ipc::After(5000);
        while(std::chrono::steady_clock::now()<deadline){
            const auto state=runtime.Snapshot(id);
            if(state->dataResults==results&&state->pendingData==0&&state->pages.size()==1){
                const auto& text=state->pages[0].document.blocks.back().text;
                const auto json=noven::raid::json::Parser(text).Parse();
                Check(json.At("operation").String()=="get"&&json.At("status").String()=="ok"&&json.At("catalog").String()==(std::string(action)=="raids"?"raidHistory":"events"),"async list triggers exact stable-ID get result");
                const auto key=std::string(action)=="raids"?"localSessionId":"eventId";
                Check(json.At("record").At(key).String()==(std::string(action)=="raids"?raid.localSessionId:event.eventId),"declarative text visibly retains exact history/event identity");received=true;break;
            }
            WaitForSingleObject(changed.Get(),100);
        }
        Check(received,"completed history/events asynchronous end-to-end response");
    }
    Check(GetModuleHandleW(L"noven-history.dll")==nullptr,"catalog DLL never loaded in owner");
    Check(controller.Disable(id)==ControlResult::Success&&runtime.WaitForTerminal(id,6000)&&runtime.Snapshot(id)->shutdownAcknowledged,"example disabled and Host exits");
    pages.Sync(runtime.Snapshots());Check(!registry.Contains(pageId)&&!controller.State().Intent(id).enabled,"Disable removes registered page and persisted intent");
    Check(raid.localSessionId=="raid-roundtrip_1"&&event.eventId=="community-wiki:26936:Exact.%20","reads never mutate source snapshots");
    std::cout<<"History Demo real catalog / async list-get / dynamic page PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
