#include "plugins/PluginRuntimeController.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
struct Temp {std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-http-demo-"+ipc::RandomSecret());Temp(){std::filesystem::create_directory(path);}~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==4,"test Host, DLL, manifest required");Temp temp;std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    const std::string id="com.example.noven-http";const auto dir=temp.path/"plugins"/id;std::filesystem::create_directories(dir);
    std::filesystem::copy_file(argv[2],dir/"noven-http.dll");std::filesystem::copy_file(argv[3],dir/"manifest.json");
    PluginDiscovery discovery(temp.path);PluginRuntimeManager runtime(temp.path);ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    PluginRuntimeController controller(discovery,runtime,temp.path/"data"/"plugin-state.json");controller.Refresh();controller.StartEnabled();Check(runtime.SessionCount()==0,"discovery never requests HTTP or starts Host");
    Check(controller.Enable(id,[](const auto&){return false;})==ControlResult::ConsentDeclined,"explicit consent required");
    const auto enabled=controller.Enable(id,[](const auto& manifest){return manifest.networkOrigins==std::vector<std::string>{"https://httpbin.org"};});
    const auto running=enabled==ControlResult::Success&&runtime.WaitFor(id,HostState::Running,10000);
    if(!running){std::cerr<<"Enable="<<static_cast<int>(enabled);if(auto state=runtime.Snapshot(id))std::cerr<<" state="<<static_cast<int>(state->state)<<" error="<<static_cast<int>(state->error)<<" load="<<state->loadResult;std::cerr<<'\n';}
    Check(running,"authenticated origin grant before DLL initialization");
    const auto wait=[&](std::string_view expected){const auto deadline=ipc::After(7000);while(std::chrono::steady_clock::now()<deadline){auto snapshot=runtime.Snapshot(id);if(snapshot&&!snapshot->pages.empty()&&snapshot->pages[0].document.blocks[1].text==expected)return true;WaitForSingleObject(changed.Get(),50);}return false;};
    const auto action=[&](std::string_view name){const auto deadline=ipc::After(5000);while(std::chrono::steady_clock::now()<deadline){auto snapshot=runtime.Snapshot(id);if(runtime.Action(id,snapshot->generation,"dashboard",name))return;WaitForSingleObject(changed.Get(),50);}throw std::runtime_error("action admission");};
    Check(wait("Status: 0 | HTTP: 0 | Pending: 0"),"no automatic request on initialize");
    action("get");Check(wait("Status: 0 | HTTP: 200 | Pending: 0"),"GET callback through real DLL/Host");
    Check(runtime.Snapshot(id)->pages[0].document.blocks[2].text=="offline result","borrowed body copied into declarative page");
    action("post");Check(wait("Status: 0 | HTTP: 201 | Pending: 0"),"POST callback");action("head");Check(wait("Status: 0 | HTTP: 200 | Pending: 0"),"HEAD callback");
    action("denied");Check(wait("Status: -2 | HTTP: 0 | Pending: 0"),"undeclared origin denied without transport");
    Check(controller.Disable(id)==ControlResult::Success&&runtime.WaitForTerminal(id,7000)&&runtime.Snapshot(id)->shutdownAcknowledged&&runtime.Snapshot(id)->pages.empty(),"Disable clears delivery/pages and exits Host");
    Check(GetModuleHandleW(L"noven-http.dll")==nullptr,"owner never loads DLL");
    std::cout<<"HTTP Demo authenticated Host/DLL async callbacks, consent, offline isolation PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
