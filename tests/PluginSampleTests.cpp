#include "plugins/PluginRuntimeController.h"
#include "plugins/PluginPipe.h"
#include "ui/PluginPages.h"
#include "ui/BuiltinPages.h"
#include "ui/SidebarLayout.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-sample-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==4,"host, C sample DLL and manifest required");Temp temp;
    std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");const auto directory=temp.path/"plugins"/"com.example.noven-hello";
    std::filesystem::create_directories(directory);std::filesystem::copy_file(argv[2],directory/"noven-hello.dll");std::filesystem::copy_file(argv[3],directory/"manifest.json");
    PluginDiscovery discovery(temp.path);discovery.Refresh();ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));PluginRuntimeManager runtime(temp.path);
    runtime.SetChangeHandler([&]{SetEvent(changed.Get());});PluginRuntimeController controller(discovery,runtime,temp.path/"data"/"plugin-state.json");
    controller.StartEnabled();controller.Refresh();Check(runtime.SessionCount()==0,"sample defaults Disabled");
    const std::string id="com.example.noven-hello";
    Check(controller.Enable(id,[](const auto&){return true;})==ControlResult::Success&&runtime.WaitFor(id,HostState::Running,10000),"C ABI sample loads after explicit consent");
    const auto snapshot=runtime.Snapshot(id);Check(snapshot->pages.size()==1&&snapshot->pages[0].title=="Hello Plugin"&&snapshot->pages[0].document.HasAction("increment"),"sample page/document published");
    auto registry=noven::ui::MakeBuiltinPageRegistry();noven::ui::PluginPages pages(registry);pages.Sync({*snapshot});
    const noven::ui::PageId pageId{"plugin.com.example.noven-hello.dashboard"};
    const auto initial=noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{});
    const auto geometry=noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{},initial.maximum);
    Check(registry.Contains(pageId)&&registry.Find(pageId)->displayTitle=="Hello Plugin"&&geometry.Find(pageId),"real C callback/Host/IPC reaches registry and computed geometry");
    Check(geometry.Find(pageId)->visible&&geometry.Find(pageId)->rect.bottom<=geometry.navigationViewport.bottom
        &&geometry.HitTest(30,(geometry.Find(pageId)->rect.top+geometry.Find(pageId)->rect.bottom)/2)==pageId,"real registered sample is reachable and selectable below capacity threshold");
    Check(GetModuleHandleW(L"noven-hello.dll")==nullptr,"sample DLL absent from owner");
    Check(runtime.Action(id,snapshot->generation,"dashboard","increment"),"scoped counter action");
    bool updated=false;const auto deadline=ipc::After(5000);
    while(std::chrono::steady_clock::now()<deadline){
        const auto next=runtime.Snapshot(id);if(next->pages.size()==1&&next->pages[0].document.blocks[2].value=="1"){updated=true;break;}
        WaitForSingleObject(changed.Get(),100);
    }
    Check(updated,"button round-trip publishes new counter document");
    Check(controller.Disable(id)==ControlResult::Success&&runtime.WaitForTerminal(id,6000)&&runtime.Snapshot(id)->pages.empty()&&runtime.Snapshot(id)->shutdownAcknowledged,"sample clean shutdown removes pages");
    pages.Sync(runtime.Snapshots());Check(!registry.Contains(pageId)&&!noven::ui::BuildSidebarLayout(registry,700,noven::ui::UiTheme{}).Find(pageId),"real Disable removes registry and geometry");
    Check(!controller.State().Intent(id).enabled,"disabled sample does not restart");
    std::cout<<"C sample load/action/shutdown PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
