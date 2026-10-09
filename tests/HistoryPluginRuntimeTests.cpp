#include "plugins/PluginRuntimeManager.h"
#include "plugins/PluginPipe.h"
#include <iostream>
#include <stdexcept>
using namespace noven::plugins;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Temp {
    std::filesystem::path path=std::filesystem::temp_directory_path()/("noven-history-runtime-"+ipc::RandomSecret());
    Temp(){std::filesystem::create_directory(path);}
    ~Temp(){std::error_code error;std::filesystem::remove_all(path,error);}
};
int wmain(int argc,wchar_t** argv) try {
    Check(argc==8,"Host and six history fixtures");Temp temp;
    std::filesystem::copy_file(argv[1],temp.path/"NovenPluginHost.exe");
    auto service=std::make_shared<CatalogPluginService>(std::span<const noven::data::ItemRecord>{},std::span<const noven::data::TaskRecord>{},std::span<const noven::data::MapRecord>{});
    noven::raid::RaidSession raid;raid.localSessionId="raid-exact_1";raid.startObserved=raid.endObserved=true;
    noven::events::EventRecord event;event.eventId="community-wiki:26936:Exact.%20";event.title="Community";
    service->PublishRaidHistory(std::vector{raid});service->PublishEvents(std::vector{event});
    PluginRuntimeManager runtime(temp.path);runtime.SetCatalogService(service);
    ipc::Handle changed(CreateEventW(nullptr,FALSE,FALSE,nullptr));runtime.SetChangeHandler([&]{SetEvent(changed.Get());});
    PluginStateStore grants;
    const auto record=[&](std::string id,const wchar_t* dll,std::vector<std::string> permissions){
        PluginRecord value;value.state=PluginState::Valid;value.directory=temp.path/"plugins"/id;value.manifest=PluginManifest{};
        auto& manifest=*value.manifest;manifest.id=std::move(id);manifest.manifestVersion=2;manifest.apiVersion=1;
        manifest.runtime=NativeRuntime{"native-dll","plugin.dll"};manifest.requestedPermissions=std::move(permissions);
        std::filesystem::create_directories(value.directory);std::filesystem::copy_file(dll,value.directory/"plugin.dll");return value;
    };
    const auto wait=[&](std::string_view id,const auto& predicate){const auto deadline=ipc::After(5000);
        while(std::chrono::steady_clock::now()<deadline){const auto state=runtime.Snapshot(id);if(state&&predicate(*state))return true;WaitForSingleObject(changed.Get(),100);}return false;};
    const std::vector<std::string> both{"raid.history.read","catalog.events.read"};
    auto healthy=record("com.example.healthy",argv[2],both);
    Check(!runtime.StartNative(healthy,grants)&&runtime.SessionCount()==0,"discovery/absent consent cannot execute history requests");
    Check(grants.Consent(*healthy.manifest)&&runtime.StartNative(healthy,grants)&&runtime.WaitFor(healthy.manifest->id,HostState::Running,10000),"independent healthy history session");
    Check(wait(healthy.manifest->id,[](const auto& state){return state.dataResults==4&&state.pendingData==0;}),"history/event list and exact-ID get callbacks");
    const int modes[]{0,1,2,3,4,7};
    for(unsigned index=0;index<6;++index){const auto mode=modes[index];
        auto value=record("com.example.mode"+std::to_string(mode),argv[index+2],mode==1?std::vector<std::string>{}:mode==2?std::vector<std::string>{"raid.history.read"}:both);
        Check(!runtime.StartNative(value,grants),"another plugin cannot borrow healthy grants");
        Check(grants.Consent(*value.manifest)&&runtime.StartNative(value,grants),"independent consent and session");
        if(mode==3||mode==4){
            Check(runtime.WaitForTerminal(value.manifest->id,8000),"pending result hang/crash bounded");const auto state=runtime.Snapshot(value.manifest->id);
            Check(mode==3?state->error==HostError::DataTimeout:state->state==HostState::Crashed,"dedicated host fault state");
            Check(state->pendingData==0&&state->dataResults==0,"crash/hang invalidates outstanding result");
        }else{
            Check(runtime.WaitFor(value.manifest->id,HostState::Running,10000),"scoped history runtime");
            const unsigned expected=mode==1?0:mode==2?2:mode==7?16:4;
            Check(wait(value.manifest->id,[&](const auto& state){return state.dataResults==expected&&state.pendingData==0;}),"permission isolation, same request IDs and queue bounds");
            Check(runtime.Stop(value.manifest->id)&&runtime.WaitForTerminal(value.manifest->id,6000),"scoped host cleanup");
        }
        Check(runtime.Ping(healthy.manifest->id)&&runtime.WaitForPong(healthy.manifest->id,index+1,5000),"owner and neighboring session remain alive");
    }
    auto cancel=record("com.example.cancel",argv[5],both);
    Check(grants.Consent(*cancel.manifest)&&runtime.StartNative(cancel,grants)&&runtime.WaitFor(cancel.manifest->id,HostState::Running,10000),"cancel session starts");
    Check(wait(cancel.manifest->id,[](const auto& state){return state.pendingData>0;}),"Disable while callback outstanding");
    Check(runtime.Stop(cancel.manifest->id)&&runtime.Snapshot(cancel.manifest->id)->pendingData==0,"Disable invalidates immediately");
    Check(runtime.WaitForTerminal(cancel.manifest->id,6000)&&runtime.Snapshot(cancel.manifest->id)->dataResults==0,"no late callback delivery");
    Check(GetModuleHandleW(L"plugin.dll")==nullptr,"history DLL never loaded in core owner");
    Check(runtime.Stop(healthy.manifest->id)&&runtime.WaitForTerminal(healthy.manifest->id,6000),"owned host exits without orphan");
    std::cout<<"History/event async isolation, denial, queue bounds, crash/hang and Disable PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
