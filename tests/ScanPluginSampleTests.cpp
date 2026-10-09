#include "plugins/PluginRuntimeController.h"
#include "plugins/PluginPipe.h"
#include "ui/PluginPages.h"
#include "ui/BuiltinPages.h"
#include "ui/SidebarLayout.h"
#include "raid/RaidJson.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
namespace json=noven::raid::json;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-scan-demo-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code e;std::filesystem::remove_all(path,e);}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==4,"Host, Scan Demo DLL and manifest required");Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    const std::string id="com.example.noven-scan";const auto directory=temp.path/"plugins"/id;std::filesystem::create_directories(directory);
    std::filesystem::copy_file(argv[2],directory/"noven-scan.dll");std::filesystem::copy_file(argv[3],directory/"manifest.json");
    PluginDiscovery discovery(temp.path);PluginRuntimeManager runtime(temp.path);auto service=std::make_shared<CatalogPluginService>(std::span<const noven::data::ItemRecord>{},std::span<const noven::data::TaskRecord>{},std::span<const noven::data::MapRecord>{});runtime.SetCatalogService(service);
    noven::data::RecentScanEntry scan;scan.scanId=9007199254740993ULL;scan.scannedAtUnixMs=100;scan.stableItemId="exact-stable-item";scan.canonicalName="扫描物品";scan.localSessionId="raid-exact";
    runtime.PublishRecentScans(std::vector{scan});ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    PluginRuntimeController controller(discovery,runtime,temp.path/"data"/"plugin-state.json");controller.Refresh();controller.StartEnabled();
    Check(runtime.SessionCount()==0,"new discovery/Refresh stays Disabled");
    Check(controller.Enable(id,[](const auto&){return false;})==ControlResult::ConsentDeclined&&runtime.SessionCount()==0,"explicit consent required");
    Check(controller.Enable(id,[](const auto&){return true;})==ControlResult::Success&&runtime.WaitFor(id,HostState::Running,10000),"consented Scan Demo starts");
    auto registry=noven::ui::MakeBuiltinPageRegistry();noven::ui::PluginPages pages(registry);pages.Sync(runtime.Snapshots());const noven::ui::PageId pageId{"plugin.com.example.noven-scan.dashboard"};
    auto layout=noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{});layout=noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{},layout.maximum);
    Check(registry.Find(pageId)->displayTitle=="Scan Demo"&&layout.Find(pageId)->visible,"recognized page enters real reachable sidebar");
    const auto wait=[&](const auto& predicate){const auto deadline=ipc::After(5000);while(std::chrono::steady_clock::now()<deadline){auto state=runtime.Snapshot(id);if(predicate(*state))return true;WaitForSingleObject(changed.Get(),50);}return false;};
    const auto action=[&](std::string_view name){const auto deadline=ipc::After(5000);while(std::chrono::steady_clock::now()<deadline){const auto state=runtime.Snapshot(id);if(runtime.Action(id,state->generation,"dashboard",name))return;WaitForSingleObject(changed.Get(),50);}throw std::runtime_error("sample action dispatched");};
    const auto barrier=[&]{auto count=runtime.Snapshot(id)->pongs;Check(runtime.Ping(id)&&runtime.WaitForPong(id,count+1,5000),"protocol barrier");};
    action("history");Check(wait([](const auto& state){return state.dataResults==1&&state.pendingData==0;}),"async safe history callback");
    auto state=runtime.Snapshot(id);auto history=json::Parser(state->pages[0].document.blocks[7].text).Parse();
    Check(history.At("catalog").String()=="recentScans"&&history.At("records").array[0].At("scanId").String()==std::to_string(scan.scanId),"stored scan identity exact in declarative page");
    runtime.NotifyScanCompleted(scan);barrier();Check(runtime.Snapshot(id)->scanEvents==0,"no event before user subscription");
    action("subscribe");Check(wait([](const auto& s){return s.scanSubscribed&&s.pages[0].document.blocks[1].text.starts_with("Subscription: Active");}),"subscription callback reflected in page");barrier();
    Check(runtime.Snapshot(id)->scanEvents==0,"no history replay");runtime.NotifyScanCompleted(scan);Check(wait([](const auto& s){return s.scanEvents==1;}),"real native event callback round trip");
    auto event=json::Parser(runtime.Snapshot(id)->pages[0].document.blocks.back().text).Parse();Check(event.At("stableItemId").String()==scan.stableItemId&&event.At("localSessionId").String()==*scan.localSessionId,"safe item and optional association visible");
    action("unsubscribe");Check(wait([](const auto& s){return !s.scanSubscribed&&s.pages[0].document.blocks[1].text.starts_with("Subscription: Inactive");}),"unsubscribe reflected");
    runtime.NotifyScanCompleted(scan);barrier();Check(runtime.Snapshot(id)->scanEvents==1,"no later event after unsubscribe");
    Check(controller.Disable(id)==ControlResult::Success&&runtime.WaitForTerminal(id,7000)&&runtime.Snapshot(id)->shutdownAcknowledged,"Disable clean Host exit");
    pages.Sync(runtime.Snapshots());Check(!registry.Contains(pageId)&&!controller.State().Intent(id).enabled,"page and persisted intent removed");
    Check(GetModuleHandleW(L"noven-scan.dll")==nullptr,"plugin never loaded in owner");std::cout<<"Scan Demo real history/subscription/page lifecycle PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
