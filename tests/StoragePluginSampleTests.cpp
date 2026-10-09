#include "plugins/PluginRuntimeController.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-store-demo-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==4,"Host, Storage DLL and manifest required");Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    const std::string id="com.example.noven-storage";const auto dir=temp.path/"plugins"/id;std::filesystem::create_directories(dir);
    std::filesystem::copy_file(argv[2],dir/"noven-storage.dll");std::filesystem::copy_file(argv[3],dir/"manifest.json");
    for(unsigned restart=0;restart<2;++restart){
        PluginDiscovery discovery(temp.path);PluginRuntimeManager runtime(temp.path);ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
        PluginRuntimeController controller(discovery,runtime,temp.path/"data"/"plugin-state.json");controller.Refresh();controller.StartEnabled();Check(runtime.SessionCount()==0,"Disabled discovery/refresh never starts Host");
        Check(controller.Enable(id,[](const auto&){return false;})==ControlResult::ConsentDeclined,"explicit decision required");
        Check(controller.Enable(id,[](const auto&){return true;})==ControlResult::Success&&runtime.WaitFor(id,HostState::Running,10000),"storage consent starts Host");
        const auto wait=[&](std::string_view expected){const auto deadline=ipc::After(7000);while(std::chrono::steady_clock::now()<deadline){auto state=runtime.Snapshot(id);if(!state->pages.empty()&&state->pages[0].document.blocks.size()>1&&state->pages[0].document.blocks[1].text==expected)return true;WaitForSingleObject(changed.Get(),50);}return false;};
        const auto action=[&](std::string_view name){const auto deadline=ipc::After(5000);while(std::chrono::steady_clock::now()<deadline){auto state=runtime.Snapshot(id);if(runtime.Action(id,state->generation,"dashboard",name))return;WaitForSingleObject(changed.Get(),50);}throw std::runtime_error("action dispatch");};
        Check(wait(restart?"Stored counter: 1 | Keys: 0 | Status: 0":"Stored counter: 0 | Keys: 0 | Status: 2"),"actual owner restart preserves value");
        if(!restart){action("save");Check(wait("Stored counter: 1 | Keys: 0 | Status: 0"),"async SET saved");action("list");Check(wait("Stored counter: 1 | Keys: 1 | Status: 0"),"LIST logical keys");}
        else{action("delete");Check(wait("Stored counter: 0 | Keys: 0 | Status: 0"),"DELETE saved");action("reload");Check(wait("Stored counter: 0 | Keys: 0 | Status: 2"),"GET missing explicit");}
        Check(controller.Disable(id)==ControlResult::Success&&runtime.WaitForTerminal(id,7000)&&runtime.Snapshot(id)->shutdownAcknowledged,"Disable host exit");
        Check(runtime.Snapshot(id)->pages.empty()&&GetModuleHandleW(L"noven-storage.dll")==nullptr,"pages cleared and no owner DLL loading");
    }
    std::cout<<"Storage Demo real async GET/SET/LIST/DELETE/owner restart PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
